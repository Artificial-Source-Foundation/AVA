//! Lightweight tool output pruning — selectively replaces old tool result
//! content with a short summary, avoiding the cost of full LLM compaction.

use ava_types::{Message, Role};

use crate::token_tracker::estimate_tokens_for_message;

/// Minimum character length for a tool message to be eligible for pruning.
/// Messages shorter than this are not worth pruning.
const MIN_PRUNE_CHARS: usize = 200;

/// Prune old tool outputs from conversation history.
///
/// Walks messages from oldest to newest, replacing tool result content older
/// than `protected_tokens` (measured from the end) with a compact summary.
/// Returns the number of messages pruned.
///
/// This is much cheaper than full LLM compaction — no API call required.
pub fn prune_old_tool_outputs(messages: &mut Vec<Message>, protected_tokens: usize) -> usize {
    let total_tokens: usize = messages
        .iter()
        .map(|m| estimate_tokens_for_message(m))
        .sum();
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
    use ava_types::{Message, Role, ToolResult};

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
}
