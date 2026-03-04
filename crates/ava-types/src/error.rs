//! Error types for the AVA system

use serde::{Deserialize, Serialize};
use thiserror::Error;

pub type Result<T> = std::result::Result<T, AvaError>;

#[derive(Error, Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum AvaError {
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
}

impl AvaError {
    /// Get the category for this error
    pub fn category(&self) -> ErrorCategory {
        match self {
            AvaError::ToolError(_) => ErrorCategory::Tool,
            AvaError::IoError(_) | AvaError::PlatformError(_) => ErrorCategory::System,
            AvaError::SerializationError(_) => ErrorCategory::Data,
            AvaError::ConfigError(_) => ErrorCategory::Config,
            AvaError::ValidationError(_) => ErrorCategory::Validation,
            AvaError::DatabaseError(_) => ErrorCategory::Database,
            AvaError::TimeoutError(_) => ErrorCategory::Timeout,
            AvaError::NotFound(_) => ErrorCategory::NotFound,
            AvaError::PermissionDenied(_) => ErrorCategory::Permission,
        }
    }

    /// Check if this error is retryable
    pub fn is_retryable(&self) -> bool {
        matches!(
            self,
            AvaError::TimeoutError(_) | AvaError::DatabaseError(_) | AvaError::PlatformError(_)
        )
    }

    /// Get a user-friendly error message
    pub fn user_message(&self) -> String {
        match self {
            AvaError::ToolError(msg) => format!("Tool failed: {}", msg),
            AvaError::IoError(msg) => format!("I/O error: {}", msg),
            AvaError::SerializationError(msg) => format!("Data error: {}", msg),
            AvaError::PlatformError(msg) => format!("System error: {}", msg),
            AvaError::ConfigError(msg) => format!("Configuration error: {}", msg),
            AvaError::ValidationError(msg) => format!("Validation failed: {}", msg),
            AvaError::DatabaseError(msg) => format!("Database error: {}", msg),
            AvaError::TimeoutError(msg) => format!("Operation timed out: {}", msg),
            AvaError::NotFound(msg) => format!("Not found: {}", msg),
            AvaError::PermissionDenied(msg) => format!("Permission denied: {}", msg),
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
}
