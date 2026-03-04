use ava_types::Message;

use crate::error::Result;
use crate::strategies::CondensationStrategy;
use crate::token_tracker::estimate_tokens_for_message;

#[derive(Debug, Clone, Default)]
pub struct SlidingWindowStrategy;

impl CondensationStrategy for SlidingWindowStrategy {
    fn name(&self) -> &'static str {
        "sliding_window"
    }

    fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>> {
        let mut selected = Vec::new();
        let mut used = 0_usize;

        for message in messages.iter().rev() {
            let message_tokens = estimate_tokens_for_message(message);
            if used + message_tokens > max_tokens {
                continue;
            }
            used += message_tokens;
            selected.push(message.clone());
        }

        selected.reverse();
        Ok(selected)
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role};

    use super::*;

    #[test]
    fn keeps_latest_messages_within_budget() {
        let strategy = SlidingWindowStrategy;
        let messages = vec![
            Message::new(Role::User, "one"),
            Message::new(Role::Assistant, "two two two two two two"),
            Message::new(Role::User, "three"),
        ];
        let out = strategy.condense(&messages, 6).unwrap();
        assert!(!out.is_empty());
        assert_eq!(out.last().unwrap().content, "three");
    }
}
