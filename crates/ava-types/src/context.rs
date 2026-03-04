//! Context management for LLM interactions

use crate::message::Message;

#[derive(Debug, Clone)]
pub struct Context {
    pub messages: Vec<Message>,
    pub token_count: usize,
    pub token_limit: usize,
}

impl Context {
    pub fn new(token_limit: usize) -> Self {
        Self {
            messages: Vec::new(),
            token_count: 0,
            token_limit,
        }
    }

    pub fn add_message(&mut self, message: Message) {
        self.messages.push(message);
    }

    pub fn is_within_limit(&self) -> bool {
        self.token_count <= self.token_limit
    }

    pub fn remaining_tokens(&self) -> usize {
        self.token_limit.saturating_sub(self.token_count)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_context_token_management() {
        let mut context = Context::new(1000);
        assert_eq!(context.token_limit, 1000);
        assert_eq!(context.token_count, 0);
        assert!(context.is_within_limit());
        assert_eq!(context.remaining_tokens(), 1000);

        context.token_count = 500;
        assert!(context.is_within_limit());
        assert_eq!(context.remaining_tokens(), 500);

        context.token_count = 1000;
        assert!(context.is_within_limit());
        assert_eq!(context.remaining_tokens(), 0);

        context.token_count = 1001;
        assert!(!context.is_within_limit());
        assert_eq!(context.remaining_tokens(), 0);
    }
}
