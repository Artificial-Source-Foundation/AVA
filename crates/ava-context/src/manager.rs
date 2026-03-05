use ava_types::{Message, Role, ToolResult};

use crate::condenser::{create_condenser, Condenser};
use crate::token_tracker::TokenTracker;
use crate::Result;

pub struct ContextManager {
    messages: Vec<Message>,
    token_limit: usize,
    tracker: TokenTracker,
    condenser: Condenser,
}

impl ContextManager {
    pub fn new(token_limit: usize) -> Self {
        Self {
            messages: Vec::new(),
            token_limit,
            tracker: TokenTracker::new(token_limit),
            condenser: create_condenser(token_limit),
        }
    }

    pub fn add_message(&mut self, message: Message) {
        self.tracker.add_message(&message);
        self.messages.push(message);
    }

    pub fn add_tool_result(&mut self, result: ToolResult) {
        let message =
            Message::new(Role::Tool, result.content.clone()).with_tool_results(vec![result]);
        self.add_message(message);
    }

    pub fn get_messages(&self) -> &[Message] {
        &self.messages
    }

    pub fn token_count(&self) -> usize {
        self.tracker.current_tokens
    }

    pub fn should_compact(&self) -> bool {
        self.token_count() > self.token_limit.saturating_mul(4) / 5
    }

    pub fn compact(&mut self) -> Result<()> {
        let condensed = self.condenser.condense(&self.messages)?;
        self.messages = condensed.messages;
        self.tracker.reset();
        self.tracker.add_messages(&self.messages);
        Ok(())
    }

    pub fn get_system_message(&self) -> Option<&Message> {
        self.messages
            .iter()
            .find(|message| message.role == Role::System)
    }
}
