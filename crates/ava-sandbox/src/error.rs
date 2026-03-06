use thiserror::Error;

pub type Result<T> = std::result::Result<T, SandboxError>;

#[derive(Debug, Error)]
pub enum SandboxError {
    #[error("invalid policy: {0}")]
    InvalidPolicy(String),
    #[error("unsupported platform: {0}")]
    UnsupportedPlatform(String),
    #[error("sandbox execution failed: {0}")]
    ExecutionFailed(String),
    #[error("sandbox execution timed out")]
    Timeout,
}
