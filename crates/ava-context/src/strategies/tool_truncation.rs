use ava_types::Message;

use crate::error::Result;
use crate::strategies::CondensationStrategy;

#[derive(Debug, Clone)]
pub struct ToolTruncationStrategy {
    max_chars: usize,
}

impl ToolTruncationStrategy {
    pub fn new(max_chars: usize) -> Self {
        Self { max_chars }
    }
}

impl Default for ToolTruncationStrategy {
    fn default() -> Self {
        Self { max_chars: 2000 }
    }
}

impl CondensationStrategy for ToolTruncationStrategy {
    fn name(&self) -> &'static str {
        "tool_truncation"
    }

    fn condense(&self, messages: &[Message], _max_tokens: usize) -> Result<Vec<Message>> {
        let mut out = Vec::with_capacity(messages.len());
        for message in messages {
            let mut cloned = message.clone();
            for result in &mut cloned.tool_results {
                if result.content.chars().count() > self.max_chars {
                    let truncated = result
                        .content
                        .chars()
                        .take(self.max_chars)
                        .collect::<String>();
                    result.content = format!("{truncated}...[truncated]");
                }
            }
            out.push(cloned);
        }
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolResult};

    use super::*;

    #[test]
    fn truncates_long_tool_results() {
        let mut message = Message::new(Role::Tool, "result");
        message.tool_results.push(ToolResult {
            call_id: "call-1".to_string(),
            content: "a".repeat(40),
            is_error: false,
        });

        let strategy = ToolTruncationStrategy::new(10);
        let out = strategy.condense(&[message], 100).unwrap();
        assert!(out[0].tool_results[0].content.ends_with("...[truncated]"));
    }
}
