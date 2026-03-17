//! Observation masking strategy (BG2-12, inspired by OpenHands).
//!
//! Replaces old tool result content with `[MASKED]` to reduce token count
//! while preserving the conversation structure. Only masks tool results
//! beyond a protected window from the end.

use ava_types::Message;

use super::CondensationStrategy;
use crate::error::Result;

/// Masks old tool result content with `[MASKED]`, keeping recent results intact.
pub struct ObservationMaskingStrategy {
    /// Number of recent messages to protect from masking.
    protected_recent: usize,
}

impl ObservationMaskingStrategy {
    pub fn new(protected_recent: usize) -> Self {
        Self { protected_recent }
    }
}

impl Default for ObservationMaskingStrategy {
    fn default() -> Self {
        Self {
            protected_recent: 6,
        }
    }
}

impl CondensationStrategy for ObservationMaskingStrategy {
    fn name(&self) -> &'static str {
        "observation_masking"
    }

    fn condense(&self, messages: &[Message], _target_tokens: usize) -> Result<Vec<Message>> {
        if messages.len() <= self.protected_recent {
            return Ok(messages.to_vec());
        }

        let boundary = messages.len().saturating_sub(self.protected_recent);
        let mut result = messages.to_vec();

        for msg in result.iter_mut().take(boundary) {
            // Mask tool results
            for tr in &mut msg.tool_results {
                if tr.content.len() > 100 {
                    tr.content = "[MASKED]".to_string();
                }
            }
            // Mask tool role messages (direct tool responses)
            if msg.role == ava_types::Role::Tool && msg.content.len() > 100 {
                msg.content = "[MASKED]".to_string();
            }
        }

        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolResult};

    use super::*;

    #[test]
    fn masks_old_tool_results() {
        let strategy = ObservationMaskingStrategy::new(2);
        let mut msg = Message::new(Role::Assistant, "I'll read the file");
        msg.tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: "x".repeat(200),
            is_error: false,
        });

        let messages = vec![
            msg,
            Message::new(Role::User, "thanks"),
            Message::new(Role::Assistant, "you're welcome"),
        ];

        let result = strategy.condense(&messages, 100).unwrap();
        assert_eq!(result[0].tool_results[0].content, "[MASKED]");
        // Recent messages untouched
        assert_eq!(result[1].content, "thanks");
    }

    #[test]
    fn preserves_short_tool_results() {
        let strategy = ObservationMaskingStrategy::new(1);
        let mut msg = Message::new(Role::Assistant, "done");
        msg.tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: "ok".to_string(), // short, not masked
            is_error: false,
        });

        let messages = vec![msg, Message::new(Role::User, "next")];
        let result = strategy.condense(&messages, 100).unwrap();
        assert_eq!(result[0].tool_results[0].content, "ok");
    }

    #[test]
    fn all_within_protected_window() {
        let strategy = ObservationMaskingStrategy::new(10);
        let messages = vec![
            Message::new(Role::User, "hi"),
            Message::new(Role::Assistant, "hello"),
        ];
        let result = strategy.condense(&messages, 100).unwrap();
        assert_eq!(result.len(), 2);
    }

    #[test]
    fn masks_tool_role_messages() {
        let strategy = ObservationMaskingStrategy::new(1);
        let messages = vec![
            Message::new(Role::Tool, "x".repeat(200)),
            Message::new(Role::User, "got it"),
        ];
        let result = strategy.condense(&messages, 100).unwrap();
        assert_eq!(result[0].content, "[MASKED]");
    }
}
