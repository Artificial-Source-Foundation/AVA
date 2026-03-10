use thiserror::Error;

pub type Result<T> = std::result::Result<T, SandboxError>;

/// Errors from the command sandbox (bwrap on Linux, sandbox-exec on macOS).
#[derive(Debug, Error)]
pub enum SandboxError {
    /// The sandbox policy configuration is invalid or contradictory.
    #[error("invalid policy: {0}")]
    InvalidPolicy(String),
    /// No sandbox backend available for the current OS.
    #[error("unsupported platform: {0}")]
    UnsupportedPlatform(String),
    /// The sandboxed command failed to execute or returned an error.
    #[error("sandbox execution failed: {0}")]
    ExecutionFailed(String),
    /// The sandboxed command exceeded its time budget.
    #[error("sandbox execution timed out")]
    Timeout,
}
