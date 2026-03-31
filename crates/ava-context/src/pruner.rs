//! Lightweight tool output pruning — selectively replaces old tool result
//! content with a short summary, avoiding the cost of full LLM compaction.
//!
//! ## F4 — Smart Context Pruning
//!
//! Three pruning passes (in order):
//! 1. **Dedup**: Identical `(tool_name, args_hash)` pairs → keep only the latest result.
//! 2. **Edit caching**: Successful write/edit results older than 2 turns → compact marker.
//! 3. **Age-based**: Old tool outputs beyond the protected window → short summary.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};

use ava_types::{Message, Role};

use crate::token_tracker::estimate_tokens_for_message;

/// Minimum character length for a tool message to be eligible for pruning.
/// Messages shorter than this are not worth pruning.
const MIN_PRUNE_CHARS: usize = 200;

/// Tools whose successful results can be compacted after EDIT_CACHE_AGE turns.
const EDIT_TOOLS: &[&str] = &["write", "edit", "multiedit", "apply_patch"];

/// Number of turns after which successful edit results are compacted.
const EDIT_CACHE_AGE: usize = 2;

/// Hash a tool call's name + arguments for dedup detection.
fn tool_call_hash(tc: &ava_types::ToolCall) -> u64 {
    let mut hasher = DefaultHasher::new();
    tc.name.hash(&mut hasher);
    // Use Display impl on serde_json::Value (always available via ava_types)
    tc.arguments.to_string().hash(&mut hasher);
    hasher.finish()
}

/// F4 — Dedup pass: find duplicate `(tool_name, args_hash)` pairs across
/// assistant messages and replace all but the latest with a compact marker.
/// Returns the number of deduplicated tool results.
pub fn dedup_tool_results(messages: &mut [Message]) -> usize {
    use std::collections::HashMap;

    // Pass 1: collect the latest index for each (tool_name, args_hash) pair.
    // We only look at assistant messages that carry tool_calls.
    let mut latest: HashMap<u64, usize> = HashMap::new();
    for (i, msg) in messages.iter().enumerate() {
        if msg.role != Role::Assistant {
            continue;
        }
        for tc in &msg.tool_calls {
            let hash = tool_call_hash(tc);
            latest.insert(hash, i);
        }
    }

    // Pass 2: for each assistant message with tool_calls, if it's NOT the latest
    // for that hash, find the matching tool-result message and compact it.
    let mut deduped = 0;
    let mut stale_call_ids: Vec<String> = Vec::new();

    for (i, msg) in messages.iter().enumerate() {
        if msg.role != Role::Assistant {
            continue;
        }
        for tc in &msg.tool_calls {
            let hash = tool_call_hash(tc);
            if let Some(&latest_idx) = latest.get(&hash) {
                if latest_idx != i {
                    stale_call_ids.push(tc.id.clone());
                }
            }
        }
    }

    // Pass 3: compact stale tool-result messages.
    for msg in messages.iter_mut() {
        if msg.role != Role::Tool {
            continue;
        }
        if let Some(ref call_id) = msg.tool_call_id {
            if stale_call_ids.contains(call_id) && msg.content.len() > MIN_PRUNE_CHARS {
                let original_len = msg.content.len();
                msg.content =
                    format!("[duplicate tool call — superseded by later identical call, was {original_len} chars]");
                for result in &mut msg.tool_results {
                    if result.content.len() > MIN_PRUNE_CHARS {
                        result.content = msg.content.clone();
                    }
                }
                deduped += 1;
            }
        }
    }

    if deduped > 0 {
        tracing::info!(
            stale_count = stale_call_ids.len(),
            compacted = deduped,
            "F4: deduped stale tool calls"
        );
    }

    deduped
}

