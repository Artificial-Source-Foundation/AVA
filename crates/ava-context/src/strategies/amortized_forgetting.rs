//! Amortized forgetting strategy (BG2-12, inspired by OpenHands).
//!
//! Keeps the first N messages (system prompt + initial context) and the
//! last M messages (recent conversation), dropping everything in between.
//! This is a simple but effective way to reduce context when other
//! strategies haven't reduced it enough.

use ava_types::Message;

use super::CondensationStrategy;
use crate::error::Result;

/// Keeps first `keep_first` + last `keep_last` messages, drops the middle.
pub struct AmortizedForgettingStrategy {
    /// Number of messages to keep from the start (system prompt, initial context).
    keep_first: usize,
    /// Number of messages to keep from the end (recent conversation).
    keep_last: usize,
}

impl AmortizedForgettingStrategy {
    pub fn new(keep_first: usize, keep_last: usize) -> Self {
        Self {
            keep_first,
            keep_last,
        }
    }
}

impl Default for AmortizedForgettingStrategy {
    fn default() -> Self {
        Self {
            keep_first: 2,
            keep_last: 8,
        }
    }
}

impl CondensationStrategy for AmortizedForgettingStrategy {
    fn name(&self) -> &'static str {
        "amortized_forgetting"
    }

    fn condense(&self, messages: &[Message], _target_tokens: usize) -> Result<Vec<Message>> {
        let total = messages.len();

        // If we can keep everything, do so
        if total <= self.keep_first + self.keep_last {
            return Ok(messages.to_vec());
        }

        let mut result = Vec::with_capacity(self.keep_first + self.keep_last + 1);

        // Keep first N
        result.extend_from_slice(&messages[..self.keep_first]);

        // Insert a marker message indicating dropped content
        let dropped_count = total - self.keep_first - self.keep_last;
        let marker = Message::new(
            ava_types::Role::System,
            format!(
                "[{dropped_count} messages omitted from conversation history to manage context size]"
            ),
        );
        result.push(marker);

        // Keep last M
        result.extend_from_slice(&messages[total - self.keep_last..]);

        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role};

    use super::*;

    #[test]
    fn keeps_all_when_under_threshold() {
        let strategy = AmortizedForgettingStrategy::new(2, 3);
        let messages: Vec<Message> = (0..4)
            .map(|i| Message::new(Role::User, format!("msg{i}")))
            .collect();

        let result = strategy.condense(&messages, 100).unwrap();
        assert_eq!(result.len(), 4);
    }

    #[test]
    fn drops_middle_messages() {
        let strategy = AmortizedForgettingStrategy::new(2, 2);
        let messages: Vec<Message> = (0..10)
            .map(|i| Message::new(Role::User, format!("msg{i}")))
            .collect();

        let result = strategy.condense(&messages, 100).unwrap();
        // 2 first + 1 marker + 2 last = 5
        assert_eq!(result.len(), 5);
        assert_eq!(result[0].content, "msg0");
        assert_eq!(result[1].content, "msg1");
        assert!(result[2].content.contains("6 messages omitted"));
        assert_eq!(result[3].content, "msg8");
        assert_eq!(result[4].content, "msg9");
    }

    #[test]
    fn exact_boundary() {
        let strategy = AmortizedForgettingStrategy::new(3, 3);
        let messages: Vec<Message> = (0..6)
            .map(|i| Message::new(Role::User, format!("msg{i}")))
            .collect();

        let result = strategy.condense(&messages, 100).unwrap();
        assert_eq!(result.len(), 6); // exactly at boundary, no drop
    }

    #[test]
    fn default_values() {
        let strategy = AmortizedForgettingStrategy::default();
        assert_eq!(strategy.keep_first, 2);
        assert_eq!(strategy.keep_last, 8);
    }
}
