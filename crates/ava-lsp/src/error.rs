use thiserror::Error;

pub type Result<T> = std::result::Result<T, LspError>;

#[derive(Debug, Error)]
pub enum LspError {
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
    #[error("serialization error: {0}")]
    Serde(#[from] serde_json::Error),
    #[error("protocol error: {0}")]
    Protocol(String),
}
