use ava_types::{Message, Role};

use crate::error::Result;
use crate::strategies::CondensationStrategy;
use crate::token_tracker::estimate_tokens_for_message;

/// Tool results larger than this are truncated inline before dropping the whole unit.
const LARGE_TOOL_RESULT_THRESHOLD: usize = 10_000;

#[derive(Debug, Clone, Default)]
pub struct SlidingWindowStrategy;

/// A "unit" is the smallest group of messages that must stay together:
/// - An assistant message with tool_calls + its subsequent Tool messages
/// - Or a standalone message (user/system/assistant without tool calls)
struct MessageUnit {
    /// Indices into the original message slice
    indices: Vec<usize>,
}

fn group_into_units(messages: &[Message]) -> Vec<MessageUnit> {
    let mut units = Vec::new();
    let mut i = 0;
    while i < messages.len() {
        let msg = &messages[i];
        if msg.role == Role::Assistant && !msg.tool_calls.is_empty() {
            // Gather this assistant message + all following Tool messages
            let mut indices = vec![i];
            let mut j = i + 1;
            while j < messages.len() && messages[j].role == Role::Tool {
                indices.push(j);
                j += 1;
            }
            units.push(MessageUnit { indices });
            i = j;
        } else {
            units.push(MessageUnit { indices: vec![i] });
            i += 1;
        }
    }
    units
}

fn truncate_large_tool_results(messages: &[Message], unit: &MessageUnit) -> Vec<Message> {
    unit.indices
        .iter()
        .map(|&idx| {
            let msg = &messages[idx];
            if msg.role == Role::Tool && msg.content.len() > LARGE_TOOL_RESULT_THRESHOLD {
                let mut truncated = msg.clone();
                let original_len = truncated.content.len();
                let mut cut = LARGE_TOOL_RESULT_THRESHOLD;
                while cut > 0 && !truncated.content.is_char_boundary(cut) {
                    cut -= 1;
                }
                truncated.content.truncate(cut);
                truncated
                    .content
                    .push_str(&format!("\n\n[... truncated {} chars]", original_len - cut));
                truncated
            } else {
                msg.clone()
            }
        })
        .collect()
}

impl CondensationStrategy for SlidingWindowStrategy {
    fn name(&self) -> &'static str {
        "sliding_window"
    }

    fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>> {
        let units = group_into_units(messages);
        let mut selected_units: Vec<Vec<Message>> = Vec::new();
        let mut used = 0_usize;

        // Always preserve the system prompt if it's the first message
        let skip_first = if !units.is_empty()
            && units[0].indices.len() == 1
            && messages[units[0].indices[0]].role == Role::System
        {
            let sys_tokens: usize = units[0]
                .indices
                .iter()
                .map(|&idx| estimate_tokens_for_message(&messages[idx]))
                .sum();
            if sys_tokens <= max_tokens {
                let sys_messages: Vec<Message> = units[0]
                    .indices
                    .iter()
                    .map(|&idx| messages[idx].clone())
                    .collect();
                used += sys_tokens;
                selected_units.push(sys_messages);
            }
            true
        } else {
            false
        };

        // Iterate remaining units in reverse (newest first)
        for unit in units.iter().rev() {
            // Skip the system prompt unit — already handled
            if skip_first && std::ptr::eq(unit, &units[0]) {
                continue;
            }
            let unit_tokens: usize = unit
                .indices
                .iter()
                .map(|&idx| estimate_tokens_for_message(&messages[idx]))
                .sum();

            if used + unit_tokens <= max_tokens {
                // Whole unit fits
                let unit_messages: Vec<Message> = unit
                    .indices
                    .iter()
                    .map(|&idx| messages[idx].clone())
                    .collect();
                used += unit_tokens;
                selected_units.push(unit_messages);
            } else {
                // Try truncating large tool results in this unit
                let truncated = truncate_large_tool_results(messages, unit);
                let truncated_tokens: usize =
                    truncated.iter().map(estimate_tokens_for_message).sum();
                if used + truncated_tokens <= max_tokens {
                    used += truncated_tokens;
                    selected_units.push(truncated);
                }
                // Otherwise skip the entire unit
            }
        }

        // Reverse to restore chronological order
        selected_units.reverse();
        Ok(selected_units.into_iter().flatten().collect())
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolCall};
    use serde_json::json;

    use super::*;

    #[test]
    fn keeps_latest_messages_within_budget() {
        let strategy = SlidingWindowStrategy;
        let messages = vec![
            Message::new(Role::User, "one"),
            Message::new(Role::Assistant, "two two two two two two"),
            Message::new(Role::User, "three"),
        ];
        let out = strategy.condense(&messages, 20).unwrap();
        assert!(!out.is_empty());
        assert_eq!(out.last().unwrap().content, "three");
    }

    #[test]
    fn truncation_preserves_tool_pairs() {
        let strategy = SlidingWindowStrategy;

        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: json!({"path": "/tmp/file"}),
        };
        let assistant = Message::new(Role::Assistant, "Reading file").with_tool_calls(vec![tc]);
        let tool_result =
            Message::new(Role::Tool, "file contents here").with_tool_call_id("call_1");
        let user = Message::new(Role::User, "thanks");

        let messages = vec![assistant.clone(), tool_result.clone(), user.clone()];

        // With enough budget, all three should be kept together
        let out = strategy.condense(&messages, 500).unwrap();
        assert_eq!(out.len(), 3);
        assert_eq!(out[0].role, Role::Assistant);
        assert_eq!(out[1].role, Role::Tool);
        assert_eq!(out[2].role, Role::User);

        // With very tight budget, should not include just the assistant without its tool result
        let out = strategy.condense(&messages, 12).unwrap();
        // Either the unit is included in full or not at all
        let has_assistant = out.iter().any(|m| m.role == Role::Assistant);
        let has_tool = out.iter().any(|m| m.role == Role::Tool);
        assert_eq!(
            has_assistant, has_tool,
            "assistant and tool must come together"
        );
    }

    #[test]
    fn large_tool_result_truncated_inline() {
        let strategy = SlidingWindowStrategy;

        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: json!({"path": "/big"}),
        };
        let assistant =
            Message::new(Role::Assistant, "Reading the file now").with_tool_calls(vec![tc]);
        // 20KB tool result with multiple words so token count is realistic
        let big_content = (0..5000)
            .map(|i| format!("line{i}"))
            .collect::<Vec<_>>()
            .join(" ");
        let tool_result = Message::new(Role::Tool, big_content).with_tool_call_id("call_1");

        let messages = vec![assistant, tool_result];

        // Budget large enough for truncated content (~10KB) but not full (~30KB)
        // Full unit: ~5000 words * 4/3 = ~6666 tokens + overhead
        // Truncated: ~10KB content ≈ ~2500 words * 4/3 = ~3333 tokens + overhead
        let out = strategy.condense(&messages, 4000).unwrap();
        assert_eq!(out.len(), 2, "both assistant + tool should be kept");
        assert!(
            out[1].content.contains("[... truncated"),
            "tool result should be truncated"
        );
        assert!(out[1].content.len() < 15_000);
    }
}
