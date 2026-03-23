//! AVA Session — session persistence with SQLite storage.
//!
//! This crate provides:
//! - Session creation, saving, and loading
//! - Message history management
//! - Full-text search over sessions

pub mod diff_tracking;
mod helpers;

use std::path::{Path, PathBuf};

use ava_types::{AvaError, Message, Result, Session};
use chrono::Utc;
use rusqlite::{params, Connection, OptionalExtension};
use serde_json::Value;
use uuid::Uuid;

use crate::helpers::{
    db_error, parse_datetime, parse_uuid, role_to_str, str_to_role, to_conversion_error,
    MIGRATION_SQL, SCHEMA_SQL,
};

/// A labeled bookmark at a specific message index within a session.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct Bookmark {
    pub id: Uuid,
    pub session_id: Uuid,
    pub label: String,
    pub message_index: usize,
    pub created_at: chrono::DateTime<Utc>,
}

/// A node in the conversation tree (BG-10).
#[derive(Debug, Clone)]
pub struct TreeNode {
    pub message: Message,
    pub children: Vec<Uuid>,
}

/// Full conversation tree for a session (BG-10).
#[derive(Debug, Clone)]
pub struct ConversationTree {
    /// Root message ID (first message in the session).
    pub root: Option<Uuid>,
    /// All nodes keyed by message ID.
    pub nodes: std::collections::HashMap<Uuid, TreeNode>,
    /// Currently active branch head.
    pub branch_head: Option<Uuid>,
}

/// Summary of a branch leaf for selection UI (BG-10).
#[derive(Debug, Clone)]
pub struct BranchLeaf {
    pub leaf_id: Uuid,
    pub preview: String,
    pub depth: usize,
    pub role: ava_types::Role,
    pub timestamp: chrono::DateTime<Utc>,
    pub is_active: bool,
}

pub struct SessionManager {
    db_path: PathBuf,
    /// Persistent connection reused across all method calls.
    ///
    /// Eliminates per-call `Connection::open()` + PRAGMA setup overhead (~0.5-2 ms each).
    /// The `Mutex` ensures single-writer access since `rusqlite::Connection` is not `Send`.
    conn: std::sync::Mutex<Connection>,
}

/// Generate a short descriptive title from a user message.
///
/// Rules:
/// - If the message starts with a `/` command, use the command name (e.g. "/help" -> "/help")
/// - Take the first line of the message
/// - Truncate to ~50 chars at a word boundary
/// - Strip leading/trailing whitespace
///
/// This is a simple heuristic approach. It can be swapped for an LLM-based
/// titler later by replacing this function's body.
pub fn generate_title(first_message: &str) -> String {
    let trimmed = first_message.trim();
    if trimmed.is_empty() {
        return "Untitled session".to_string();
    }

    // If it starts with a slash command, use the command name + rest truncated
    if trimmed.starts_with('/') {
        let first_line = trimmed.lines().next().unwrap_or(trimmed);
        return truncate_at_word_boundary(first_line, 50);
    }

    let first_line = trimmed.lines().next().unwrap_or(trimmed);
    truncate_at_word_boundary(first_line, 50)
}

/// Truncate a string at a word boundary, appending "..." if truncated.
fn truncate_at_word_boundary(s: &str, max_len: usize) -> String {
    let s = s.trim();
    if s.len() <= max_len {
        return s.to_string();
    }

    // Find the last space before the limit
    let truncated = &s[..max_len];
    if let Some(last_space) = truncated.rfind(' ') {
        // Don't truncate to less than half the max length
        if last_space > max_len / 2 {
            return format!("{}...", &s[..last_space]);
        }
    }
    // No good word boundary found, hard truncate
    format!("{}...", truncated.trim_end())
}

impl SessionManager {
    pub fn new(path: impl AsRef<Path>) -> Result<Self> {
        let db_path = path.as_ref().to_path_buf();
        let conn = Self::open_new_conn(&db_path)?;
        let manager = Self {
            db_path,
            conn: std::sync::Mutex::new(conn),
        };
        manager.init_schema()?;
        Ok(manager)
    }

    pub fn create(&self) -> Result<Session> {
        let session = Session::new();
        tracing::info!("Session created: {}", session.id);
        Ok(session)
    }

    pub fn db_path(&self) -> &Path {
        &self.db_path
    }

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

