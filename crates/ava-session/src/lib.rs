//! AVA Session — session persistence with SQLite storage.
//!
//! This crate provides:
//! - Session creation, saving, and loading
//! - Message history management
//! - Full-text search over sessions

pub mod diff_tracking;
mod helpers;
mod manager;
mod search;
mod tree;

use std::path::{Path, PathBuf};

use ava_types::{Result, Session};
use rusqlite::Connection;

/// A labeled bookmark at a specific message index within a session.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct Bookmark {
    pub id: uuid::Uuid,
    pub session_id: uuid::Uuid,
    pub label: String,
    pub message_index: usize,
    pub created_at: chrono::DateTime<chrono::Utc>,
}

/// A node in the conversation tree (BG-10).
#[derive(Debug, Clone)]
pub struct TreeNode {
    pub message: ava_types::Message,
    pub children: Vec<uuid::Uuid>,
}

/// Full conversation tree for a session (BG-10).
#[derive(Debug, Clone)]
pub struct ConversationTree {
    /// Root message ID (first message in the session).
    pub root: Option<uuid::Uuid>,
    /// All nodes keyed by message ID.
    pub nodes: std::collections::HashMap<uuid::Uuid, TreeNode>,
    /// Currently active branch head.
    pub branch_head: Option<uuid::Uuid>,
}

/// Summary of a branch leaf for selection UI (BG-10).
#[derive(Debug, Clone)]
pub struct BranchLeaf {
    pub leaf_id: uuid::Uuid,
    pub preview: String,
    pub depth: usize,
    pub role: ava_types::Role,
    pub timestamp: chrono::DateTime<chrono::Utc>,
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
