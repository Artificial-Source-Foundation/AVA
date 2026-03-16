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
pub use message::{ImageContent, ImageMediaType, Message, Role};
pub use session::Session;
pub use todo::{TodoItem, TodoPriority, TodoState, TodoStatus};
pub use tool::{Tool, ToolCall, ToolResult};

// --- Context attachment types (B35: @-mention scoping) ---

/// A context attachment resolved from an @-mention in the composer.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum ContextAttachment {
    /// Attach the contents of a specific file.
    File { path: std::path::PathBuf },
    /// Attach a listing of files in a directory.
    Folder { path: std::path::PathBuf },
    /// Run a codebase search and attach results.
    CodebaseQuery { query: String },
}

impl ContextAttachment {
    /// Short display label for badges (e.g. "src/main.rs", "src/", "search:foo").
    pub fn label(&self) -> String {
        match self {
            Self::File { path } => path.display().to_string(),
            Self::Folder { path } => format!("{}/", path.display()),
            Self::CodebaseQuery { query } => format!("search:{query}"),
        }
    }

    /// Prefix used in the composer text (e.g. "@file:", "@folder:", "@codebase:").
    pub fn mention_prefix(&self) -> &'static str {
        match self {
            Self::File { .. } => "@file:",
            Self::Folder { .. } => "@folder:",
            Self::CodebaseQuery { .. } => "@codebase:",
        }
    }
}

/// Parse @-mention strings from user input text.
/// Returns (attachments, cleaned_text) where cleaned_text has @mentions stripped.
///
/// Recognized forms:
///   `@file:path/to/file`   — explicit file
///   `@folder:path/to/dir`  — explicit folder
///   `@codebase:query`      — codebase search
///   `@path/to/file.rs`     — bare file (must contain `/` or `.`)
///   `@path/to/dir/`        — bare folder (trailing `/`)
pub fn parse_mentions(text: &str) -> (Vec<ContextAttachment>, String) {
    let mut attachments = Vec::new();
    let mut cleaned_parts: Vec<&str> = Vec::new();

    // Split by whitespace, preserving positions for reconstruction
    let mut last_end = 0;
    let words: Vec<(usize, &str)> = text
        .split_whitespace()
        .map(|word| {
            let start = text[last_end..]
                .find(word)
                .expect("word from split_whitespace must exist in source")
                + last_end;
            last_end = start + word.len();
            (start, word)
        })
        .collect();

    for (_start, word) in &words {
        if let Some(rest) = word.strip_prefix('@') {
            if let Some(attachment) = try_parse_mention(rest) {
                attachments.push(attachment);
                continue; // Skip this word in cleaned output
            }
        }
        cleaned_parts.push(word);
    }

    let cleaned = cleaned_parts.join(" ");
    (attachments, cleaned)
}

fn try_parse_mention(token: &str) -> Option<ContextAttachment> {
    // Explicit prefixes
    if let Some(path_str) = token.strip_prefix("file:") {
        if !path_str.is_empty() {
            return Some(ContextAttachment::File {
                path: std::path::PathBuf::from(path_str),
            });
        }
    } else if let Some(path_str) = token.strip_prefix("folder:") {
        if !path_str.is_empty() {
            return Some(ContextAttachment::Folder {
                path: std::path::PathBuf::from(path_str),
            });
        }
    } else if let Some(query) = token.strip_prefix("codebase:") {
        if !query.is_empty() {
            return Some(ContextAttachment::CodebaseQuery {
                query: query.to_string(),
            });
        }
    } else if !token.is_empty() && (token.contains('/') || token.contains('.')) {
        // Bare mention — must look like a path (contains / or .)
        if token.ends_with('/') {
            return Some(ContextAttachment::Folder {
                path: std::path::PathBuf::from(token.trim_end_matches('/')),
            });
        }
        return Some(ContextAttachment::File {
            path: std::path::PathBuf::from(token),
        });
    }
    None
}

// --- Mid-stream messaging types ---

/// Tier classification for messages sent while the agent is running.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MessageTier {
    /// Tier 1: High-priority steering — injected after current tool, skips remaining tools.
    Steering,
    /// Tier 2: Follow-up — injected after agent finishes current task.
    FollowUp,
    /// Tier 3: Post-complete — runs in grouped pipeline stages after agent says "done".
    PostComplete { group: u32 },
}