        // Remove orphaned messages that are no longer in the session's message
        // list.  This replaces the old DELETE-all + INSERT-all pattern with a
        // targeted cleanup that only removes messages whose IDs are absent from
        // the current set.
        if !session.messages.is_empty() {
            // Build a comma-separated list of quoted message IDs for the NOT IN clause.
            let id_list: Vec<String> = session.messages.iter().map(|m| m.id.to_string()).collect();
            let placeholders: String = id_list.iter().map(|_| "?").collect::<Vec<_>>().join(",");
            let sql = format!(
                "DELETE FROM messages WHERE session_id = ?1 AND id NOT IN ({placeholders})"
            );
            let mut stmt = tx.prepare(&sql).map_err(db_error)?;
            let mut param_values: Vec<Box<dyn rusqlite::types::ToSql>> =
                vec![Box::new(session.id.to_string())];
            for id in &id_list {
                param_values.push(Box::new(id.clone()));
            }
            let params_ref: Vec<&dyn rusqlite::types::ToSql> =
                param_values.iter().map(|p| p.as_ref()).collect();
            stmt.execute(params_ref.as_slice()).map_err(db_error)?;
        } else {
            // Session has no messages — remove all messages for this session.
            tx.execute(
                "DELETE FROM messages WHERE session_id = ?1",
                params![session.id.to_string()],
            )
            .map_err(db_error)?;
        }

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
        let conn = self.open_conn()?;
        let updated = conn
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

        conn.execute(
            "UPDATE sessions SET updated_at = ?1 WHERE id = ?2",
            params![Utc::now().to_rfc3339(), session_id.to_string()],
        )
        .map_err(db_error)?;

