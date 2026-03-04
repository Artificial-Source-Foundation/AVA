//! AVA Context - token tracking and condensation orchestration.

pub mod condenser;
pub mod error;
pub mod strategies;
pub mod token_tracker;
pub mod types;

pub use condenser::{create_condenser, create_full_condenser, Condenser};
pub use error::{ContextError, Result};
pub use strategies::{CondensationStrategy, SlidingWindowStrategy, ToolTruncationStrategy};
pub use token_tracker::{estimate_tokens, estimate_tokens_for_message, TokenTracker};
pub use types::{CondensationResult, CondenserConfig, ContextChunk};

pub fn healthcheck() -> bool {
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn healthcheck_returns_true() {
        assert!(healthcheck());
    }
}
