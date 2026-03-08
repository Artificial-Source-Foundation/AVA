use ava_types::Message;

/// Word-based token estimation (~1.3 tokens per word).
/// Within 15-20% of actual for English text and code.
pub fn estimate_tokens(text: &str) -> usize {
    let word_count = text.split_whitespace().count();
    if word_count == 0 {
        // For empty or whitespace-only, fall back to byte-based for punctuation
        return (text.len() / 4).max(1);
    }
    (word_count * 4 / 3).max(1)
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
    fn estimate_tokens_word_based_accuracy() {
        // "The quick brown fox jumps over the lazy dog" = 9 words
        // GPT-4 tokenizes this to ~9 tokens. Our estimate: 9 * 4/3 = 12
        let text = "The quick brown fox jumps over the lazy dog";
        let est = estimate_tokens(text);
        assert!(est >= 9 && est <= 15, "got {est}, expected 9-15");

        // Code snippet: typically more tokens per word due to punctuation
        let code = "fn main() { println!(\"hello\"); }";
        let est = estimate_tokens(code);
        assert!(est >= 3, "code estimate should be >= 3, got {est}");

        // Empty string
        assert_eq!(estimate_tokens(""), 1);

        // Whitespace-only
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