/// F4 — Edit caching: replace successful write/edit tool results with a compact
/// marker when they are older than `EDIT_CACHE_AGE` turns from the end.
/// Returns the number of messages compacted.
pub fn compact_old_edit_results(messages: &mut [Message]) -> usize {
    // Count assistant turns (each assistant message = 1 turn).
    let total_turns: usize = messages
        .iter()
        .filter(|m| m.role == Role::Assistant)
        .count();

    if total_turns <= EDIT_CACHE_AGE {
        return 0;
    }

    // Find the message index where the protected window starts (last EDIT_CACHE_AGE turns).
    let mut turns_from_end = 0;
    let mut protected_boundary = messages.len();
    for (i, msg) in messages.iter().enumerate().rev() {
        if msg.role == Role::Assistant {
            turns_from_end += 1;
            if turns_from_end >= EDIT_CACHE_AGE {
                protected_boundary = i;
                break;
            }
        }
    }

    let mut compacted = 0;

    // Walk assistant messages before the boundary. For each edit tool call,
    // find and compact the corresponding tool-result message.
    let mut stale_edit_ids: Vec<String> = Vec::new();
    for msg in messages[..protected_boundary].iter() {
        if msg.role != Role::Assistant {
            continue;
        }
        for tc in &msg.tool_calls {
            if EDIT_TOOLS.contains(&tc.name.as_str()) {
                stale_edit_ids.push(tc.id.clone());
            }
        }
    }

    for msg in messages[..protected_boundary].iter_mut() {
        if msg.role != Role::Tool {
            continue;
        }
        if let Some(ref call_id) = msg.tool_call_id {
            if stale_edit_ids.contains(call_id) && msg.content.len() > MIN_PRUNE_CHARS {
                // Only compact successful results (not errors)
                let is_error = msg
                    .tool_results
                    .first()
                    .map(|r| r.is_error)
                    .unwrap_or(false);
                if !is_error {
                    let original_len = msg.content.len();
                    msg.content =
                        format!("[edit result cached — was {original_len} chars, file was modified successfully]");
                    for result in &mut msg.tool_results {
                        if result.content.len() > MIN_PRUNE_CHARS {
                            result.content = msg.content.clone();
                        }
                    }
                    compacted += 1;
                }
            }
        }
    }

    if compacted > 0 {
        tracing::info!(compacted, "F4: compacted old edit results");
    }

    compacted
}

