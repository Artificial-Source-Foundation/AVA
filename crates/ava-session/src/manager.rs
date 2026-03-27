//! CRUD operations for sessions, messages, and bookmarks.

use ava_types::{AvaError, Message, Result, Session};
use chrono::Utc;
use rusqlite::{params, Connection, OptionalExtension};
use serde_json::Value;
use uuid::Uuid;

use crate::helpers::{
    db_error, parse_datetime, parse_uuid, role_to_str, str_to_role, to_conversion_error,
    MIGRATION_SQL, SCHEMA_SQL,
};
use crate::{Bookmark, SessionManager};

fn serialize_json<T: serde::Serialize>(value: &T) -> Result<String> {
    serde_json::to_string(value).map_err(|error| AvaError::SerializationError(error.to_string()))
}

fn deserialize_json<T: serde::de::DeserializeOwned>(
    index: usize,
    raw: &str,
) -> std::result::Result<T, rusqlite::Error> {
    serde_json::from_str(raw).map_err(|error| {
        rusqlite::Error::FromSqlConversionFailure(
            index,
            rusqlite::types::Type::Text,
            Box::new(error),
        )
    })
}

pub(crate) fn row_to_message(
    row: &rusqlite::Row<'_>,
) -> std::result::Result<Message, rusqlite::Error> {
    let tool_calls = deserialize_json::<Vec<ava_types::ToolCall>>(4, &row.get::<_, String>(4)?)?;
    let tool_results =
        deserialize_json::<Vec<ava_types::ToolResult>>(5, &row.get::<_, String>(5)?)?;
    let tool_call_id: Option<String> = row.get(6)?;
    let images_json: String = row
        .get::<_, Option<String>>(7)?
        .unwrap_or_else(|| "[]".to_string());
    let images = deserialize_json::<Vec<ava_types::ImageContent>>(7, &images_json)?;
    let parent_id_str: Option<String> = row.get(8)?;
    let parent_id = parent_id_str
        .as_deref()
        .map(parse_uuid)
        .transpose()
        .map_err(to_conversion_error)?;
    let agent_visible = row.get::<_, Option<i64>>(9)?.unwrap_or(1) != 0;
    let user_visible = row.get::<_, Option<i64>>(10)?.unwrap_or(1) != 0;
    let original_content: Option<String> = row.get(11)?;
    let structured_content_json: String = row
        .get::<_, Option<String>>(12)?
        .unwrap_or_else(|| "[]".to_string());
    let structured_content =
        deserialize_json::<Vec<ava_types::StructuredContentBlock>>(12, &structured_content_json)?;
    let metadata_json: String = row
        .get::<_, Option<String>>(13)?
        .unwrap_or_else(|| "{}".to_string());
    let metadata = deserialize_json::<serde_json::Value>(13, &metadata_json)?;

    Ok(Message {
        id: parse_uuid(&row.get::<_, String>(0)?).map_err(to_conversion_error)?,
        role: str_to_role(&row.get::<_, String>(1)?).map_err(to_conversion_error)?,
        content: row.get(2)?,
        timestamp: parse_datetime(&row.get::<_, String>(3)?).map_err(to_conversion_error)?,
        tool_calls,
        tool_results,
        tool_call_id,
        images,
        parent_id,
        agent_visible,
        user_visible,
        original_content,
        structured_content,
        metadata,
    })
}

