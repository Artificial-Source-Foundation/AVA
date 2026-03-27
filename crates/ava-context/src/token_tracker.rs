use ava_types::Message;

use crate::tokenizer;

/// Accurate BPE token count using cl100k_base encoding.
///
/// Uses tiktoken for precise token counting. Returns at least 1 for
/// non-empty text (matching the old API contract).
pub fn estimate_tokens(text: &str) -> usize {
    if text.is_empty() {
        1
    } else {
        tokenizer::count_tokens_default(text).max(1)
    }
}

/// Model-aware BPE token count.
///
/// Selects the appropriate encoding (cl100k_base or o200k_base) based on
/// the model name and returns a precise token count.
pub fn estimate_tokens_for_model(text: &str, model: &str) -> usize {
    if text.is_empty() {
        1
    } else {
        tokenizer::count_tokens_for_model(text, model).max(1)
    }
}

pub fn estimate_tokens_for_message(message: &Message) -> usize {
    let mut total = estimate_tokens(&message.content);
    // Message overhead: role token + delimiters (~4 tokens)
    total += 4;
    // Tool call metadata overhead
    total += message
        .tool_calls
        .iter()
        .map(|tc| estimate_tokens(&tc.name) + estimate_tokens(&tc.arguments.to_string()) + 3)
        .sum::<usize>();
    // Tool results
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
    fn estimate_tokens_bpe_accuracy() {
        // "The quick brown fox jumps over the lazy dog" = 9 tokens in cl100k_base
        let text = "The quick brown fox jumps over the lazy dog";
        let est = estimate_tokens(text);
        assert_eq!(est, 9, "BPE should count exactly 9 tokens, got {est}");

        // Code snippet: BPE handles punctuation precisely
        let code = "fn main() { println!(\"hello\"); }";
        let est = estimate_tokens(code);
        assert!(
            (8..=13).contains(&est),
            "code estimate {est} out of expected range"
        );

        // Empty string — returns 1 (API contract: always >= 1)
        assert_eq!(estimate_tokens(""), 1);

        // Whitespace-only — returns 1
        assert_eq!(estimate_tokens("   "), 1);
    }

    #[test]
    fn estimate_tokens_for_message_includes_overhead() {
        let msg = Message::new(Role::User, "hello");
        let tokens = estimate_tokens_for_message(&msg);
        // Should include content tokens + 4 overhead
        assert!(tokens > estimate_tokens("hello"));
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
