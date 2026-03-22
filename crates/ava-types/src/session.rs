//! Session management types

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use std::collections::HashSet;

use crate::message::{Message, Role};
use crate::tool::ToolResult;
use crate::TokenUsage;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Session {
    pub id: Uuid,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub messages: Vec<Message>,
    pub metadata: serde_json::Value,
    /// Accumulated token usage across all turns in this session.
    #[serde(default)]
    pub token_usage: TokenUsage,
    /// Active branch head — the leaf message ID of the currently selected branch.
    /// `None` means linear mode (use all messages in order).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub branch_head: Option<Uuid>,
}

impl Session {
    pub fn new() -> Self {
        let now = Utc::now();
        Self {
            id: Uuid::new_v4(),
            created_at: now,
            updated_at: now,
            messages: Vec::new(),
            metadata: serde_json::json!({}),
            token_usage: TokenUsage::default(),
            branch_head: None,
        }
    }

    /// Create a session with a specific ID (e.g., one provided by a frontend).
    pub fn with_id(mut self, id: Uuid) -> Self {
        self.id = id;
        self
    }

    pub fn with_metadata(mut self, metadata: serde_json::Value) -> Self {
        self.metadata = metadata;
        self
    }

    pub fn add_message(&mut self, message: Message) {
        self.messages.push(message);
        self.updated_at = Utc::now();
    }
}

impl Default for Session {
    fn default() -> Self {
        Self::new()
    }
}

/// Scan messages for assistant tool_calls that lack a corresponding tool_result
/// message and append synthetic error results for each orphaned call.
///
/// After a crash or interruption, the last checkpoint may contain an assistant
/// message with `tool_calls` but no subsequent `Role::Tool` messages carrying
/// the results.  LLM APIs (Anthropic, OpenAI) require every `tool_use` to have
/// a matching `tool_result`, so we synthesize error results to keep the
/// conversation valid.
pub fn cleanup_interrupted_tools(messages: &mut Vec<Message>) {
    // Collect all tool_call IDs that already have a result message.
    let answered: HashSet<&str> = messages
        .iter()
        .filter(|m| m.role == Role::Tool)
        .filter_map(|m| m.tool_call_id.as_deref())
        .collect();

    // Find orphaned tool_calls (present in an Assistant message but no
    // matching Tool message exists).
    let mut orphaned: Vec<(String, String)> = Vec::new(); // (call_id, tool_name)
    for msg in messages.iter() {
        if msg.role != Role::Assistant {
            continue;
        }
        for tc in &msg.tool_calls {
            if !answered.contains(tc.id.as_str()) {
                orphaned.push((tc.id.clone(), tc.name.clone()));
            }
        }
    }

    // Append a synthetic error Tool message for each orphaned call.
    for (call_id, _tool_name) in orphaned {
        let result = ToolResult {
            call_id: call_id.clone(),
            content: "[Tool execution was interrupted]".to_string(),
            is_error: true,
        };
        let tool_msg = Message::new(Role::Tool, result.content.clone())
            .with_tool_call_id(&call_id)
            .with_tool_results(vec![result]);
        messages.push(tool_msg);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::message::Role;
    use crate::tool::ToolCall;

    #[test]
    fn test_session_creation() {
        let session = Session::new();
        assert!(!session.id.to_string().is_empty());
        assert!(session.messages.is_empty());
        assert_eq!(session.metadata, serde_json::json!({}));
    }

    #[test]
    fn test_session_with_metadata() {
        let metadata = serde_json::json!({
            "project": "AVA",
            "version": "0.1.0"
        });
        let session = Session::new().with_metadata(metadata.clone());
        assert_eq!(session.metadata, metadata);
    }

    #[test]
    fn test_session_add_message() {
        let mut session = Session::new();
        let message = Message::new(Role::User, "Hello");
        let original_updated_at = session.updated_at;

        session.add_message(message);

        assert_eq!(session.messages.len(), 1);
        assert!(session.updated_at >= original_updated_at);
    }

    // ── cleanup_interrupted_tools tests ──

    #[test]
    fn test_cleanup_no_tool_calls() {
        let mut messages = vec![
            Message::new(Role::User, "Hello"),
            Message::new(Role::Assistant, "Hi there"),
        ];
        cleanup_interrupted_tools(&mut messages);
        assert_eq!(messages.len(), 2);
    }

    #[test]
    fn test_cleanup_complete_tool_calls() {
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({}),
        };
        let tr = ToolResult {
            call_id: "call_1".to_string(),
            content: "file contents".to_string(),
            is_error: false,
        };
        let mut messages = vec![
            Message::new(Role::User, "read file"),
            Message::new(Role::Assistant, "reading").with_tool_calls(vec![tc]),
            Message::new(Role::Tool, "file contents")
                .with_tool_call_id("call_1")
                .with_tool_results(vec![tr]),
        ];
        cleanup_interrupted_tools(&mut messages);
        // No new messages should be added
        assert_eq!(messages.len(), 3);
    }

    #[test]
    fn test_cleanup_orphaned_tool_call() {
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({"command": "ls"}),
        };
        let mut messages = vec![
            Message::new(Role::User, "list files"),
            Message::new(Role::Assistant, "running ls").with_tool_calls(vec![tc]),
            // No Tool message — simulates a crash during execution
        ];
        cleanup_interrupted_tools(&mut messages);
        assert_eq!(messages.len(), 3);
        let synth = &messages[2];
        assert_eq!(synth.role, Role::Tool);
        assert_eq!(synth.tool_call_id.as_deref(), Some("call_1"));
        assert_eq!(synth.content, "[Tool execution was interrupted]");
        assert!(synth.tool_results[0].is_error);
    }

    #[test]
    fn test_cleanup_multiple_orphaned_calls() {
        let tc1 = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({}),
        };
        let tc2 = ToolCall {
            id: "call_2".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({}),
        };
        let tr1 = ToolResult {
            call_id: "call_1".to_string(),
            content: "ok".to_string(),
            is_error: false,
        };
        let mut messages = vec![
            Message::new(Role::User, "do stuff"),
            Message::new(Role::Assistant, "doing").with_tool_calls(vec![tc1, tc2]),
            // Only call_1 has a result; call_2 was interrupted
            Message::new(Role::Tool, "ok")
                .with_tool_call_id("call_1")
                .with_tool_results(vec![tr1]),
        ];
        cleanup_interrupted_tools(&mut messages);
        assert_eq!(messages.len(), 4);
        let synth = &messages[3];
        assert_eq!(synth.tool_call_id.as_deref(), Some("call_2"));
        assert!(synth.tool_results[0].is_error);
    }

    #[test]
    fn test_cleanup_empty_messages() {
        let mut messages: Vec<Message> = vec![];
        cleanup_interrupted_tools(&mut messages);
        assert!(messages.is_empty());
    }

    #[test]
    fn test_cleanup_idempotent() {
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({}),
        };
        let mut messages = vec![
            Message::new(Role::User, "run"),
            Message::new(Role::Assistant, "running").with_tool_calls(vec![tc]),
        ];
        cleanup_interrupted_tools(&mut messages);
        assert_eq!(messages.len(), 3);
        // Running again should not add duplicates
        cleanup_interrupted_tools(&mut messages);
        assert_eq!(messages.len(), 3);
    }
}