impl SessionManager {
    pub fn save(&self, session: &Session) -> Result<()> {
        tracing::debug!(
            "Session saved: {}, {} messages",
            session.id,
            session.messages.len()
        );
        let mut conn = self.open_conn()?;
        let tx = conn.transaction().map_err(db_error)?;

        tx.execute(
            "INSERT INTO sessions (id, created_at, updated_at, metadata, parent_id, token_usage, branch_head)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
             ON CONFLICT(id) DO UPDATE SET
               updated_at = excluded.updated_at,
               metadata = excluded.metadata,
               parent_id = excluded.parent_id,
               token_usage = excluded.token_usage,
               branch_head = excluded.branch_head",
            params![
                session.id.to_string(),
                session.created_at.to_rfc3339(),
                session.updated_at.to_rfc3339(),
                serde_json::to_string(&session.metadata)
                    .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                session.metadata.get("parent_id").and_then(Value::as_str),
                serde_json::to_string(&session.token_usage)
                    .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                session.branch_head.map(|id| id.to_string()),
            ],
        )
        .map_err(db_error)?;

        // Upsert each message with INSERT OR REPLACE — no DELETE-all needed.
        // This is crash-safe: if the process dies mid-transaction, existing
        // messages remain intact because the transaction rolls back.
        for message in &session.messages {
            Self::upsert_message_in_tx(&tx, session.id, message)?;
        }

        Self::delete_orphaned_messages_in_tx(&tx, session.id, &session.messages)?;

        tx.commit().map_err(db_error)
    }

    /// Persist a single message to the database, upserting it.
    ///
    /// This is the preferred method for incremental saves (e.g. checkpoints).
    /// It only touches one row and updates the session's `updated_at` timestamp.
    /// If the process crashes, previously persisted messages are unaffected.
    pub fn add_message(&self, session_id: Uuid, message: &Message) -> Result<()> {
        let mut conn = self.open_conn()?;
        let tx = conn.transaction().map_err(db_error)?;

        Self::upsert_message_in_tx(&tx, session_id, message)?;

        tx.execute(
            "UPDATE sessions SET updated_at = ?1 WHERE id = ?2",
            params![Utc::now().to_rfc3339(), session_id.to_string()],
        )
        .map_err(db_error)?;

        tx.commit().map_err(db_error)
    }

    /// Persist multiple messages incrementally without deleting existing ones.
    ///
    /// Like `add_message` but batched in a single transaction for efficiency.
    pub fn add_messages(&self, session_id: Uuid, messages: &[Message]) -> Result<()> {
        if messages.is_empty() {
            return Ok(());
        }
        let mut conn = self.open_conn()?;
        let tx = conn.transaction().map_err(db_error)?;

        for message in messages {
            Self::upsert_message_in_tx(&tx, session_id, message)?;
        }

        tx.execute(
            "UPDATE sessions SET updated_at = ?1 WHERE id = ?2",
            params![Utc::now().to_rfc3339(), session_id.to_string()],
        )
        .map_err(db_error)?;

        tx.commit().map_err(db_error)
    }

    /// Update only the content of an existing message.
    ///
    /// This is O(1) — a single UPDATE statement. Use this when only the
    /// message text has changed (e.g. streaming content finalization).
    pub fn update_message_content(
        &self,
        session_id: Uuid,
        message_id: Uuid,
        content: &str,
    ) -> Result<()> {
        let mut conn = self.open_conn()?;
        let tx = conn.transaction().map_err(db_error)?;
        let updated = tx
            .execute(
                "UPDATE messages SET content = ?1 WHERE id = ?2 AND session_id = ?3",
                params![content, message_id.to_string(), session_id.to_string()],
            )
            .map_err(db_error)?;

        if updated == 0 {
            return Err(AvaError::NotFound(format!(
                "message {message_id} not found in session {session_id}"
            )));
        }

        tx.execute(
            "UPDATE sessions SET updated_at = ?1 WHERE id = ?2",
            params![Utc::now().to_rfc3339(), session_id.to_string()],
        )
        .map_err(db_error)?;

        tx.commit().map_err(db_error)
    }

