pub mod error;
pub mod fuzzy_match;
pub mod recovery;
pub mod request;
pub mod strategies;

pub use error::EditError;
pub use fuzzy_match::{FuzzyMatchStrategy, StreamMatch, StreamingMatcher};
pub use recovery::{RecoveryPipeline, RecoveryResult, SelfCorrector};
pub use request::EditRequest;
pub use strategies::{
    BlockAnchorStrategy, EditStrategy, ExactMatchStrategy, FlexibleMatchStrategy,
    IndentationAwareStrategy, LineNumberStrategy, MultiOccurrenceStrategy, RegexMatchStrategy,
    TokenBoundaryStrategy,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EditResult {
    pub content: String,
    pub strategy: String,
}

pub struct EditEngine {
    strategies: Vec<Box<dyn EditStrategy>>,
}

impl Default for EditEngine {
    fn default() -> Self {
        Self {
            strategies: vec![
                Box::new(ExactMatchStrategy),
                Box::new(FlexibleMatchStrategy),
                Box::new(BlockAnchorStrategy),
                Box::new(RegexMatchStrategy),
                Box::new(FuzzyMatchStrategy::new()),
                Box::new(LineNumberStrategy),
                Box::new(TokenBoundaryStrategy),
                Box::new(IndentationAwareStrategy),
                Box::new(MultiOccurrenceStrategy),
            ],
        }
    }
}

impl EditEngine {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn apply(&self, request: &EditRequest) -> Result<EditResult, EditError> {
        for strategy in &self.strategies {
            if let Some(content) = strategy.apply(request)? {
                return Ok(EditResult {
                    content,
                    strategy: strategy.name().to_string(),
                });
            }
        }
        Err(EditError::NoMatch)
    }

    pub fn strategy_count(&self) -> usize {
        self.strategies.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn engine_has_nine_strategies() {
        let engine = EditEngine::new();
        assert_eq!(engine.strategy_count(), 9);
    }

    #[test]
    fn engine_applies_first_matching_strategy() {
        let engine = EditEngine::new();
        let req = EditRequest::new("hello world", "world", "ava");
        let out = engine.apply(&req).unwrap();
        assert_eq!(out.content, "hello ava");
        assert_eq!(out.strategy, "exact_match");
    }
}
