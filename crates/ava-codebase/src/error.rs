use thiserror::Error;

pub type Result<T> = std::result::Result<T, CodebaseError>;

#[derive(Debug, Error)]
pub enum CodebaseError {
    #[error("tantivy error: {0}")]
    Tantivy(String),
    #[error("io error: {0}")]
    Io(#[from] std::io::Error),
    #[error("invalid query: {0}")]
    InvalidQuery(String),
}

impl From<tantivy::TantivyError> for CodebaseError {
    fn from(value: tantivy::TantivyError) -> Self {
        Self::Tantivy(value.to_string())
    }
}