    /// Upsert a single message row within an existing transaction.
    pub(crate) fn upsert_message_in_tx(
        tx: &rusqlite::Transaction<'_>,
        session_id: Uuid,
        message: &Message,
    ) -> Result<()> {
        tx.execute(
            "INSERT INTO messages (id, session_id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id, agent_visible, user_visible, original_content, structured_content, metadata)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15)
             ON CONFLICT(id) DO UPDATE SET
                content = excluded.content,
                tool_calls = excluded.tool_calls,
                tool_results = excluded.tool_results,
                tool_call_id = excluded.tool_call_id,
                images = excluded.images,
                parent_id = excluded.parent_id,
                agent_visible = excluded.agent_visible,
                user_visible = excluded.user_visible,
                original_content = excluded.original_content,
                structured_content = excluded.structured_content,
                metadata = excluded.metadata",
            params![
                message.id.to_string(),
                session_id.to_string(),
                role_to_str(&message.role),
                message.content,
                message.timestamp.to_rfc3339(),
                serialize_json(&message.tool_calls)?,
                serialize_json(&message.tool_results)?,
                message.tool_call_id.as_deref(),
                serialize_json(&message.images)?,
                message.parent_id.map(|id| id.to_string()),
                i64::from(message.agent_visible),
                i64::from(message.user_visible),
                message.original_content.as_deref(),
                serialize_json(&message.structured_content)?,
                serialize_json(&message.metadata)?,
            ],
        )
        .map_err(db_error)?;
        Ok(())
    }

    fn delete_orphaned_messages_in_tx(
        tx: &rusqlite::Transaction<'_>,
        session_id: Uuid,
        messages: &[Message],
    ) -> Result<()> {
        if messages.is_empty() {
            tx.execute(
                "DELETE FROM messages WHERE session_id = ?1",
                params![session_id.to_string()],
            )
            .map_err(db_error)?;
            return Ok(());
        }

        tx.execute_batch(
            "CREATE TEMP TABLE IF NOT EXISTS current_session_message_ids (
                 id TEXT PRIMARY KEY
             );
             DELETE FROM current_session_message_ids;",
        )
        .map_err(db_error)?;

        {
            let mut insert = tx
                .prepare("INSERT INTO current_session_message_ids (id) VALUES (?1)")
                .map_err(db_error)?;
            for message in messages {
                insert
                    .execute(params![message.id.to_string()])
                    .map_err(db_error)?;
            }
        }

        let deleted = tx
            .execute(
                "DELETE FROM messages
                 WHERE session_id = ?1
                   AND id NOT IN (SELECT id FROM current_session_message_ids)",
                params![session_id.to_string()],
            )
            .map_err(db_error)?;

        tracing::debug!(
            session_id = %session_id,
            retained = messages.len(),
            deleted,
            "synchronized persisted session messages"
        );

