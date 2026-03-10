//! AVA Core Types
//!
//! Provides the fundamental types for the AVA system including:
//! - Error types and error handling
//! - Tool definitions and tool calls
//! - Messages, sessions, and context management

pub mod context;
pub mod error;
pub mod message;
pub mod session;
pub mod todo;
pub mod tool;

pub use context::Context;
pub use error::{AvaError, ErrorCategory, Result};
pub use message::{Message, Role};
pub use session::Session;
pub use todo::{TodoItem, TodoPriority, TodoState, TodoStatus};
pub use tool::{Tool, ToolCall, ToolResult};

/// Token usage reported by an LLM provider after a request.
#[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
pub struct TokenUsage {
    pub input_tokens: usize,
    pub output_tokens: usize,
}

/// A chunk from a streaming LLM response.
#[derive(Debug, Clone, Default)]
pub struct StreamChunk {
    /// Text content delta.
    pub content: Option<String>,
    /// Tool call fragment being assembled incrementally.
    pub tool_call: Option<StreamToolCall>,
    /// Token usage metadata (typically only in the final chunk).
    pub usage: Option<TokenUsage>,
    /// Thinking/reasoning content delta.
    pub thinking: Option<String>,
    /// Whether this is the final chunk.
    pub done: bool,
}

/// A partial tool call from streaming chunks.
#[derive(Debug, Clone)]
pub struct StreamToolCall {
    /// Tool call index (for parallel tool calls).
    pub index: usize,
    /// Tool call ID (may arrive in first chunk only).
    pub id: Option<String>,
    /// Tool/function name (may arrive in first chunk only).
    pub name: Option<String>,
    /// Incremental JSON arguments fragment.
    pub arguments_delta: Option<String>,
}

impl StreamChunk {
    pub fn text(s: impl Into<String>) -> Self {
        Self {
            content: Some(s.into()),
            ..Default::default()
        }
    }

    pub fn finished() -> Self {
        Self {
            done: true,
            ..Default::default()
        }
    }

    pub fn with_usage(usage: TokenUsage) -> Self {
        Self {
            usage: Some(usage),
            done: true,
            ..Default::default()
        }
    }

    pub fn text_content(&self) -> Option<&str> {
        self.content.as_deref()
    }
}

/// Thinking/reasoning effort level for models that support extended thinking.
/// Maps to provider-specific parameters (Anthropic adaptive thinking,
/// OpenAI reasoningEffort, Gemini thinkingConfig, etc.)
#[derive(
    Debug, Clone, Copy, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize, Default,
)]
pub enum ThinkingLevel {
    /// No thinking (default behavior)
    #[default]
    Off,
    /// Minimal reasoning
    Low,
    /// Moderate reasoning
    Medium,
    /// Full reasoning
    High,
    /// Maximum reasoning budget
    Max,
}

impl ThinkingLevel {
    /// Cycle to next level: Off → Low → Medium → High → Max → Off
    pub fn cycle(self) -> Self {
        match self {
            Self::Off => Self::Low,
            Self::Low => Self::Medium,
            Self::Medium => Self::High,
            Self::High => Self::Max,
            Self::Max => Self::Off,
        }
    }

    /// Display label for status bar
    pub fn label(self) -> &'static str {
        match self {
            Self::Off => "off",
            Self::Low => "low",
            Self::Medium => "med",
            Self::High => "high",
            Self::Max => "max",
        }
    }

    /// Parse from string (for /think command)
    pub fn from_str_loose(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "off" | "none" | "0" => Some(Self::Off),
            "low" | "l" | "1" | "minimal" => Some(Self::Low),
            "medium" | "med" | "m" | "2" => Some(Self::Medium),
            "high" | "h" | "3" => Some(Self::High),
            "max" | "x" | "xhigh" | "4" => Some(Self::Max),
            _ => None,
        }
    }
}