        Ok(())
    }

    /// Upsert a single message row within an existing transaction.
    fn upsert_message_in_tx(
        tx: &rusqlite::Transaction<'_>,
        session_id: Uuid,
        message: &Message,
    ) -> Result<()> {
        tx.execute(
            "INSERT INTO messages (id, session_id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
             ON CONFLICT(id) DO UPDATE SET
               content = excluded.content,
               tool_calls = excluded.tool_calls,
               tool_results = excluded.tool_results,
               tool_call_id = excluded.tool_call_id,
               images = excluded.images,
               parent_id = excluded.parent_id",
            params![
                message.id.to_string(),
                session_id.to_string(),
                role_to_str(&message.role),
                message.content,
                message.timestamp.to_rfc3339(),
                serde_json::to_string(&message.tool_calls)
                    .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                serde_json::to_string(&message.tool_results)
                    .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                message.tool_call_id.as_deref(),
                serde_json::to_string(&message.images)
                    .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                message.parent_id.map(|id| id.to_string()),
            ],
        )
        .map_err(db_error)?;
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

    pub fn search(&self, query: &str) -> Result<Vec<Session>> {
        let conn = self.open_conn()?;
        let mut stmt = conn
            .prepare(
                "SELECT DISTINCT m.session_id
                 FROM messages_fts f
                 JOIN messages m ON m.rowid = f.rowid
                 WHERE messages_fts MATCH ?1
                 ORDER BY m.timestamp DESC",
            )
            .map_err(db_error)?;

        let ids = stmt
            .query_map(params![query], |row| row.get::<_, String>(0))
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

    // ── Conversation Tree (BG-10) ──────────────────────────────────

    /// Get a linear branch by walking parent_id links from `leaf_id` back to root.
    /// Returns messages in chronological order (root first).
    pub fn get_branch(&self, session_id: Uuid, leaf_id: Uuid) -> Result<Vec<Message>> {
        let conn = self.open_conn()?;
        // Load all messages for this session into a HashMap
        let all_messages = self.load_all_messages(&conn, session_id)?;

        let mut branch = Vec::new();
        let mut current_id = Some(leaf_id);

        while let Some(id) = current_id {
            let msg = all_messages
                .get(&id)
                .ok_or_else(|| AvaError::NotFound(format!("message {id} not found in session")))?;
            branch.push(msg.clone());
            current_id = msg.parent_id;
        }

        branch.reverse(); // root first
        Ok(branch)
    }

    /// Get the full conversation tree for a session.
    pub fn get_tree(&self, session_id: Uuid) -> Result<ConversationTree> {
        let conn = self.open_conn()?;
        let all_messages = self.load_all_messages(&conn, session_id)?;

        let mut nodes: std::collections::HashMap<Uuid, TreeNode> = std::collections::HashMap::new();
        let mut roots = Vec::new();

        // Build tree nodes
        for (id, msg) in &all_messages {
            nodes.insert(
                *id,
                TreeNode {
                    message: msg.clone(),
                    children: Vec::new(),
                },
            );
        }

        // Wire up children
        for msg in all_messages.values() {
            if let Some(pid) = msg.parent_id {
                if let Some(parent_node) = nodes.get_mut(&pid) {
                    parent_node.children.push(msg.id);
                }
            } else {
                roots.push(msg.id);
            }
        }

        // Sort children by timestamp for deterministic ordering
        for node in nodes.values_mut() {
            node.children.sort_by(|a, b| {
                let ta = all_messages.get(a).map(|m| m.timestamp);
                let tb = all_messages.get(b).map(|m| m.timestamp);
                ta.cmp(&tb)
            });
        }

        // Pick the first root (there should be exactly one for well-formed sessions)
        let root = roots.into_iter().next();

        // Read branch_head from session
        let branch_head: Option<String> = conn
            .query_row(
                "SELECT branch_head FROM sessions WHERE id = ?1",
                params![session_id.to_string()],
                |row| row.get(0),
            )
            .optional()
            .map_err(db_error)?
            .flatten();

        let branch_head = branch_head.as_deref().map(parse_uuid).transpose()?;

        Ok(ConversationTree {
            root,
            nodes,
            branch_head,
        })
    }

    /// Get all leaf messages (messages with no children) for branch selection.
    pub fn get_branch_leaves(&self, session_id: Uuid) -> Result<Vec<BranchLeaf>> {
        let tree = self.get_tree(session_id)?;
        let mut leaves = Vec::new();

        for (id, node) in &tree.nodes {
            if node.children.is_empty() {
                // Count depth (branch length)
                let mut depth = 0;
                let mut cur = Some(*id);
                while let Some(cid) = cur {
                    depth += 1;
                    cur = tree.nodes.get(&cid).and_then(|n| n.message.parent_id);
                }

                let preview = if node.message.content.len() > 80 {
                    format!("{}...", &node.message.content[..77])
                } else {
                    node.message.content.clone()
                };

                leaves.push(BranchLeaf {
                    leaf_id: *id,
                    preview,
                    depth,
                    role: node.message.role.clone(),
                    timestamp: node.message.timestamp,
                    is_active: tree.branch_head == Some(*id),
                });
            }
        }

        leaves.sort_by(|a, b| a.timestamp.cmp(&b.timestamp));
        Ok(leaves)
    }

    /// Create a new branch from a specific message in the conversation.
    /// The new user message becomes a child of `branch_point_id`.
    pub fn branch_from(
        &self,
        session_id: Uuid,
        branch_point_id: Uuid,
        new_user_message: &str,
    ) -> Result<Message> {
        let mut conn = self.open_conn()?;

        let msg =
            Message::new(ava_types::Role::User, new_user_message).with_parent(branch_point_id);

        let tx = conn.transaction().map_err(db_error)?;

        tx.execute(
            "INSERT INTO messages (id, session_id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            params![
                msg.id.to_string(),
                session_id.to_string(),
                role_to_str(&msg.role),
                msg.content,
                msg.timestamp.to_rfc3339(),
                "[]",
                "[]",
                Option::<String>::None,
                "[]",
                msg.parent_id.map(|id| id.to_string()),
            ],
        )
        .map_err(db_error)?;

        // Update branch_head to the new message
        tx.execute(
            "UPDATE sessions SET branch_head = ?1, updated_at = ?2 WHERE id = ?3",
            params![
                msg.id.to_string(),
                Utc::now().to_rfc3339(),
                session_id.to_string(),
            ],
        )
        .map_err(db_error)?;

        tx.commit().map_err(db_error)?;

        Ok(msg)
    }

    /// Switch the active branch to a different leaf.
    pub fn switch_branch(&self, session_id: Uuid, leaf_id: Uuid) -> Result<()> {
        let conn = self.open_conn()?;
        let updated = conn
            .execute(
                "UPDATE sessions SET branch_head = ?1, updated_at = ?2 WHERE id = ?3",
                params![
                    leaf_id.to_string(),
                    Utc::now().to_rfc3339(),
                    session_id.to_string(),
                ],
            )
            .map_err(db_error)?;

        if updated == 0 {
            return Err(AvaError::NotFound(format!(
                "session {session_id} not found"
            )));
        }
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
            tx.execute(
                "INSERT INTO messages (id, session_id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
                 ON CONFLICT(id) DO UPDATE SET
                   content = excluded.content,
                   tool_calls = excluded.tool_calls,
                   tool_results = excluded.tool_results,
                   images = excluded.images,
                   parent_id = excluded.parent_id",
                params![
                    message.id.to_string(),
                    session_id.to_string(),
                    role_to_str(&message.role),
                    message.content,
                    message.timestamp.to_rfc3339(),
                    serde_json::to_string(&message.tool_calls)
                        .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                    serde_json::to_string(&message.tool_results)
                        .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                    message.tool_call_id.as_deref(),
                    serde_json::to_string(&message.images)
                        .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                    message.parent_id.map(|id| id.to_string()),
                ],
            )
            .map_err(db_error)?;
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

    /// Backfill parent_id for existing linear sessions.
    /// Chains messages by timestamp order within each session.
    pub fn backfill_parent_ids(&self) -> Result<usize> {
        let conn = self.open_conn()?;

        // Find sessions with messages that have NULL parent_id
        let mut stmt = conn
            .prepare("SELECT DISTINCT session_id FROM messages WHERE parent_id IS NULL")
            .map_err(db_error)?;

        let session_ids: Vec<String> = stmt
            .query_map([], |row| row.get::<_, String>(0))
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let mut total_updated = 0;

        for sid in &session_ids {
            // Get messages ordered by timestamp
            let mut msg_stmt = conn
                .prepare(
                    "SELECT id FROM messages WHERE session_id = ?1 ORDER BY timestamp ASC, id ASC",
                )
                .map_err(db_error)?;

            let ids: Vec<String> = msg_stmt
                .query_map(params![sid], |row| row.get::<_, String>(0))
                .map_err(db_error)?
                .collect::<std::result::Result<Vec<_>, _>>()
                .map_err(db_error)?;

            // Chain: message[i].parent_id = message[i-1].id
            for i in 1..ids.len() {
                conn.execute(
                    "UPDATE messages SET parent_id = ?1 WHERE id = ?2",
                    params![ids[i - 1], ids[i]],
                )
                .map_err(db_error)?;
                total_updated += 1;
            }
        }

        Ok(total_updated)
    }

    /// Load all messages for a session into a HashMap keyed by message ID.
    fn load_all_messages(
        &self,
        conn: &Connection,
        session_id: Uuid,
    ) -> Result<std::collections::HashMap<Uuid, Message>> {
        let mut stmt = conn
            .prepare(
                "SELECT id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id
                 FROM messages WHERE session_id = ?1",
            )
            .map_err(db_error)?;

        let messages = stmt
            .query_map(params![session_id.to_string()], |row| {
                let tool_calls =
                    serde_json::from_str::<Vec<ava_types::ToolCall>>(&row.get::<_, String>(4)?)
                        .map_err(|error| {
                            rusqlite::Error::FromSqlConversionFailure(
                                4,
                                rusqlite::types::Type::Text,
                                Box::new(error),
                            )
                        })?;
                let tool_results =
                    serde_json::from_str::<Vec<ava_types::ToolResult>>(&row.get::<_, String>(5)?)
                        .map_err(|error| {
                        rusqlite::Error::FromSqlConversionFailure(
                            5,
                            rusqlite::types::Type::Text,
                            Box::new(error),
                        )
                    })?;
                let tool_call_id: Option<String> = row.get(6)?;
                let images_json: String = row
                    .get::<_, Option<String>>(7)?
                    .unwrap_or_else(|| "[]".to_string());
                let images = serde_json::from_str::<Vec<ava_types::ImageContent>>(&images_json)
                    .map_err(|error| {
                        rusqlite::Error::FromSqlConversionFailure(
                            7,
                            rusqlite::types::Type::Text,
                            Box::new(error),
                        )
                    })?;
                let parent_id_str: Option<String> = row.get(8)?;
                let parent_id = parent_id_str
                    .as_deref()
                    .map(parse_uuid)
                    .transpose()
                    .map_err(to_conversion_error)?;

                Ok(Message {
                    id: parse_uuid(&row.get::<_, String>(0)?).map_err(to_conversion_error)?,
                    role: str_to_role(&row.get::<_, String>(1)?).map_err(to_conversion_error)?,
                    content: row.get(2)?,
                    timestamp: parse_datetime(&row.get::<_, String>(3)?)
                        .map_err(to_conversion_error)?,
                    tool_calls,
                    tool_results,
                    tool_call_id,
                    images,
                    parent_id,
                    agent_visible: true,
                    user_visible: true,
                    original_content: None,
                })
            })
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let mut map = std::collections::HashMap::new();
        for msg in messages {
            map.insert(msg.id, msg);
        }
        Ok(map)
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

    fn get_with_conn(&self, conn: &Connection, id: Uuid) -> Result<Option<Session>> {
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
                "SELECT id, role, content, timestamp, tool_calls, tool_results, tool_call_id, images, parent_id
                 FROM messages WHERE session_id = ?1 ORDER BY timestamp ASC, id ASC",
            )
            .map_err(db_error)?;

        let messages = messages_stmt
            .query_map(params![session_id.clone()], |row| {
                let tool_calls =
                    serde_json::from_str::<Vec<ava_types::ToolCall>>(&row.get::<_, String>(4)?)
                        .map_err(|error| {
                            rusqlite::Error::FromSqlConversionFailure(
                                4,
                                rusqlite::types::Type::Text,
                                Box::new(error),
                            )
                        })?;
                let tool_results =
                    serde_json::from_str::<Vec<ava_types::ToolResult>>(&row.get::<_, String>(5)?)
                        .map_err(|error| {
                        rusqlite::Error::FromSqlConversionFailure(
                            5,
                            rusqlite::types::Type::Text,
                            Box::new(error),
                        )
                    })?;
                let tool_call_id: Option<String> = row.get(6)?;
                let images_json: String = row
                    .get::<_, Option<String>>(7)?
                    .unwrap_or_else(|| "[]".to_string());
                let images = serde_json::from_str::<Vec<ava_types::ImageContent>>(&images_json)
                    .map_err(|error| {
                        rusqlite::Error::FromSqlConversionFailure(
                            7,
                            rusqlite::types::Type::Text,
                            Box::new(error),
                        )
                    })?;
                let parent_id_str: Option<String> = row.get(8)?;
                let parent_id = parent_id_str
                    .as_deref()
                    .map(parse_uuid)
                    .transpose()
                    .map_err(to_conversion_error)?;

                Ok(Message {
                    id: parse_uuid(&row.get::<_, String>(0)?).map_err(to_conversion_error)?,
                    role: str_to_role(&row.get::<_, String>(1)?).map_err(to_conversion_error)?,
                    content: row.get(2)?,
                    timestamp: parse_datetime(&row.get::<_, String>(3)?)
                        .map_err(to_conversion_error)?,
                    tool_calls,
                    tool_results,
                    tool_call_id,
                    images,
                    parent_id,
                    agent_visible: true,
                    user_visible: true,
                    original_content: None,
                })
            })
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

    fn init_schema(&self) -> Result<()> {
        let conn = self.open_conn()?;

        let version: i32 = conn
            .query_row("PRAGMA user_version", [], |r| r.get(0))
            .map_err(db_error)?;

        if version < 1 {
            conn.execute_batch(SCHEMA_SQL).map_err(db_error)?;
            conn.execute_batch("PRAGMA user_version = 1;")
                .map_err(db_error)?;
        }

        // Run migrations for existing databases (idempotent — ignores "duplicate column" errors)
        for sql in MIGRATION_SQL {
            let _ = conn.execute_batch(sql);
        }

        Ok(())
    }

    /// Open a brand-new SQLite connection with WAL mode and foreign keys enabled.
    fn open_new_conn(db_path: &Path) -> Result<Connection> {
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
    fn open_conn(&self) -> Result<std::sync::MutexGuard<'_, Connection>> {
        Ok(self.conn.lock().unwrap_or_else(|e| e.into_inner()))
    }
}

#[cfg(test)]
mod bookmark_tests {
    use super::*;

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
}

#[cfg(test)]
mod title_tests {
    use super::*;

    #[test]
    fn short_message_kept_as_is() {
        assert_eq!(generate_title("Fix the login bug"), "Fix the login bug");
    }

    #[test]
    fn empty_message_gives_untitled() {
        assert_eq!(generate_title(""), "Untitled session");
        assert_eq!(generate_title("   "), "Untitled session");
    }

    #[test]
    fn multiline_uses_first_line() {
        let msg = "Fix the login bug\nAlso update the tests\nAnd the docs";
        assert_eq!(generate_title(msg), "Fix the login bug");
    }

    #[test]
    fn long_message_truncated_at_word_boundary() {
        let msg = "Implement a comprehensive user authentication system with OAuth2 support and JWT tokens";
        let title = generate_title(msg);
        assert!(title.len() <= 53, "title too long: {} chars", title.len()); // 50 + "..."
        assert!(title.ends_with("..."));
        // Should break at a word boundary
        assert!(!title.contains("  "));
    }

    #[test]
    fn slash_command_preserved() {
        assert_eq!(generate_title("/help"), "/help");
        assert_eq!(generate_title("/model openai/gpt-4"), "/model openai/gpt-4");
    }

    #[test]
    fn whitespace_trimmed() {
        assert_eq!(generate_title("  Hello world  "), "Hello world");
    }

    #[test]
    fn exactly_50_chars_no_truncation() {
        let msg = "a]".repeat(25); // 50 chars
        let title = generate_title(&msg);
        assert_eq!(title, msg);
    }
}

#[cfg(test)]
mod tree_tests {
    use super::*;
    use ava_types::Role;

    fn temp_manager() -> (SessionManager, tempfile::TempDir) {
        let dir = tempfile::tempdir().unwrap();
        let mgr = SessionManager::new(dir.path().join("test.db")).unwrap();
        (mgr, dir)
    }

    /// Build a linear session with parent_id links: msg1 -> msg2 -> msg3.
    fn build_linear_session(mgr: &SessionManager) -> (Session, Vec<Uuid>) {
        let mut session = mgr.create().unwrap();

        let m1 = Message::new(Role::User, "Hello");
        let m1_id = m1.id;
        session.add_message(m1);

        let m2 = Message::new(Role::Assistant, "Hi there!").with_parent(m1_id);
        let m2_id = m2.id;
        session.add_message(m2);

        let m3 = Message::new(Role::User, "How are you?").with_parent(m2_id);
        let m3_id = m3.id;
        session.add_message(m3);

        session.branch_head = Some(m3_id);
        mgr.save(&session).unwrap();
        (session, vec![m1_id, m2_id, m3_id])
    }

    #[test]
    fn get_branch_returns_linear_path() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        let branch = mgr.get_branch(session.id, ids[2]).unwrap();
        assert_eq!(branch.len(), 3);
        assert_eq!(branch[0].id, ids[0]); // root first
        assert_eq!(branch[1].id, ids[1]);
        assert_eq!(branch[2].id, ids[2]);
    }

    #[test]
    fn get_branch_partial() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Get branch from midpoint
        let branch = mgr.get_branch(session.id, ids[1]).unwrap();
        assert_eq!(branch.len(), 2);
        assert_eq!(branch[0].id, ids[0]);
        assert_eq!(branch[1].id, ids[1]);
    }

    #[test]
    fn get_tree_structure() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        let tree = mgr.get_tree(session.id).unwrap();
        assert_eq!(tree.root, Some(ids[0]));
        assert_eq!(tree.nodes.len(), 3);
        assert_eq!(tree.branch_head, Some(ids[2]));

        // Check parent-child relationships
        let root_node = tree.nodes.get(&ids[0]).unwrap();
        assert_eq!(root_node.children, vec![ids[1]]);

        let mid_node = tree.nodes.get(&ids[1]).unwrap();
        assert_eq!(mid_node.children, vec![ids[2]]);

        let leaf_node = tree.nodes.get(&ids[2]).unwrap();
        assert!(leaf_node.children.is_empty());
    }

    #[test]
    fn branch_from_creates_fork() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork from the assistant message (ids[1])
        let new_msg = mgr
            .branch_from(session.id, ids[1], "Actually, different question")
            .unwrap();

        assert_eq!(new_msg.parent_id, Some(ids[1]));
        assert_eq!(new_msg.role, Role::User);

        // Tree should now have 4 nodes
        let tree = mgr.get_tree(session.id).unwrap();
        assert_eq!(tree.nodes.len(), 4);

        // ids[1] should have two children: ids[2] and new_msg.id
        let mid_node = tree.nodes.get(&ids[1]).unwrap();
        assert_eq!(mid_node.children.len(), 2);
        assert!(mid_node.children.contains(&ids[2]));
        assert!(mid_node.children.contains(&new_msg.id));

        // Branch head should be updated to new message
        assert_eq!(tree.branch_head, Some(new_msg.id));
    }

    #[test]
    fn get_branch_leaves_finds_all_leaves() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork to create two branches
        let fork_msg = mgr.branch_from(session.id, ids[1], "Branch B").unwrap();

        let leaves = mgr.get_branch_leaves(session.id).unwrap();
        assert_eq!(leaves.len(), 2);

        let leaf_ids: Vec<Uuid> = leaves.iter().map(|l| l.leaf_id).collect();
        assert!(leaf_ids.contains(&ids[2])); // original branch leaf
        assert!(leaf_ids.contains(&fork_msg.id)); // new branch leaf

        // The new branch should be marked active
        let active = leaves.iter().find(|l| l.is_active).unwrap();
        assert_eq!(active.leaf_id, fork_msg.id);
    }

    #[test]
    fn switch_branch_updates_head() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork
        let _fork = mgr.branch_from(session.id, ids[1], "Branch B").unwrap();

        // Switch back to original branch
        mgr.switch_branch(session.id, ids[2]).unwrap();

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.branch_head, Some(ids[2]));
    }

    #[test]
    fn save_incremental_preserves_other_branches() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        // Fork
        let fork_msg = mgr.branch_from(session.id, ids[1], "Branch B").unwrap();

        // Add new messages incrementally to the fork branch
        let m4 = Message::new(Role::Assistant, "Branch B reply").with_parent(fork_msg.id);
        let m4_id = m4.id;

        mgr.save_incremental(
            session.id,
            &[m4],
            &ava_types::TokenUsage::default(),
            Some(m4_id),
        )
        .unwrap();

        // All 5 messages should exist
        let tree = mgr.get_tree(session.id).unwrap();
        assert_eq!(tree.nodes.len(), 5);
        assert_eq!(tree.branch_head, Some(m4_id));

        // Original branch still intact
        let branch_a = mgr.get_branch(session.id, ids[2]).unwrap();
        assert_eq!(branch_a.len(), 3);

        // New branch
        let branch_b = mgr.get_branch(session.id, m4_id).unwrap();
        assert_eq!(branch_b.len(), 4); // root + assistant + fork_user + fork_reply
    }

    #[test]
    fn backfill_parent_ids_chains_linear_messages() {
        let (mgr, _dir) = temp_manager();
        let mut session = mgr.create().unwrap();

        // Add messages WITHOUT parent_id (simulating legacy data)
        let m1 = Message::new(Role::User, "First");
        let m1_id = m1.id;
        session.add_message(m1);

        let m2 = Message::new(Role::Assistant, "Second");
        let m2_id = m2.id;
        session.add_message(m2);

        let m3 = Message::new(Role::User, "Third");
        let m3_id = m3.id;
        session.add_message(m3);

        mgr.save(&session).unwrap();

        // Verify parent_ids are NULL
        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert!(loaded.messages[0].parent_id.is_none());
        assert!(loaded.messages[1].parent_id.is_none());
        assert!(loaded.messages[2].parent_id.is_none());

        // Backfill
        let count = mgr.backfill_parent_ids().unwrap();
        assert_eq!(count, 2); // 2 messages get parent_id set

        // Verify chain
        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert!(loaded.messages[0].parent_id.is_none()); // root
        assert_eq!(loaded.messages[1].parent_id, Some(m1_id));
        assert_eq!(loaded.messages[2].parent_id, Some(m2_id));
    }

    #[test]
    fn branch_head_persists_through_save_load() {
        let (mgr, _dir) = temp_manager();
        let (session, ids) = build_linear_session(&mgr);

        let loaded = mgr.get(session.id).unwrap().unwrap();
        assert_eq!(loaded.branch_head, Some(ids[2]));
    }

    #[test]
    fn empty_session_tree() {
        let (mgr, _dir) = temp_manager();
        let session = mgr.create().unwrap();
        mgr.save(&session).unwrap();

        let tree = mgr.get_tree(session.id).unwrap();
        assert!(tree.root.is_none());
        assert!(tree.nodes.is_empty());
        assert!(tree.branch_head.is_none());

        let leaves = mgr.get_branch_leaves(session.id).unwrap();
        assert!(leaves.is_empty());
    }
}

#[cfg(test)]
mod incremental_tests {
    use super::*;
    use ava_types::Role;

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
}