        Ok(())
    }

    pub fn get(&self, id: Uuid) -> Result<Option<Session>> {
        tracing::debug!("Session loading: {id}");
        let conn = self.open_conn()?;
        self.get_with_conn(&conn, id)
    }

    pub fn list_recent(&self, limit: usize) -> Result<Vec<Session>> {
        let conn = self.open_conn()?;
        let mut stmt = conn
            .prepare("SELECT id FROM sessions ORDER BY updated_at DESC, id DESC LIMIT ?1")
            .map_err(db_error)?;

        let ids = stmt
            .query_map(params![limit as i64], |row| row.get::<_, String>(0))
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let mut sessions = Vec::new();
        for id in ids {
            if let Some(session) = self.get_with_conn(&conn, parse_uuid(&id)?)? {
                sessions.push(session);
            }
        }
        Ok(sessions)
    }

    pub fn fork(&self, session: &Session) -> Result<Session> {
        let mut forked = Session::new();
        forked.messages = session.messages.clone();
        forked.metadata = session.metadata.clone();
        forked.metadata["parent_id"] = Value::String(session.id.to_string());
        Ok(forked)
    }

    /// List all child sessions (sub-agent sessions) whose `parent_id` matches
    /// the given session ID.
    pub fn get_children(&self, parent_id: Uuid) -> Result<Vec<Session>> {
        let conn = self.open_conn()?;
        let mut stmt = conn
            .prepare("SELECT id FROM sessions WHERE parent_id = ?1 ORDER BY created_at ASC")
            .map_err(db_error)?;

        let ids = stmt
            .query_map(params![parent_id.to_string()], |row| {
                row.get::<_, String>(0)
            })
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let mut sessions = Vec::new();
        for id in ids {
            if let Some(session) = self.get_with_conn(&conn, parse_uuid(&id)?)? {
                sessions.push(session);
            }
        }
        Ok(sessions)
    }

    pub fn find_recent_child_by_external_link(
        &self,
        parent_id: Uuid,
        agent_type: &str,
        provider: &str,
        cwd: &str,
    ) -> Result<Option<Session>> {
        let children = self.get_children(parent_id)?;
        Ok(children.into_iter().rev().find(|session| {
            session.metadata.get("agent_type").and_then(Value::as_str) == Some(agent_type)
                && session
                    .metadata
                    .get("externalLink")
                    .and_then(|value| {
                        serde_json::from_value::<ava_types::ExternalSessionLink>(value.clone()).ok()
                    })
                    .is_some_and(|link| {
                        link.provider.as_deref() == Some(provider)
                            && link.cwd.as_deref() == Some(cwd)
                            && link.external_session_id.as_deref().is_some()
                    })
        }))
    }

    pub fn recent_delegation_records(
        &self,
        parent_id: Uuid,
        limit: usize,
    ) -> Result<Vec<ava_types::DelegationRecord>> {
        let mut records = Vec::new();
        for session in self.get_children(parent_id)?.into_iter().rev() {
            if let Some(record) = session.metadata.get("delegation").and_then(|value| {
                serde_json::from_value::<ava_types::DelegationRecord>(value.clone()).ok()
            }) {
                records.push(record);
                if records.len() >= limit {
                    break;
                }
            }
        }
        Ok(records)
    }

    pub fn delete(&self, id: Uuid) -> Result<()> {
        let mut conn = self.open_conn()?;
        let tx = conn.transaction().map_err(db_error)?;

        tx.execute(
            "DELETE FROM bookmarks WHERE session_id = ?1",
            params![id.to_string()],
        )
        .map_err(db_error)?;

        tx.execute(
            "DELETE FROM messages WHERE session_id = ?1",
            params![id.to_string()],
        )
        .map_err(db_error)?;

        let deleted = tx
            .execute(
                "DELETE FROM sessions WHERE id = ?1",
                params![id.to_string()],
            )
            .map_err(db_error)?;

        if deleted == 0 {
            return Err(AvaError::NotFound(format!("session {id} not found")));
        }

        tx.commit().map_err(db_error)
    }

    /// Rename a session by updating its metadata title and `updated_at`.
    pub fn rename(&self, id: Uuid, new_title: &str) -> Result<()> {
        let conn = self.open_conn()?;

        // Read current metadata
        let metadata_str: String = conn
            .query_row(
                "SELECT metadata FROM sessions WHERE id = ?1",
                params![id.to_string()],
                |row| row.get(0),
            )
            .optional()
            .map_err(db_error)?
            .ok_or_else(|| AvaError::NotFound(format!("session {id} not found")))?;

        let mut metadata: serde_json::Map<String, Value> = serde_json::from_str(&metadata_str)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        metadata.insert("title".to_string(), Value::String(new_title.to_string()));

        let updated_at = Utc::now().to_rfc3339();
        conn.execute(
            "UPDATE sessions SET metadata = ?1, updated_at = ?2 WHERE id = ?3",
            params![
                serde_json::to_string(&metadata)
                    .map_err(|e| AvaError::SerializationError(e.to_string()))?,
                updated_at,
                id.to_string(),
            ],
        )
        .map_err(db_error)?;

        Ok(())
    }

    /// Save new messages incrementally without deleting existing messages on other branches.
    /// Only inserts/updates the given messages and updates session metadata.
    pub fn save_incremental(
        &self,
        session_id: Uuid,
        new_messages: &[Message],
        token_usage: &ava_types::TokenUsage,
        branch_head: Option<Uuid>,
    ) -> Result<()> {
        let mut conn = self.open_conn()?;
        let tx = conn.transaction().map_err(db_error)?;

        // Upsert each message
        for message in new_messages {
            Self::upsert_message_in_tx(&tx, session_id, message)?;
        }

        // Update session metadata
        tx.execute(
            "UPDATE sessions SET updated_at = ?1, token_usage = ?2, branch_head = ?3 WHERE id = ?4",
            params![
                Utc::now().to_rfc3339(),
                serde_json::to_string(token_usage)
                    .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                branch_head.map(|id| id.to_string()),
                session_id.to_string(),
            ],
        )
        .map_err(db_error)?;

        tx.commit().map_err(db_error)
    }

    // ── Bookmark CRUD (BG-13) ───────────────────────────────────────

    /// Add a bookmark at the given message index with a label.
    pub fn add_bookmark(
        &self,
        session_id: Uuid,
        label: &str,
        message_index: usize,
    ) -> Result<Bookmark> {
        let conn = self.open_conn()?;
        let bookmark = Bookmark {
            id: Uuid::new_v4(),
            session_id,
            label: label.to_string(),
            message_index,
            created_at: Utc::now(),
        };
        conn.execute(
            "INSERT INTO bookmarks (id, session_id, label, message_index, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5)",
            params![
                bookmark.id.to_string(),
                bookmark.session_id.to_string(),
                bookmark.label,
                bookmark.message_index as i64,
                bookmark.created_at.to_rfc3339(),
            ],
        )
        .map_err(db_error)?;
        Ok(bookmark)
    }

    /// List all bookmarks for a session, ordered by message index.
    pub fn list_bookmarks(&self, session_id: Uuid) -> Result<Vec<Bookmark>> {
        let conn = self.open_conn()?;
        let mut stmt = conn
            .prepare(
                "SELECT id, session_id, label, message_index, created_at
                 FROM bookmarks WHERE session_id = ?1
                 ORDER BY message_index ASC",
            )
            .map_err(db_error)?;

        let bookmarks = stmt
            .query_map(params![session_id.to_string()], |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    row.get::<_, String>(1)?,
                    row.get::<_, String>(2)?,
                    row.get::<_, i64>(3)?,
                    row.get::<_, String>(4)?,
                ))
            })
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        bookmarks
            .into_iter()
            .map(|(id, sid, label, idx, created)| {
                Ok(Bookmark {
                    id: parse_uuid(&id)?,
                    session_id: parse_uuid(&sid)?,
                    label,
                    message_index: idx as usize,
                    created_at: parse_datetime(&created)?,
                })
            })
            .collect()
    }

    /// Remove a bookmark by its ID.
    pub fn remove_bookmark(&self, bookmark_id: Uuid) -> Result<()> {
        let conn = self.open_conn()?;
        let deleted = conn
            .execute(
                "DELETE FROM bookmarks WHERE id = ?1",
                params![bookmark_id.to_string()],
            )
            .map_err(db_error)?;
        if deleted == 0 {
            return Err(AvaError::NotFound(format!(
                "bookmark {bookmark_id} not found"
            )));
        }
        Ok(())
    }

    /// Remove all bookmarks for a session.
    pub fn clear_bookmarks(&self, session_id: Uuid) -> Result<usize> {
        let conn = self.open_conn()?;
        let deleted = conn
            .execute(
                "DELETE FROM bookmarks WHERE session_id = ?1",
                params![session_id.to_string()],
            )
            .map_err(db_error)?;
        Ok(deleted)
    }

    pub(crate) fn get_with_conn(&self, conn: &Connection, id: Uuid) -> Result<Option<Session>> {
        let row = conn
            .query_row(
                "SELECT id, created_at, updated_at, metadata, token_usage, branch_head FROM sessions WHERE id = ?1",
                params![id.to_string()],
                |row| {
                    Ok((
                        row.get::<_, String>(0)?,
                        row.get::<_, String>(1)?,
                        row.get::<_, String>(2)?,
                        row.get::<_, String>(3)?,
                        row.get::<_, Option<String>>(4)?,
                        row.get::<_, Option<String>>(5)?,
                    ))
                },
            )
            .optional()
            .map_err(db_error)?;

        let Some((session_id, created_at, updated_at, metadata, token_usage_json, branch_head_str)) =
            row
        else {
            return Ok(None);
        };

        let mut messages_stmt = conn
            .prepare(
                "SELECT id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id, agent_visible, user_visible, original_content, structured_content, metadata
                 FROM messages WHERE session_id = ?1 ORDER BY timestamp ASC, id ASC",
            )
            .map_err(db_error)?;

        let messages = messages_stmt
            .query_map(params![session_id.clone()], row_to_message)
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let token_usage = token_usage_json
            .as_deref()
            .and_then(|json| serde_json::from_str::<ava_types::TokenUsage>(json).ok())
            .unwrap_or_default();

        let branch_head = branch_head_str.as_deref().map(parse_uuid).transpose()?;

        Ok(Some(Session {
            id: parse_uuid(&session_id)?,
            created_at: parse_datetime(&created_at)?,
            updated_at: parse_datetime(&updated_at)?,
            messages,
            metadata: serde_json::from_str(&metadata)
                .map_err(|error| AvaError::SerializationError(error.to_string()))?,
            token_usage,
            branch_head,
        }))
    }

    pub(crate) fn init_schema(&self) -> Result<()> {
        let conn = self.open_conn()?;

        let version: i32 = conn
            .query_row("PRAGMA user_version", [], |r| r.get(0))
            .map_err(db_error)?;

        if version < 1 {
            conn.execute_batch(SCHEMA_SQL).map_err(db_error)?;
            conn.execute_batch("PRAGMA user_version = 1;")
                .map_err(db_error)?;
        }

        // Run migrations for existing databases. Re-applying already-landed ALTERs is
        // expected, but anything else should fail loudly so the app does not continue
        // on a partially migrated schema.
        for sql in MIGRATION_SQL {
            if let Err(error) = conn.execute_batch(sql) {
                if should_ignore_migration_error(&error) {
                    tracing::debug!(migration = %sql, error = %error, "ignoring already-applied session migration");
                    continue;
                }

                return Err(db_error(error));
            }
        }

        Ok(())
    }

    /// Open a brand-new SQLite connection with WAL mode and foreign keys enabled.
    pub(crate) fn open_new_conn(db_path: &std::path::Path) -> Result<Connection> {
        let conn = Connection::open(db_path).map_err(db_error)?;
        conn.execute_batch(
            "PRAGMA journal_mode = WAL;
             PRAGMA synchronous = NORMAL;
             PRAGMA foreign_keys = ON;
             PRAGMA busy_timeout = 5000;
             PRAGMA cache_size = -64000;",
        )
        .map_err(db_error)?;
        Ok(conn)
    }

    /// Acquire the persistent connection.
    ///
    /// Callers hold the `MutexGuard` for the duration of their DB operation and then
    /// drop it, returning the connection to the pool.
    pub(crate) fn open_conn(&self) -> Result<std::sync::MutexGuard<'_, Connection>> {
        Ok(self.conn.lock().unwrap_or_else(|e| e.into_inner()))
    }
}