/// A user message queued for delivery to the agent while it is running.
#[derive(Debug, Clone)]
pub struct QueuedMessage {
    pub text: String,
    pub tier: MessageTier,
}

/// Token usage reported by an LLM provider after a request.
#[derive(Debug, Clone, Default, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct TokenUsage {
    pub input_tokens: usize,
    pub output_tokens: usize,
    /// Tokens read from prompt cache (Anthropic `cache_read_input_tokens`, OpenAI `cached_tokens`).
    #[serde(default)]
    pub cache_read_tokens: usize,
    /// Tokens written to prompt cache (Anthropic `cache_creation_input_tokens`).
    #[serde(default)]
    pub cache_creation_tokens: usize,
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

    /// Binary toggle: Off ↔ High (for models that don't support granular levels).
    pub fn cycle_binary(self) -> Self {
        match self {
            Self::Off => Self::High,
            _ => Self::Off,
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
            ..Default::default()
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

    // --- Context attachment / mention parsing tests ---

    #[test]
    fn parse_file_mention() {
        let (attachments, cleaned) = parse_mentions("fix @file:src/main.rs please");
        assert_eq!(attachments.len(), 1);
        assert_eq!(
            attachments[0],
            ContextAttachment::File {
                path: std::path::PathBuf::from("src/main.rs")
            }
        );
        assert_eq!(cleaned, "fix please");
    }

    #[test]
    fn parse_bare_file_mention() {
        let (attachments, cleaned) = parse_mentions("look at @src/lib.rs");
        assert_eq!(attachments.len(), 1);
        assert_eq!(
            attachments[0],
            ContextAttachment::File {
                path: std::path::PathBuf::from("src/lib.rs")
            }
        );
        assert_eq!(cleaned, "look at");
    }

    #[test]
    fn parse_folder_mention() {
        let (attachments, cleaned) = parse_mentions("review @folder:src/widgets please");
        assert_eq!(attachments.len(), 1);
        assert_eq!(
            attachments[0],
            ContextAttachment::Folder {
                path: std::path::PathBuf::from("src/widgets")
            }
        );
        assert_eq!(cleaned, "review please");
    }

    #[test]
    fn parse_bare_folder_mention_trailing_slash() {
        let (attachments, cleaned) = parse_mentions("check @src/widgets/");
        assert_eq!(attachments.len(), 1);
        assert_eq!(
            attachments[0],
            ContextAttachment::Folder {
                path: std::path::PathBuf::from("src/widgets")
            }
        );
        assert_eq!(cleaned, "check");
    }

    #[test]
    fn parse_codebase_mention() {
        let (attachments, cleaned) = parse_mentions("find @codebase:error_handling patterns");
        assert_eq!(attachments.len(), 1);
        assert_eq!(
            attachments[0],
            ContextAttachment::CodebaseQuery {
                query: "error_handling".to_string()
            }
        );
        assert_eq!(cleaned, "find patterns");
    }

    #[test]
    fn parse_multiple_mentions() {
        let (attachments, cleaned) = parse_mentions("compare @file:a.rs and @file:b.rs");
        assert_eq!(attachments.len(), 2);
        assert_eq!(
            attachments[0],
            ContextAttachment::File {
                path: std::path::PathBuf::from("a.rs")
            }
        );
        assert_eq!(
            attachments[1],
            ContextAttachment::File {
                path: std::path::PathBuf::from("b.rs")
            }
        );
        assert_eq!(cleaned, "compare and");
    }

    #[test]
    fn parse_no_mentions() {
        let (attachments, cleaned) = parse_mentions("just a normal message");
        assert!(attachments.is_empty());
        assert_eq!(cleaned, "just a normal message");
    }

    #[test]
    fn parse_email_not_a_mention() {
        // @ followed by nothing valid should pass through
        let (attachments, cleaned) = parse_mentions("contact user@ for info");
        assert!(attachments.is_empty());
        assert_eq!(cleaned, "contact user@ for info");
    }

    #[test]
    fn context_attachment_label() {
        let f = ContextAttachment::File {
            path: std::path::PathBuf::from("src/main.rs"),
        };
        assert_eq!(f.label(), "src/main.rs");

        let d = ContextAttachment::Folder {
            path: std::path::PathBuf::from("src"),
        };
        assert_eq!(d.label(), "src/");

        let q = ContextAttachment::CodebaseQuery {
            query: "error".to_string(),
        };
        assert_eq!(q.label(), "search:error");
    }
}
