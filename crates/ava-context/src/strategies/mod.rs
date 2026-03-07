use async_trait::async_trait;
use ava_types::Message;

use crate::error::Result;

mod sliding_window;
pub mod summarization;
mod tool_truncation;

pub use sliding_window::SlidingWindowStrategy;
pub use summarization::SummarizationStrategy;
pub use tool_truncation::ToolTruncationStrategy;

pub trait CondensationStrategy: Send + Sync {
    fn name(&self) -> &'static str;
    fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>>;
}

/// Async variant of `CondensationStrategy` for strategies that require async
/// operations (e.g., LLM-based summarization).
#[async_trait]
pub trait AsyncCondensationStrategy: Send + Sync {
    fn name(&self) -> &'static str;
    async fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>>;
}

/// Minimal trait for LLM-based summarization.
/// Defined here to avoid ava-context depending on ava-llm directly.
/// The agent stack wraps an `LLMProvider` into this.
#[async_trait]
pub trait Summarizer: Send + Sync {
    async fn summarize(&self, text: &str) -> std::result::Result<String, String>;
}
