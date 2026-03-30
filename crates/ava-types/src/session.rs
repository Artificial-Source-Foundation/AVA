//! Session management types

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use std::collections::HashSet;

use crate::message::{Message, Role};
use crate::tool::ToolResult;
use crate::TokenUsage;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "camelCase")]
pub struct ExternalSessionLink {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub provider: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub agent_name: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub external_session_id: Option<String>,
    #[serde(default)]
    pub resume_attempted: bool,
    #[serde(default)]
    pub resumed: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cwd: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Default)]
#[serde(rename_all = "camelCase")]
pub struct DelegationRecord {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub agent_type: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub provider: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub parent_session_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub child_session_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub external_session_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub policy_reason: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub policy_version: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub latency_ms: Option<u64>,
    #[serde(default)]
    pub resumed: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub input_tokens: Option<usize>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub output_tokens: Option<usize>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub cost_usd: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub outcome: Option<String>,
}

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

/// Repair a conversation history before sending it to an LLM.
///
/// Fixes structural issues that cause cryptic API errors:
/// 1. Orphaned tool results — `Role::Tool` messages without a preceding assistant
///    message that contains a matching `tool_call`. Removed.
/// 2. Empty assistant messages — assistant messages with no content and no tool calls.
///    Removed.
/// 3. Consecutive user messages — merged into a single user message (LLMs expect
///    alternating user/assistant).
/// 4. Messages after a terminal assistant response — if an assistant message has no
///    tool calls (i.e. it is a final response), remove any non-user messages that
///    follow it (stale tool results from a prior turn, etc.).
/// 5. Duplicate messages — same role + same content in immediate sequence. Deduplicated.
///
/// This is intentionally conservative: it only touches messages that would cause API
/// errors. Call it right before building the LLM request.
pub fn repair_conversation(messages: &mut Vec<Message>) {
    if messages.is_empty() {
        return;
    }

    // --- Pass 1: Remove empty assistant messages ---
    messages.retain(|m| {
        if m.role == Role::Assistant && m.content.trim().is_empty() && m.tool_calls.is_empty() {
            return false;
        }
        true
    });

    // --- Pass 2: Remove orphaned tool results ---
    // Collect all tool_call IDs present in assistant messages.
    let valid_call_ids: HashSet<String> = messages
        .iter()
        .filter(|m| m.role == Role::Assistant)
        .flat_map(|m| m.tool_calls.iter().map(|tc| tc.id.clone()))
        .collect();

    messages.retain(|m| {
        if m.role == Role::Tool {
            if let Some(ref call_id) = m.tool_call_id {
                return valid_call_ids.contains(call_id);
            }
            // Tool message with no call_id — orphaned, remove it.
            return false;
        }
        true
    });

    // --- Pass 3: Remove non-user messages after terminal assistant ---
    // A "terminal" assistant message has content but no tool calls.
    // Any Tool messages after it are stale leftovers.
    let mut last_terminal_assistant_idx: Option<usize> = None;
    for (i, m) in messages.iter().enumerate() {
        if m.role == Role::Assistant && m.tool_calls.is_empty() && !m.content.trim().is_empty() {
            last_terminal_assistant_idx = Some(i);
        } else if m.role == Role::User {
            // A user message resets — the user is continuing the conversation.
            last_terminal_assistant_idx = None;
        }
    }
    if let Some(idx) = last_terminal_assistant_idx {
        // Remove any non-user messages after the terminal assistant.
        let mut i = idx + 1;
        while i < messages.len() {
            if messages[i].role != Role::User {
                messages.remove(i);
            } else {
                i += 1;
            }
        }
    }

    // --- Pass 4: Merge consecutive user messages ---
    let mut i = 0;
    while i + 1 < messages.len() {
        if messages[i].role == Role::User && messages[i + 1].role == Role::User {
            let next_content = messages[i + 1].content.clone();
            let next_images = messages[i + 1].images.clone();
            messages[i].content = if messages[i].content.is_empty() {
                next_content
            } else if next_content.is_empty() {
                messages[i].content.clone()
            } else {
                format!("{}\n\n{}", messages[i].content, next_content)
            };
            messages[i].images.extend(next_images);
            messages.remove(i + 1);
        } else {
            i += 1;
        }
    }

    // --- Pass 5: Remove sequential duplicates (same role + same content) ---
    let mut i = 0;
    while i + 1 < messages.len() {
        if messages[i].role == messages[i + 1].role
            && messages[i].content == messages[i + 1].content
            && messages[i].tool_calls.len() == messages[i + 1].tool_calls.len()
        {
            messages.remove(i + 1);
        } else {
            i += 1;
        }
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

    // ── repair_conversation tests ──

    #[test]
    fn test_repair_empty() {
        let mut messages: Vec<Message> = vec![];
        repair_conversation(&mut messages);
        assert!(messages.is_empty());
    }

    #[test]
    fn test_repair_valid_conversation_unchanged() {
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({}),
        };
        let tr = ToolResult {
            call_id: "call_1".to_string(),
            content: "ok".to_string(),
            is_error: false,
        };
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "calling read").with_tool_calls(vec![tc]),
            Message::new(Role::Tool, "ok")
                .with_tool_call_id("call_1")
                .with_tool_results(vec![tr]),
            Message::new(Role::Assistant, "done"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 4);
    }

    #[test]
    fn test_repair_orphaned_tool_result() {
        // Tool result with no matching assistant tool_call — should be removed.
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "hi"),
            Message::new(Role::Tool, "stale result").with_tool_call_id("nonexistent_call"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 2);
        assert_eq!(messages[0].role, Role::User);
        assert_eq!(messages[1].role, Role::Assistant);
    }

    #[test]
    fn test_repair_tool_result_no_call_id() {
        // Tool message with no call_id at all — orphaned, remove.
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Tool, "mystery result"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 1);
        assert_eq!(messages[0].role, Role::User);
    }

    #[test]
    fn test_repair_empty_assistant_message() {
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, ""),
            Message::new(Role::Assistant, "real response"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 2);
        assert_eq!(messages[1].content, "real response");
    }

    #[test]
    fn test_repair_whitespace_only_assistant_message() {
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "   \n  "),
            Message::new(Role::Assistant, "actual reply"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 2);
        assert_eq!(messages[1].content, "actual reply");
    }

    #[test]
    fn test_repair_assistant_with_tool_calls_not_removed() {
        // An assistant message with empty content but tool_calls should NOT be removed.
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({}),
        };
        let tr = ToolResult {
            call_id: "call_1".to_string(),
            content: "ok".to_string(),
            is_error: false,
        };
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "").with_tool_calls(vec![tc]),
            Message::new(Role::Tool, "ok")
                .with_tool_call_id("call_1")
                .with_tool_results(vec![tr]),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 3);
    }

    #[test]
    fn test_repair_consecutive_user_messages_merged() {
        let mut messages = vec![
            Message::new(Role::User, "first"),
            Message::new(Role::User, "second"),
            Message::new(Role::User, "third"),
            Message::new(Role::Assistant, "reply"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 2);
        assert_eq!(messages[0].role, Role::User);
        assert!(messages[0].content.contains("first"));
        assert!(messages[0].content.contains("second"));
        assert!(messages[0].content.contains("third"));
        assert_eq!(messages[1].role, Role::Assistant);
    }

    #[test]
    fn test_repair_messages_after_terminal_assistant() {
        // After a terminal assistant message (no tool calls), stale Tool messages
        // should be removed, but user messages should be kept.
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "done, no more tools"),
            Message::new(Role::Tool, "stale tool result").with_tool_call_id("old_call"),
        ];
        repair_conversation(&mut messages);
        // The tool result is orphaned (no matching tool_call) AND after terminal assistant.
        assert_eq!(messages.len(), 2);
        assert_eq!(messages[0].role, Role::User);
        assert_eq!(messages[1].role, Role::Assistant);
    }

    #[test]
    fn test_repair_user_after_terminal_assistant_kept() {
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "done"),
            Message::new(Role::User, "follow up"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 3);
        assert_eq!(messages[2].role, Role::User);
        assert_eq!(messages[2].content, "follow up");
    }

    #[test]
    fn test_repair_duplicate_messages() {
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "world"),
            Message::new(Role::Assistant, "world"),
        ];
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), 2);
        assert_eq!(messages[1].content, "world");
    }

    #[test]
    fn test_repair_duplicate_user_messages() {
        // Consecutive identical user messages get merged first, then deduped.
        let mut messages = vec![
            Message::new(Role::User, "same"),
            Message::new(Role::User, "same"),
            Message::new(Role::Assistant, "reply"),
        ];
        repair_conversation(&mut messages);
        // Two "same" user messages get merged into "same\n\nsame", so no exact dedup.
        // But the merge itself consolidates them.
        assert_eq!(messages.len(), 2);
        assert_eq!(messages[0].role, Role::User);
        assert_eq!(messages[1].role, Role::Assistant);
    }

    #[test]
    fn test_repair_idempotent() {
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::User, "world"),
            Message::new(Role::Assistant, ""),
            Message::new(Role::Assistant, "reply"),
            Message::new(Role::Tool, "orphan").with_tool_call_id("nope"),
        ];
        repair_conversation(&mut messages);
        let first_pass = messages.clone();
        repair_conversation(&mut messages);
        assert_eq!(messages.len(), first_pass.len());
        for (a, b) in messages.iter().zip(first_pass.iter()) {
            assert_eq!(a.role, b.role);
            assert_eq!(a.content, b.content);
        }
    }

    #[test]
    fn test_repair_complex_scenario() {
        // A complex scenario combining multiple issues.
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({}),
        };
        let tr = ToolResult {
            call_id: "call_1".to_string(),
            content: "file data".to_string(),
            is_error: false,
        };
        let mut messages = vec![
            Message::new(Role::User, "step 1"),
            Message::new(Role::User, "step 2"), // consecutive user — merge
            Message::new(Role::Assistant, ""),  // empty assistant — remove
            Message::new(Role::Assistant, "reading").with_tool_calls(vec![tc]),
            Message::new(Role::Tool, "file data")
                .with_tool_call_id("call_1")
                .with_tool_results(vec![tr]),
            Message::new(Role::Tool, "orphan").with_tool_call_id("call_99"), // orphaned — remove
            Message::new(Role::Assistant, "all done"),
        ];
        repair_conversation(&mut messages);
        // Expected: merged user, assistant+tool_call, tool_result, terminal assistant
        assert_eq!(messages.len(), 4);
        assert_eq!(messages[0].role, Role::User);
        assert!(messages[0].content.contains("step 1"));
        assert!(messages[0].content.contains("step 2"));
        assert_eq!(messages[1].role, Role::Assistant);
        assert_eq!(messages[1].content, "reading");
        assert_eq!(messages[2].role, Role::Tool);
        assert_eq!(messages[2].tool_call_id.as_deref(), Some("call_1"));
        assert_eq!(messages[3].role, Role::Assistant);
        assert_eq!(messages[3].content, "all done");
    }
}
