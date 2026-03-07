//! Error types for the AVA system

use serde::{Deserialize, Serialize};
use thiserror::Error;

pub type Result<T> = std::result::Result<T, AvaError>;

#[derive(Error, Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum AvaError {
    // ── Structured provider/LLM errors ──────────────────────────────────

    #[error("Provider '{provider}' error: {message}")]
    ProviderError { provider: String, message: String },

    #[error("No API key configured for provider '{provider}'. Add your key to ~/.ava/credentials.json under providers.{provider}.api_key")]
    MissingApiKey { provider: String },

    #[error("Rate limited by {provider}. Retry after {retry_after_secs}s")]
    RateLimited {
        provider: String,
        retry_after_secs: u64,
    },

    #[error("Model '{model}' not found for provider '{provider}'")]
    ModelNotFound { provider: String, model: String },

    // ── Structured tool errors ──────────────────────────────────────────

    #[error("Tool '{tool}' not found. Available tools: {available}")]
    ToolNotFound { tool: String, available: String },

    #[error("Tool '{tool}' execution failed: {message}")]
    ToolExecutionError { tool: String, message: String },

    #[error("Tool '{tool}' timed out after {timeout_secs}s")]
    ToolTimeout { tool: String, timeout_secs: u64 },

    // ── Structured context errors ───────────────────────────────────────

    #[error("Context window exceeded ({current} tokens > {limit} tokens). Consider using a model with larger context or breaking the task into smaller parts")]
    ContextWindowExceeded { current: usize, limit: usize },

    // ── Structured agent errors ─────────────────────────────────────────

    #[error("Agent loop stopped: {reason}")]
    AgentStopped { reason: String },

    #[error("Agent run cancelled by user")]
    Cancelled,

    // ── Legacy string-payload variants (backward compat) ────────────────

    #[error("Tool execution failed: {0}")]
    ToolError(String),
    #[error("IO error: {0}")]
    IoError(String),
    #[error("Serialization error: {0}")]
    SerializationError(String),
    #[error("Platform error: {0}")]
    PlatformError(String),
    #[error("Configuration error: {0}")]
    ConfigError(String),
    #[error("Validation error: {0}")]
    ValidationError(String),
    #[error("Database error: {0}")]
    DatabaseError(String),
    #[error("Timeout error: {0}")]
    TimeoutError(String),
    #[error("Not found: {0}")]
    NotFound(String),
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

            AvaError::ProviderError { .. } | AvaError::RateLimited { .. } => {
                ErrorCategory::Provider
            }

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
    fn from(err: std::io::Error) -> Self {
        AvaError::IoError(err.to_string())
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
        let io_err = std::io::Error::new(std::io::ErrorKind::NotFound, "file not found");
        let ava_err: AvaError = io_err.into();
        match ava_err {
            AvaError::IoError(_) => (),
            _ => panic!("Expected IoError variant"),
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