fn should_ignore_migration_error(error: &rusqlite::Error) -> bool {
    let message = error.to_string().to_ascii_lowercase();
    message.contains("duplicate column name")
        || message.contains("already exists")
        || message.contains("duplicate key name")
}

#[cfg(test)]
mod bookmark_tests {
    use super::should_ignore_migration_error;
    use crate::*;
    use rusqlite::Error as SqlError;
    use uuid::Uuid;

    fn temp_manager() -> (SessionManager, tempfile::TempDir) {
        let dir = tempfile::tempdir().unwrap();
        let mgr = SessionManager::new(dir.path().join("test.db")).unwrap();
        (mgr, dir)
    }

    #[test]
    fn add_and_list_bookmarks() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let b1 = mgr.add_bookmark(session.id, "start", 0).unwrap();
        let b2 = mgr.add_bookmark(session.id, "midpoint", 5).unwrap();

        let bookmarks = mgr.list_bookmarks(session.id).unwrap();
        assert_eq!(bookmarks.len(), 2);
        assert_eq!(bookmarks[0].id, b1.id);
        assert_eq!(bookmarks[0].label, "start");
        assert_eq!(bookmarks[0].message_index, 0);
        assert_eq!(bookmarks[1].id, b2.id);
        assert_eq!(bookmarks[1].label, "midpoint");
        assert_eq!(bookmarks[1].message_index, 5);
    }

    #[test]
    fn remove_bookmark() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let b = mgr.add_bookmark(session.id, "mark", 3).unwrap();
        mgr.remove_bookmark(b.id).unwrap();

        let bookmarks = mgr.list_bookmarks(session.id).unwrap();
        assert!(bookmarks.is_empty());
    }

    #[test]
    fn remove_nonexistent_bookmark_returns_not_found() {
        let (mgr, _dir) = temp_manager();
        let result = mgr.remove_bookmark(Uuid::new_v4());
        assert!(result.is_err());
    }

    #[test]
    fn clear_bookmarks() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        mgr.add_bookmark(session.id, "a", 0).unwrap();
        mgr.add_bookmark(session.id, "b", 1).unwrap();
        mgr.add_bookmark(session.id, "c", 2).unwrap();

        let count = mgr.clear_bookmarks(session.id).unwrap();
        assert_eq!(count, 3);
        assert!(mgr.list_bookmarks(session.id).unwrap().is_empty());
    }

    #[test]
    fn delete_session_removes_bookmarks() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        mgr.add_bookmark(session.id, "mark", 0).unwrap();
        mgr.delete(session.id).unwrap();

        // Bookmarks should be gone (table still exists, just no rows for this session)
        let bookmarks = mgr.list_bookmarks(session.id).unwrap();
        assert!(bookmarks.is_empty());
    }

    #[test]
    fn list_empty_bookmarks() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let bookmarks = mgr.list_bookmarks(session.id).unwrap();
        assert!(bookmarks.is_empty());
    }

    #[test]
    fn duplicate_column_migration_errors_are_ignored() {
        let error = SqlError::SqliteFailure(
            rusqlite::ffi::Error {
                code: rusqlite::ErrorCode::Unknown,
                extended_code: 1,
            },
            Some("duplicate column name: tool_call_id".to_string()),
        );

        assert!(should_ignore_migration_error(&error));
    }

    #[test]
    fn unexpected_migration_errors_are_not_ignored() {
        let error = SqlError::SqliteFailure(
            rusqlite::ffi::Error {
                code: rusqlite::ErrorCode::ReadOnly,
                extended_code: 8,
            },
            Some("attempt to write a readonly database".to_string()),
        );

        assert!(!should_ignore_migration_error(&error));
    }
}