/// Prune old tool outputs from conversation history.
///
/// Walks messages from oldest to newest, replacing tool result content older
/// than `protected_tokens` (measured from the end) with a compact summary.
/// Returns the number of messages pruned.
///
/// This is much cheaper than full LLM compaction — no API call required.
pub fn prune_old_tool_outputs(messages: &mut [Message], protected_tokens: usize) -> usize {
    let total_tokens: usize = messages.iter().map(estimate_tokens_for_message).sum();
    if total_tokens <= protected_tokens {
        return 0;
    }

    // Walk backwards to figure out which messages fall within the protected window.
    let mut tokens_from_end: usize = 0;
    let mut protected_boundary = messages.len(); // index where protection starts (inclusive to end)

    for (i, msg) in messages.iter().enumerate().rev() {
        let msg_tokens = estimate_tokens_for_message(msg);
        tokens_from_end += msg_tokens;
        if tokens_from_end > protected_tokens {
            protected_boundary = i + 1;
            break;
        }
    }

    // If everything fits in the protected window, nothing to prune.
    if protected_boundary == 0 {
        return 0;
    }

    let mut pruned_count = 0;

    for msg in messages[..protected_boundary].iter_mut() {
        // Only prune tool-role messages with substantial content.
        if msg.role != Role::Tool {
            continue;
        }

        if msg.content.len() > MIN_PRUNE_CHARS {
            let original_len = msg.content.len();
            msg.content = format!("[tool output pruned -- was {original_len} chars]");
            pruned_count += 1;
        }

        // Also prune individual tool_results entries.
        for result in &mut msg.tool_results {
            if result.content.len() > MIN_PRUNE_CHARS {
                let original_len = result.content.len();
                result.content = format!("[tool output pruned -- was {original_len} chars]");
                // Don't double-count: the message-level prune is what matters.
            }
        }
    }

    pruned_count
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolCall, ToolResult};

    use super::*;

    fn tool_message(content: &str) -> Message {
        let mut msg = Message::new(Role::Tool, content);
        msg.tool_results.push(ToolResult {
            call_id: "call-1".to_string(),
            content: content.to_string(),
            is_error: false,
        });
        msg
    }

    fn tool_message_with_id(content: &str, call_id: &str) -> Message {
        let mut msg = Message::new(Role::Tool, content).with_tool_call_id(call_id);
        msg.tool_results.push(ToolResult {
            call_id: call_id.to_string(),
            content: content.to_string(),
            is_error: false,
        });
        msg
    }

    fn assistant_with_tool_calls(calls: Vec<ToolCall>) -> Message {
        Message::new(Role::Assistant, "ok").with_tool_calls(calls)
    }

    #[test]
    fn no_pruning_when_under_limit() {
        let mut messages = vec![
            Message::new(Role::User, "hello"),
            tool_message("short output"),
        ];
        let pruned = prune_old_tool_outputs(&mut messages, 100_000);
        assert_eq!(pruned, 0);
        assert_eq!(messages[1].content, "short output");
    }

    #[test]
    fn prunes_old_large_tool_outputs() {
        let big_output = "x ".repeat(500); // ~500 words = ~666 tokens
        let mut messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "do something"),
            tool_message(&big_output),
            Message::new(Role::Assistant, "here is the result"),
            Message::new(Role::User, "now do another thing"),
            tool_message("small"),
        ];

        // Protect only the last ~50 tokens so the old tool message gets pruned.
        let pruned = prune_old_tool_outputs(&mut messages, 50);
        assert_eq!(pruned, 1);
        assert!(messages[2].content.contains("pruned"));
        // The recent small tool message is protected.
        assert_eq!(messages[5].content, "small");
    }

    #[test]
    fn skips_short_tool_messages() {
        let mut messages = vec![
            Message::new(Role::User, "hi"),
            tool_message("ok"), // too short to prune
            Message::new(Role::User, "more words to push us over the limit maybe"),
        ];
        let pruned = prune_old_tool_outputs(&mut messages, 5);
        assert_eq!(pruned, 0);
    }

    #[test]
    fn prunes_tool_results_content_too() {
        let big_output = "x ".repeat(500);
        let mut messages = vec![
            tool_message(&big_output),
            Message::new(Role::User, "recent message"),
        ];
        let pruned = prune_old_tool_outputs(&mut messages, 10);
        assert_eq!(pruned, 1);
        assert!(messages[0].tool_results[0].content.contains("pruned"));
    }

    #[test]
    fn does_not_prune_non_tool_messages() {
        let big_content = "word ".repeat(500);
        let mut messages = vec![
            Message::new(Role::Assistant, &big_content),
            Message::new(Role::User, "recent"),
        ];
        let pruned = prune_old_tool_outputs(&mut messages, 10);
        assert_eq!(pruned, 0);
        assert!(messages[0].content.len() > 200);
    }

    // --- F4: Dedup tests ---

    #[test]
    fn dedup_keeps_only_latest_duplicate_tool_call() {
        let big = "x ".repeat(200);
        let args = serde_json::json!({"path": "src/main.rs"});
        let mut messages = vec![
            // Turn 1: read src/main.rs
            assistant_with_tool_calls(vec![ToolCall {
                id: "call-1".to_string(),
                name: "read".to_string(),
                arguments: args.clone(),
            }]),
            tool_message_with_id(&big, "call-1"),
            // Turn 2: same read again
            assistant_with_tool_calls(vec![ToolCall {
                id: "call-2".to_string(),
                name: "read".to_string(),
                arguments: args.clone(),
            }]),
            tool_message_with_id(&big, "call-2"),
            // Turn 3: yet another duplicate
            assistant_with_tool_calls(vec![ToolCall {
                id: "call-3".to_string(),
                name: "read".to_string(),
                arguments: args.clone(),
            }]),
            tool_message_with_id(&big, "call-3"),
        ];

        let deduped = dedup_tool_results(&mut messages);
        assert_eq!(deduped, 2);
        // First two tool results are compacted
        assert!(messages[1].content.contains("superseded"));
        assert!(messages[3].content.contains("superseded"));
        // Latest is untouched
        assert!(!messages[5].content.contains("superseded"));
    }

    #[test]
    fn dedup_does_not_touch_different_tool_calls() {
        let big = "x ".repeat(200);
        let mut messages = vec![
            assistant_with_tool_calls(vec![ToolCall {
                id: "call-1".to_string(),
                name: "read".to_string(),
                arguments: serde_json::json!({"path": "a.rs"}),
            }]),
            tool_message_with_id(&big, "call-1"),
            assistant_with_tool_calls(vec![ToolCall {
                id: "call-2".to_string(),
                name: "read".to_string(),
                arguments: serde_json::json!({"path": "b.rs"}),
            }]),
            tool_message_with_id(&big, "call-2"),
        ];

        let deduped = dedup_tool_results(&mut messages);
        assert_eq!(deduped, 0);
    }

    // --- F4: Edit caching tests ---

    #[test]
    fn compact_old_edit_results_after_two_turns() {
        let big = "x ".repeat(200);
        let mut messages = vec![
            // Turn 1: old edit
            assistant_with_tool_calls(vec![ToolCall {
                id: "call-1".to_string(),
                name: "edit".to_string(),
                arguments: serde_json::json!({"path": "a.rs"}),
            }]),
            tool_message_with_id(&big, "call-1"),
            // Turn 2
            Message::new(Role::Assistant, "progress update"),
            // Turn 3
            Message::new(Role::Assistant, "final answer"),
        ];

        let compacted = compact_old_edit_results(&mut messages);
        assert_eq!(compacted, 1);
        assert!(messages[1].content.contains("edit result cached"));
    }

    #[test]
    fn compact_edit_preserves_recent_results() {
        let big = "x ".repeat(200);
        let mut messages = vec![
            // Turn 1 (recent): edit
            assistant_with_tool_calls(vec![ToolCall {
                id: "call-1".to_string(),
                name: "edit".to_string(),
                arguments: serde_json::json!({"path": "a.rs"}),
            }]),
            tool_message_with_id(&big, "call-1"),
            // Turn 2 (most recent)
            Message::new(Role::Assistant, "done"),
        ];

        let compacted = compact_old_edit_results(&mut messages);
        assert_eq!(compacted, 0);
        assert!(!messages[1].content.contains("cached"));
    }
}
