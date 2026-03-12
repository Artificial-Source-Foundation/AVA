//! AVA Session — session persistence with SQLite storage.
//!
//! This crate provides:
//! - Session creation, saving, and loading
//! - Message history management
//! - Full-text search over sessions

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
        let manager = Self {
            db_path: path.as_ref().to_path_buf(),
        };
        manager.init_schema()?;
        Ok(manager)
    }

    pub fn create(&self) -> Result<Session> {
        Ok(Session::new())
    }

    pub fn db_path(&self) -> &Path {
        &self.db_path
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
                    images: Vec::new(),
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
            token_usage: ava_types::TokenUsage::default(),
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