#[cfg(test)]
mod incremental_tests {
    use crate::*;
    use ava_types::{Message, Role, TokenUsage};
    use uuid::Uuid;

    fn temp_manager() -> (SessionManager, tempfile::TempDir) {
        let dir = tempfile::tempdir().unwrap();
        let mgr = SessionManager::new(dir.path().join("test.db")).unwrap();
        (mgr, dir)
    }

    #[test]
    fn add_message_persists_single_message() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let msg = Message::new(Role::User, "incremental hello");
        mgr.add_message(session.id, &msg).unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 1);
        assert_eq!(loaded.messages[0].content, "incremental hello");
        assert_eq!(loaded.messages[0].id, msg.id);
    }

    #[test]
    fn add_message_upserts_existing_message() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let mut msg = Message::new(Role::Assistant, "draft");
        let msg_id = msg.id;
        mgr.add_message(session.id, &msg).unwrap();

        // Update the content and re-add
        msg.content = "final answer".to_string();
        mgr.add_message(session.id, &msg).unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 1);
        assert_eq!(loaded.messages[0].id, msg_id);
        assert_eq!(loaded.messages[0].content, "final answer");
    }

    #[test]
    fn add_messages_batch() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let msgs = vec![
            Message::new(Role::User, "first"),
            Message::new(Role::Assistant, "second"),
            Message::new(Role::User, "third"),
        ];
        mgr.add_messages(session.id, &msgs).unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 3);
    }

    #[test]
    fn add_messages_empty_is_noop() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        mgr.add_messages(session.id, &[]).unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert!(loaded.messages.is_empty());
    }

    #[test]
    fn update_message_content_changes_only_content() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let msg = Message::new(Role::Assistant, "original");
        let msg_id = msg.id;
        mgr.add_message(session.id, &msg).unwrap();

        mgr.update_message_content(session.id, msg_id, "updated")
            .unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 1);
        assert_eq!(loaded.messages[0].content, "updated");
        assert_eq!(loaded.messages[0].role, Role::Assistant);
    }

    #[test]
    fn update_message_content_not_found() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let err = mgr
            .update_message_content(session.id, Uuid::new_v4(), "nope")
            .unwrap_err();
        assert!(err.to_string().contains("not found"));
    }

    #[test]
    fn save_incremental_survives_manager_restart() {
        let dir = tempfile::tempdir().unwrap();
        let db_path = dir.path().join("test.db");
        let mgr = SessionManager::new(&db_path).unwrap();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let message = Message::new(Role::Assistant, "checkpointed after restart");
        let message_id = message.id;
        mgr.save_incremental(session.id, &[message], &TokenUsage::default(), None)
            .unwrap();
        drop(mgr);

        let reopened = SessionManager::new(&db_path).unwrap();
        let loaded = reopened.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 1);
        assert_eq!(loaded.messages[0].id, message_id);
        assert_eq!(loaded.messages[0].content, "checkpointed after restart");
    }

    #[test]
    fn save_removes_orphaned_messages() {
        let (mgr, _dir) = temp_manager();
        let mut session = mgr.create().unwrap();
        session.add_message(Message::new(Role::User, "keep me"));
        session.add_message(Message::new(Role::Assistant, "remove me"));
        mgr.save(&session).unwrap();

        // Remove the second message from the session and re-save
        session.messages.pop();
        mgr.save(&session).unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 1);
        assert_eq!(loaded.messages[0].content, "keep me");
    }

    #[test]
    fn save_upsert_preserves_existing_on_crash_simulation() {
        // Simulates the key safety property: if we save with INSERT OR REPLACE
        // and the session has messages, existing messages are preserved.
        let (mgr, _dir) = temp_manager();
        let mut session = mgr.create().unwrap();
        let m1 = Message::new(Role::User, "first");
        let m1_id = m1.id;
        session.add_message(m1);
        mgr.save(&session).unwrap();

        // Add a second message incrementally (simulating checkpoint)
        let m2 = Message::new(Role::Assistant, "second");
        let m2_id = m2.id;
        mgr.add_message(session.id, &m2).unwrap();

        // Verify both messages exist
        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 2);
        assert_eq!(loaded.messages[0].id, m1_id);
        assert_eq!(loaded.messages[1].id, m2_id);
    }

    #[test]
    fn save_incremental_updates_tool_call_id() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let mut tool_message = Message::new(Role::Tool, "tool output").with_tool_call_id("call-1");
        let message_id = tool_message.id;

        mgr.save_incremental(
            session.id,
            &[tool_message.clone()],
            &TokenUsage::default(),
            None,
        )
        .unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages[0].tool_call_id.as_deref(), Some("call-1"));

        tool_message.tool_call_id = Some("call-2".to_string());
        mgr.save_incremental(session.id, &[tool_message], &TokenUsage::default(), None)
            .unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.messages.len(), 1);
        assert_eq!(loaded.messages[0].id, message_id);
        assert_eq!(loaded.messages[0].tool_call_id.as_deref(), Some("call-2"));
    }
}