impl std::fmt::Display for ThinkingLevel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.label())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use uuid::Uuid;

    #[test]
    fn test_uuid_generation() {
        let uuid1 = Uuid::new_v4();
        let uuid2 = Uuid::new_v4();
        assert_ne!(uuid1, uuid2);
    }

    #[test]
    fn stream_chunk_text_helper() {
        let chunk = StreamChunk::text("hello");
        assert_eq!(chunk.text_content(), Some("hello"));
        assert!(!chunk.done);
    }

    #[test]
    fn stream_chunk_finished() {
        let chunk = StreamChunk::finished();
        assert!(chunk.done);
        assert!(chunk.content.is_none());
    }

    #[test]
    fn stream_chunk_with_usage() {
        let usage = TokenUsage {
            input_tokens: 100,
            output_tokens: 50,
        };
        let chunk = StreamChunk::with_usage(usage);
        assert!(chunk.done);
        assert!(chunk.usage.is_some());
        assert_eq!(chunk.usage.unwrap().input_tokens, 100);
    }

    #[test]
    fn test_thinking_level_cycle() {
        assert_eq!(ThinkingLevel::Off.cycle(), ThinkingLevel::Low);
        assert_eq!(ThinkingLevel::Low.cycle(), ThinkingLevel::Medium);
        assert_eq!(ThinkingLevel::Medium.cycle(), ThinkingLevel::High);
        assert_eq!(ThinkingLevel::High.cycle(), ThinkingLevel::Max);
        assert_eq!(ThinkingLevel::Max.cycle(), ThinkingLevel::Off);
    }

    #[test]
    fn test_thinking_level_label() {
        assert_eq!(ThinkingLevel::Off.label(), "off");
        assert_eq!(ThinkingLevel::Low.label(), "low");
        assert_eq!(ThinkingLevel::Medium.label(), "med");
        assert_eq!(ThinkingLevel::High.label(), "high");
        assert_eq!(ThinkingLevel::Max.label(), "max");
    }

    #[test]
    fn test_thinking_level_display() {
        assert_eq!(ThinkingLevel::Off.to_string(), "off");
        assert_eq!(ThinkingLevel::High.to_string(), "high");
    }

    #[test]
    fn test_thinking_level_from_str_loose() {
        assert_eq!(
            ThinkingLevel::from_str_loose("off"),
            Some(ThinkingLevel::Off)
        );
        assert_eq!(
            ThinkingLevel::from_str_loose("none"),
            Some(ThinkingLevel::Off)
        );
        assert_eq!(ThinkingLevel::from_str_loose("0"), Some(ThinkingLevel::Off));

        assert_eq!(
            ThinkingLevel::from_str_loose("low"),
            Some(ThinkingLevel::Low)
        );
        assert_eq!(ThinkingLevel::from_str_loose("l"), Some(ThinkingLevel::Low));
        assert_eq!(ThinkingLevel::from_str_loose("1"), Some(ThinkingLevel::Low));
        assert_eq!(
            ThinkingLevel::from_str_loose("minimal"),
            Some(ThinkingLevel::Low)
        );

        assert_eq!(
            ThinkingLevel::from_str_loose("medium"),
            Some(ThinkingLevel::Medium)
        );
        assert_eq!(
            ThinkingLevel::from_str_loose("med"),
            Some(ThinkingLevel::Medium)
        );
        assert_eq!(
            ThinkingLevel::from_str_loose("m"),
            Some(ThinkingLevel::Medium)
        );
        assert_eq!(
            ThinkingLevel::from_str_loose("2"),
            Some(ThinkingLevel::Medium)
        );

        assert_eq!(
            ThinkingLevel::from_str_loose("high"),
            Some(ThinkingLevel::High)
        );
        assert_eq!(
            ThinkingLevel::from_str_loose("h"),
            Some(ThinkingLevel::High)
        );
        assert_eq!(
            ThinkingLevel::from_str_loose("3"),
            Some(ThinkingLevel::High)
        );

        assert_eq!(
            ThinkingLevel::from_str_loose("max"),
            Some(ThinkingLevel::Max)
        );
        assert_eq!(ThinkingLevel::from_str_loose("x"), Some(ThinkingLevel::Max));
        assert_eq!(
            ThinkingLevel::from_str_loose("xhigh"),
            Some(ThinkingLevel::Max)
        );
        assert_eq!(ThinkingLevel::from_str_loose("4"), Some(ThinkingLevel::Max));

        assert_eq!(ThinkingLevel::from_str_loose("invalid"), None);
        assert_eq!(ThinkingLevel::from_str_loose(""), None);
    }
}
