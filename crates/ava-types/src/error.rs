//! Error types for the AVA system

use serde::{Deserialize, Serialize};
use thiserror::Error;

pub type Result<T> = std::result::Result<T, AvaError>;

/// Unified error type for the AVA system.
///
/// Errors are organized into structured variants (with typed fields) and legacy
/// string-payload variants (retained for backward compatibility). Use structured
/// variants for new code. All variants support categorization via [`ErrorCategory`],
/// retryability checking, and user-friendly message generation.
#[derive(Error, Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum AvaError {
    // ── Structured provider/LLM errors ──────────────────────────────────
    /// Generic provider API error (non-retryable).
    #[error("Provider '{provider}' error: {message}")]
    ProviderError { provider: String, message: String },

    /// No API key found for the given provider in credentials.
    #[error("No API key configured for provider '{provider}'. Add your key to ~/.ava/credentials.json under providers.{provider}.api_key")]
    MissingApiKey { provider: String },

    /// Provider returned HTTP 429 — retryable after the specified delay.
    #[error("Rate limited by {provider}. Retry after {retry_after_secs}s")]
    RateLimited {
        provider: String,
        retry_after_secs: u64,
    },

    /// Requested model does not exist for the provider.
    #[error("Model '{model}' not found for provider '{provider}'")]
    ModelNotFound { provider: String, model: String },

    /// Circuit breaker is open — provider had too many consecutive failures.
    #[error("Provider '{provider}' is temporarily unavailable (circuit breaker open). Retry after cooldown")]
    ProviderUnavailable { provider: String },

    // ── Structured tool errors ──────────────────────────────────────────
    /// Tool name not found in the registry.
    #[error("Tool '{tool}' not found. Available tools: {available}")]
    ToolNotFound { tool: String, available: String },

    /// Tool execution returned an error.
    #[error("Tool '{tool}' execution failed: {message}")]
    ToolExecutionError { tool: String, message: String },

    /// Tool execution exceeded its time budget — retryable.
    #[error("Tool '{tool}' timed out after {timeout_secs}s")]
    ToolTimeout { tool: String, timeout_secs: u64 },

    // ── Structured context errors ───────────────────────────────────────
    /// Message history exceeds the model's context window.
    #[error("Context window exceeded ({current} tokens > {limit} tokens). Consider using a model with larger context or breaking the task into smaller parts")]
    ContextWindowExceeded { current: usize, limit: usize },

    // ── Structured agent errors ─────────────────────────────────────────
    /// Agent loop terminated (e.g., max turns, cost threshold, smart completion).
    #[error("Agent loop stopped: {reason}")]
    AgentStopped { reason: String },

    /// User pressed Ctrl+C or otherwise cancelled the run.
    #[error("Agent run cancelled by user")]
    Cancelled,

    // ── Legacy string-payload variants (backward compat) ────────────────
    //
    // These variants are **retained for backward compatibility** only.
    // New code should use the structured variants above (e.g. `ToolExecutionError`,
    // `NotFound`, `PermissionDenied`) which preserve machine-readable fields and
    // participate correctly in `is_retryable()` and `category()`.
    //
    // Migration guide:
    //   AvaError::ToolError(msg)        → AvaError::ToolExecutionError { tool, message }
    //   AvaError::IoError(msg)          → AvaError::NotFound / AvaError::PermissionDenied
    //                                     / AvaError::TimeoutError (when kind is known);
    //                                     IoError only when kind is truly generic
    //   AvaError::ConfigError(msg)      → callers that know the provider: MissingApiKey
    //   AvaError::NotFound(msg)         → AvaError::ModelNotFound or keep as-is if not a model
    /// Generic tool error.
    ///
    /// # Deprecated
    /// Prefer `ToolExecutionError { tool, message }` for new code. This variant
    /// is retained for backward compatibility with existing call sites.
    #[error("Tool execution failed: {0}")]
    ToolError(String),

    /// I/O error (file system, network socket, pipe) — kind not preserved.
    ///
    /// # Deprecated
    /// This variant discards the underlying `io::ErrorKind`. When the kind is
    /// known at the call site, use `NotFound`, `PermissionDenied`, or
    /// `TimeoutError` directly. `From<io::Error>` now maps common kinds to those
    /// structured variants and falls back to `IoError` only for unknown kinds.
    #[error("IO error: {0}")]
    IoError(String),

    /// JSON/YAML serialization or deserialization failure.
    #[error("Serialization error: {0}")]
    SerializationError(String),

    /// OS-level or platform abstraction error.
    ///
    /// # Deprecated
    /// For I/O-derived platform errors prefer constructing `NotFound`,
    /// `PermissionDenied`, or `TimeoutError` directly.
    #[error("Platform error: {0}")]
    PlatformError(String),

    /// Configuration file or value error.
    ///
    /// # Deprecated
    /// When the provider is known at the call site, prefer `MissingApiKey`.
    #[error("Configuration error: {0}")]
    ConfigError(String),

    /// Input validation failure.
    #[error("Validation error: {0}")]
    ValidationError(String),

    /// SQLite or other database error.
    #[error("Database error: {0}")]
    DatabaseError(String),

    /// Operation exceeded its time budget — retryable.
    #[error("Timeout error: {0}")]
    TimeoutError(String),

    /// Requested resource not found.
    ///
    /// # Note
    /// For model-not-found errors where the provider is known, prefer
    /// `ModelNotFound { provider, model }`.
    #[error("Not found: {0}")]
    NotFound(String),

    /// Action blocked by permission policy or inspector.
    #[error("Permission denied: {0}")]
    PermissionDenied(String),
}

