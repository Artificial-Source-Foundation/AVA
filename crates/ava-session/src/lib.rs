mod helpers;

use std::path::{Path, PathBuf};

use ava_types::{AvaError, Message, Result, Session};
use rusqlite::{params, Connection, OptionalExtension};
use serde_json::Value;
use uuid::Uuid;

use crate::helpers::{
    db_error, parse_datetime, parse_uuid, role_to_str, str_to_role, to_conversion_error, SCHEMA_SQL,
};

pub struct SessionManager {
    db_path: PathBuf,
}

pub fn healthcheck() -> bool {
    true
}

impl SessionManager {
    pub fn new(path: impl AsRef<Path>) -> Result<Self> {
        let manager = Self {
            db_path: path.as_ref().to_path_buf(),
        };
        manager.init_schema()?;
        Ok(manager)
    }

    pub fn create(&self) -> Result<Session> {
        Ok(Session::new())
    }

    pub fn save(&self, session: &Session) -> Result<()> {
        let mut conn = self.open_conn()?;
        let tx = conn.transaction().map_err(db_error)?;

        tx.execute(
            "INSERT INTO sessions (id, created_at, updated_at, metadata, parent_id)
             VALUES (?1, ?2, ?3, ?4, ?5)
             ON CONFLICT(id) DO UPDATE SET
               updated_at = excluded.updated_at,
               metadata = excluded.metadata,
               parent_id = excluded.parent_id",
            params![
                session.id.to_string(),
                session.created_at.to_rfc3339(),
                session.updated_at.to_rfc3339(),
                serde_json::to_string(&session.metadata)
                    .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                session.metadata.get("parent_id").and_then(Value::as_str)
            ],
        )
        .map_err(db_error)?;

        tx.execute(
            "DELETE FROM messages WHERE session_id = ?1",
            params![session.id.to_string()],
        )
        .map_err(db_error)?;

        for message in &session.messages {
            tx.execute(
                "INSERT INTO messages (id, session_id, role, content, timestamp, tool_calls, tool_results)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                params![
                    message.id.to_string(),
                    session.id.to_string(),
                    role_to_str(&message.role),
                    message.content,
                    message.timestamp.to_rfc3339(),
                    serde_json::to_string(&message.tool_calls)
                        .map_err(|error| AvaError::SerializationError(error.to_string()))?,
                    serde_json::to_string(&message.tool_results)
                        .map_err(|error| AvaError::SerializationError(error.to_string()))?
                ],
            )
            .map_err(db_error)?;
        }

        tx.commit().map_err(db_error)
    }

    pub fn get(&self, id: Uuid) -> Result<Option<Session>> {
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

    pub fn delete(&self, id: Uuid) -> Result<()> {
        let conn = self.open_conn()?;
        conn.execute(
            "DELETE FROM messages WHERE session_id = ?1",
            params![id.to_string()],
        )
        .map_err(db_error)?;

        let deleted = conn
            .execute(
                "DELETE FROM sessions WHERE id = ?1",
                params![id.to_string()],
            )
            .map_err(db_error)?;

        if deleted == 0 {
            return Err(AvaError::NotFound(format!("session {id} not found")));
        }

        Ok(())
    }

    fn get_with_conn(&self, conn: &Connection, id: Uuid) -> Result<Option<Session>> {
        let row = conn
            .query_row(
                "SELECT id, created_at, updated_at, metadata FROM sessions WHERE id = ?1",
                params![id.to_string()],
                |row| {
                    Ok((
                        row.get::<_, String>(0)?,
                        row.get::<_, String>(1)?,
                        row.get::<_, String>(2)?,
                        row.get::<_, String>(3)?,
                    ))
                },
            )
            .optional()
            .map_err(db_error)?;

        let Some((session_id, created_at, updated_at, metadata)) = row else {
            return Ok(None);
        };

        let mut messages_stmt = conn
            .prepare(
                "SELECT id, role, content, timestamp, tool_calls, tool_results
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

                Ok(Message {
                    id: parse_uuid(&row.get::<_, String>(0)?).map_err(to_conversion_error)?,
                    role: str_to_role(&row.get::<_, String>(1)?).map_err(to_conversion_error)?,
                    content: row.get(2)?,
                    timestamp: parse_datetime(&row.get::<_, String>(3)?)
                        .map_err(to_conversion_error)?,
                    tool_calls,
                    tool_results,
                    tool_call_id: None,
                })
            })
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        Ok(Some(Session {
            id: parse_uuid(&session_id)?,
            created_at: parse_datetime(&created_at)?,
            updated_at: parse_datetime(&updated_at)?,
            messages,
            metadata: serde_json::from_str(&metadata)
                .map_err(|error| AvaError::SerializationError(error.to_string()))?,
        }))
    }

    fn init_schema(&self) -> Result<()> {
        let conn = self.open_conn()?;
        conn.execute_batch(SCHEMA_SQL).map_err(db_error)
    }

    fn open_conn(&self) -> Result<Connection> {
        Connection::open(&self.db_path).map_err(db_error)
    }
}
