use async_trait::async_trait;
use ava_types::Message;

use crate::error::Result;

pub mod relevance;
mod sliding_window;
pub mod summarization;
mod tool_truncation;

pub use relevance::RelevanceStrategy;
pub use sliding_window::SlidingWindowStrategy;
pub use summarization::SummarizationStrategy;
pub use tool_truncation::ToolTruncationStrategy;

/// Strategy for condensing a conversation to fit within a token budget.
///
/// Implementations reduce message history while preserving important context.
/// The 3-stage pipeline applies strategies in order: tool truncation → sliding
/// window → summarization/relevance scoring.
pub trait CondensationStrategy: Send + Sync {
    /// Strategy name for logging and diagnostics.
    fn name(&self) -> &'static str;
    /// Condense `messages` to fit within `max_tokens`, returning the reduced set.
    fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>>;
}

/// Async variant of `CondensationStrategy` for strategies that require async
/// operations (e.g., LLM-based summarization).
#[async_trait]
pub trait AsyncCondensationStrategy: Send + Sync {
    fn name(&self) -> &'static str;
    async fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>>;
    /// Set a previous summary for iterative compaction. Default is no-op.
    fn set_previous_summary(&mut self, _summary: Option<String>) {}
}

/// Minimal trait for LLM-based summarization.
/// Defined here to avoid ava-context depending on ava-llm directly.
/// The agent stack wraps an `LLMProvider` into this.
#[async_trait]
pub trait Summarizer: Send + Sync {
    async fn summarize(&self, text: &str) -> std::result::Result<String, String>;
}