/// Error category for grouping related errors
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorCategory {
    Tool,
    System,
    Data,
    Config,
    Validation,
    Database,
    Timeout,
    NotFound,
    Permission,
    Provider,
    Agent,
}

impl AvaError {
    /// Get the category for this error
    pub fn category(&self) -> ErrorCategory {
        match self {
            AvaError::ToolError(_)
            | AvaError::ToolExecutionError { .. }
            | AvaError::ToolNotFound { .. }
            | AvaError::ToolTimeout { .. } => ErrorCategory::Tool,

            AvaError::IoError(_) | AvaError::PlatformError(_) => ErrorCategory::System,
            AvaError::SerializationError(_) => ErrorCategory::Data,

            AvaError::ConfigError(_) | AvaError::MissingApiKey { .. } => ErrorCategory::Config,

            AvaError::ValidationError(_) | AvaError::ContextWindowExceeded { .. } => {
                ErrorCategory::Validation
            }

            AvaError::DatabaseError(_) => ErrorCategory::Database,
            AvaError::TimeoutError(_) => ErrorCategory::Timeout,

            AvaError::NotFound(_) | AvaError::ModelNotFound { .. } => ErrorCategory::NotFound,

            AvaError::PermissionDenied(_) => ErrorCategory::Permission,

            AvaError::ProviderError { .. }
            | AvaError::RateLimited { .. }
            | AvaError::ProviderUnavailable { .. } => ErrorCategory::Provider,

            AvaError::AgentStopped { .. } | AvaError::Cancelled => ErrorCategory::Agent,
        }
    }

    /// Check if this error is retryable
    pub fn is_retryable(&self) -> bool {
        matches!(
            self,
            AvaError::TimeoutError(_)
                | AvaError::DatabaseError(_)
                | AvaError::PlatformError(_)
                | AvaError::RateLimited { .. }
                | AvaError::ToolTimeout { .. }
                | AvaError::ProviderUnavailable { .. }
        )
    }

    /// Get a user-friendly error message
    pub fn user_message(&self) -> String {
        match self {
            AvaError::ProviderError { provider, message } => {
                format!("{provider} error: {message}")
            }
            AvaError::MissingApiKey { provider } => {
                format!(
                    "No API key for {provider}. Add your key to ~/.ava/credentials.json \
                     under providers.{provider}.api_key"
                )
            }
            AvaError::RateLimited {
                provider,
                retry_after_secs,
            } => {
                format!("Rate limited by {provider}. Retry after {retry_after_secs}s")
            }
            AvaError::ModelNotFound { provider, model } => {
                format!("Model '{model}' not found for {provider}")
            }
            AvaError::ToolNotFound { tool, available } => {
                format!("Tool '{tool}' not found. Available: {available}")
            }
            AvaError::ToolExecutionError { tool, message } => {
                format!("Tool '{tool}' failed: {message}")
            }
            AvaError::ToolTimeout { tool, timeout_secs } => {
                format!("Tool '{tool}' timed out after {timeout_secs}s")
            }
            AvaError::ContextWindowExceeded { current, limit } => {
                format!(
                    "Context window exceeded ({current} > {limit} tokens). \
                     Use a larger model or break the task into smaller parts"
                )
            }
            AvaError::ProviderUnavailable { provider } => {
                format!("{provider} is temporarily unavailable (circuit breaker open)")
            }
            AvaError::AgentStopped { reason } => format!("Agent stopped: {reason}"),
            AvaError::Cancelled => "Agent run cancelled by user".to_string(),
            AvaError::ToolError(msg) => format!("Tool failed: {msg}"),
            AvaError::IoError(msg) => format!("I/O error: {msg}"),
            AvaError::SerializationError(msg) => format!("Data error: {msg}"),
            AvaError::PlatformError(msg) => format!("System error: {msg}"),
            AvaError::ConfigError(msg) => format!("Configuration error: {msg}"),
            AvaError::ValidationError(msg) => format!("Validation failed: {msg}"),
            AvaError::DatabaseError(msg) => format!("Database error: {msg}"),
            AvaError::TimeoutError(msg) => format!("Operation timed out: {msg}"),
            AvaError::NotFound(msg) => format!("Not found: {msg}"),
            AvaError::PermissionDenied(msg) => format!("Permission denied: {msg}"),
        }
    }
}

