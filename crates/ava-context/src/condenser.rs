use ava_types::Message;

use crate::error::{ContextError, Result};
use crate::strategies::{CondensationStrategy, SlidingWindowStrategy, ToolTruncationStrategy};
use crate::token_tracker::TokenTracker;
use crate::types::{CondensationResult, CondenserConfig};

pub struct Condenser {
    config: CondenserConfig,
    tracker: TokenTracker,
    strategies: Vec<Box<dyn CondensationStrategy>>,
}

impl Condenser {
    pub fn new(config: CondenserConfig, strategies: Vec<Box<dyn CondensationStrategy>>) -> Self {
        Self {
            tracker: TokenTracker::new(config.max_tokens),
            config,
            strategies,
        }
    }

    pub fn condense(&mut self, messages: &[Message]) -> Result<CondensationResult> {
        self.tracker.reset();
        self.tracker.add_messages(messages);

        if !self.tracker.is_over_limit() {
            return Ok(CondensationResult {
                messages: messages.to_vec(),
                estimated_tokens: self.tracker.current_tokens,
                strategy: "none".to_string(),
            });
        }

        let mut current = messages.to_vec();

        for strategy in &self.strategies {
            current = strategy.condense(&current, self.config.target_tokens)?;
            self.tracker.reset();
            self.tracker.add_messages(&current);

            if !self.tracker.is_over_limit() {
                return Ok(CondensationResult {
                    messages: current,
                    estimated_tokens: self.tracker.current_tokens,
                    strategy: strategy.name().to_string(),
                });
            }
        }

        Err(ContextError::TokenBudgetExceeded(
            self.tracker.current_tokens,
            self.config.max_tokens,
        ))
    }
}

pub fn create_condenser(max_tokens: usize) -> Condenser {
    let config = CondenserConfig {
        max_tokens,
        target_tokens: max_tokens.saturating_mul(3) / 4,
        max_tool_content_chars: 2000,
    };
    Condenser::new(
        config,
        vec![
            Box::new(ToolTruncationStrategy::default()),
            Box::new(SlidingWindowStrategy),
        ],
    )
}

pub fn create_full_condenser(config: CondenserConfig) -> Condenser {
    Condenser::new(
        config.clone(),
        vec![
            Box::new(ToolTruncationStrategy::new(config.max_tool_content_chars)),
            Box::new(SlidingWindowStrategy),
        ],
    )
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolResult};

    use super::*;

    #[test]
    fn no_condensation_when_under_limit() {
        let mut condenser = create_condenser(10_000);
        let messages = vec![Message::new(Role::User, "hello")];
        let result = condenser.condense(&messages).unwrap();
        assert_eq!(result.strategy, "none");
        assert_eq!(result.messages.len(), 1);
    }

    #[test]
    fn applies_strategies_when_over_limit() {
        let mut condenser = create_condenser(20);
        let mut messages = vec![Message::new(Role::User, "x".repeat(200))];
        messages[0].tool_results.push(ToolResult {
            call_id: "1".to_string(),
            content: "y".repeat(500),
            is_error: false,
        });

        let result = condenser.condense(&messages).unwrap();
        assert_ne!(result.strategy, "none");
    }
}
