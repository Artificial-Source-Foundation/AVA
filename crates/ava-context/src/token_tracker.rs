use ava_types::Message;

pub fn estimate_tokens(text: &str) -> usize {
    text.chars().count() / 4 + 1
}

pub fn estimate_tokens_for_message(message: &Message) -> usize {
    let mut total = estimate_tokens(&message.content);
    total += message
        .tool_results
        .iter()
        .map(|r| estimate_tokens(&r.content))
        .sum::<usize>();
    total
}

#[derive(Debug, Clone)]
pub struct TokenTracker {
    pub max_tokens: usize,
    pub current_tokens: usize,
}

impl TokenTracker {
    pub fn new(max_tokens: usize) -> Self {
        Self {
            max_tokens,
            current_tokens: 0,
        }
    }

    pub fn add_message(&mut self, message: &Message) {
        self.current_tokens += estimate_tokens_for_message(message);
    }

    pub fn add_messages(&mut self, messages: &[Message]) {
        for message in messages {
            self.add_message(message);
        }
    }

    pub fn is_over_limit(&self) -> bool {
        self.current_tokens > self.max_tokens
    }

    pub fn remaining(&self) -> usize {
        self.max_tokens.saturating_sub(self.current_tokens)
    }

    pub fn reset(&mut self) {
        self.current_tokens = 0;
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role};

    use super::*;

    #[test]
    fn estimate_tokens_returns_small_positive_value() {
        assert!(estimate_tokens("hello world") >= 1);
    }

    #[test]
    fn tracker_updates_token_count() {
        let mut tracker = TokenTracker::new(1000);
        let msg = Message::new(Role::User, "hello world");
        tracker.add_message(&msg);
        assert!(tracker.current_tokens > 0);
        assert!(!tracker.is_over_limit());
    }

    #[test]
    fn tracker_remaining_saturates() {
        let mut tracker = TokenTracker::new(1);
        let msg = Message::new(Role::User, "this is definitely more than four chars");
        tracker.add_message(&msg);
        assert_eq!(tracker.remaining(), 0);
    }
}
