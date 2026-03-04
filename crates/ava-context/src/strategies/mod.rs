use ava_types::Message;

use crate::error::Result;

mod sliding_window;
mod tool_truncation;

pub use sliding_window::SlidingWindowStrategy;
pub use tool_truncation::ToolTruncationStrategy;

pub trait CondensationStrategy: Send + Sync {
    fn name(&self) -> &'static str;
    fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>>;
}