impl From<std::io::Error> for AvaError {
    /// Convert an `io::Error` to `AvaError`, preserving `ErrorKind` where possible.
    ///
    /// | `io::ErrorKind`        | `AvaError` variant          |
    /// |------------------------|-----------------------------|
    /// | `NotFound`             | `NotFound`                  |
    /// | `PermissionDenied`     | `PermissionDenied`          |
    /// | `TimedOut`             | `TimeoutError` (retryable)  |
    /// | `WouldBlock`           | `TimeoutError` (retryable)  |
    /// | anything else          | `IoError` (legacy fallback) |
    fn from(err: std::io::Error) -> Self {
        match err.kind() {
            std::io::ErrorKind::NotFound => AvaError::NotFound(err.to_string()),
            std::io::ErrorKind::PermissionDenied => AvaError::PermissionDenied(err.to_string()),
            std::io::ErrorKind::TimedOut => AvaError::TimeoutError(err.to_string()),
            std::io::ErrorKind::WouldBlock => AvaError::TimeoutError(err.to_string()),
            _ => AvaError::IoError(err.to_string()),
        }
    }
}

impl From<serde_json::Error> for AvaError {
    fn from(err: serde_json::Error) -> Self {
        AvaError::SerializationError(err.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_conversions() {
        // NotFound io::Error → AvaError::NotFound (preserves kind)
        let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "file not found");
        let ava_err: AvaError = io_err.into();
        match ava_err {
            AvaError::NotFound(_) => (),
            _ => panic!("Expected NotFound variant for NotFound io::Error"),
        }

        // PermissionDenied io::Error → AvaError::PermissionDenied (preserves kind)
        let io_err = std::io::Error::new(std::io::ErrorKind::PermissionDenied, "access denied");
        let ava_err: AvaError = io_err.into();
        match ava_err {
            AvaError::PermissionDenied(_) => (),
            _ => panic!("Expected PermissionDenied variant for PermissionDenied io::Error"),
        }

        // TimedOut io::Error → AvaError::TimeoutError (retryable)
        let io_err = std::io::Error::new(std::io::ErrorKind::TimedOut, "timed out");
        let ava_err: AvaError = io_err.into();
        match &ava_err {
            AvaError::TimeoutError(_) => (),
            _ => panic!("Expected TimeoutError variant for TimedOut io::Error"),
        }
        assert!(
            ava_err.is_retryable(),
            "TimedOut io::Error should be retryable"
        );

        // Generic (BrokenPipe) io::Error → AvaError::IoError (legacy fallback)
        let io_err = std::io::Error::new(std::io::ErrorKind::BrokenPipe, "broken pipe");
        let ava_err: AvaError = io_err.into();
        match ava_err {
            AvaError::IoError(_) => (),
            _ => panic!("Expected IoError variant for generic io::Error"),
        }

        let json_err = serde_json::from_str::<serde_json::Value>("invalid json").unwrap_err();
        let ava_err: AvaError = json_err.into();
        match ava_err {
            AvaError::SerializationError(_) => (),
            _ => panic!("Expected SerializationError variant"),
        }
    }

    #[test]
    fn test_error_category() {
        let err = AvaError::DatabaseError("test".to_string());
        assert_eq!(err.category(), ErrorCategory::Database);

        let err = AvaError::TimeoutError("test".to_string());
        assert_eq!(err.category(), ErrorCategory::Timeout);
    }

    #[test]
    fn test_error_is_retryable() {
        assert!(AvaError::TimeoutError("test".to_string()).is_retryable());
        assert!(AvaError::DatabaseError("test".to_string()).is_retryable());
        assert!(!AvaError::ValidationError("test".to_string()).is_retryable());
    }

    #[test]
    fn test_error_user_message() {
        let err = AvaError::NotFound("file.txt".to_string());
        assert!(err.user_message().contains("file.txt"));
    }

    // ── New variant tests ───────────────────────────────────────────────

    #[test]
    fn test_structured_provider_errors() {
        let err = AvaError::MissingApiKey {
            provider: "anthropic".to_string(),
        };
        assert_eq!(err.category(), ErrorCategory::Config);
        assert!(!err.is_retryable());
        assert!(err.user_message().contains("anthropic"));

        let err = AvaError::RateLimited {
            provider: "openai".to_string(),
            retry_after_secs: 30,
        };
        assert_eq!(err.category(), ErrorCategory::Provider);
        assert!(err.is_retryable());
        assert!(err.to_string().contains("30s"));

        let err = AvaError::ModelNotFound {
            provider: "anthropic".to_string(),
            model: "claude-99".to_string(),
        };
        assert_eq!(err.category(), ErrorCategory::NotFound);
        assert!(err.to_string().contains("claude-99"));

        let err = AvaError::ProviderError {
            provider: "openai".to_string(),
            message: "server error".to_string(),
        };
        assert_eq!(err.category(), ErrorCategory::Provider);
        assert!(!err.is_retryable());
    }

    #[test]
    fn test_structured_tool_errors() {
        let err = AvaError::ToolNotFound {
            tool: "magic".to_string(),
            available: "read, write, bash".to_string(),
        };
        assert_eq!(err.category(), ErrorCategory::Tool);
        assert!(err.to_string().contains("magic"));
        assert!(err.to_string().contains("read, write, bash"));

        let err = AvaError::ToolExecutionError {
            tool: "bash".to_string(),
            message: "exit code 1".to_string(),
        };
        assert_eq!(err.category(), ErrorCategory::Tool);

        let err = AvaError::ToolTimeout {
            tool: "bash".to_string(),
            timeout_secs: 120,
        };
        assert!(err.is_retryable());
        assert!(err.to_string().contains("120s"));
    }

    #[test]
    fn test_structured_agent_errors() {
        let err = AvaError::AgentStopped {
            reason: "cost threshold".to_string(),
        };
        assert_eq!(err.category(), ErrorCategory::Agent);
        assert!(!err.is_retryable());

        let err = AvaError::Cancelled;
        assert_eq!(err.category(), ErrorCategory::Agent);
        assert_eq!(err.to_string(), "Agent run cancelled by user");
    }

    #[test]
    fn error_display_is_human_readable() {
        let errors: Vec<AvaError> = vec![
            AvaError::ToolError("test".to_string()),
            AvaError::IoError("test".to_string()),
            AvaError::SerializationError("test".to_string()),
            AvaError::PlatformError("test".to_string()),
            AvaError::ConfigError("test".to_string()),
            AvaError::ValidationError("test".to_string()),
            AvaError::DatabaseError("test".to_string()),
            AvaError::TimeoutError("test".to_string()),
            AvaError::NotFound("test".to_string()),
            AvaError::PermissionDenied("test".to_string()),
            AvaError::ProviderError {
                provider: "test".to_string(),
                message: "fail".to_string(),
            },
            AvaError::MissingApiKey {
                provider: "test".to_string(),
            },
            AvaError::RateLimited {
                provider: "test".to_string(),
                retry_after_secs: 10,
            },
            AvaError::ModelNotFound {
                provider: "test".to_string(),
                model: "gpt-5".to_string(),
            },
            AvaError::ToolNotFound {
                tool: "magic".to_string(),
                available: "read".to_string(),
            },
            AvaError::ToolExecutionError {
                tool: "bash".to_string(),
                message: "oops".to_string(),
            },
            AvaError::ToolTimeout {
                tool: "bash".to_string(),
                timeout_secs: 60,
            },
            AvaError::ContextWindowExceeded {
                current: 200_000,
                limit: 128_000,
            },
            AvaError::ProviderUnavailable {
                provider: "test".to_string(),
            },
            AvaError::AgentStopped {
                reason: "cost".to_string(),
            },
            AvaError::Cancelled,
        ];

        for err in &errors {
            let display = format!("{err}");
            assert!(
                !display.contains("AvaError::"),
                "Display for {err:?} should not contain 'AvaError::': got '{display}'"
            );
            assert!(!display.is_empty(), "Display should not be empty");
        }
    }

    #[test]
    fn permission_denied_blocked_message() {
        let err = AvaError::PermissionDenied("rm -rf / blocked by policy".to_string());
        let msg = err.user_message();
        assert!(
            msg.contains("denied"),
            "user_message should contain 'denied': got '{msg}'"
        );
    }

    #[test]
    fn test_context_window_exceeded() {
        let err = AvaError::ContextWindowExceeded {
            current: 200_000,
            limit: 128_000,
        };
        assert_eq!(err.category(), ErrorCategory::Validation);
        assert!(!err.is_retryable());
        assert!(err.to_string().contains("200000"));
        assert!(err.to_string().contains("128000"));
    }
}
