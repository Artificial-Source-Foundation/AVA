use thiserror::Error;

use ava_types::AvaError;

pub type Result<T> = std::result::Result<T, ContextError>;

#[derive(Debug, Error)]
pub enum ContextError {
    #[error("condensation failed: {0}")]
    Condensation(String),
    #[error("token budget exceeded: {0} > {1}")]
    TokenBudgetExceeded(usize, usize),
}

impl From<ContextError> for AvaError {
    fn from(value: ContextError) -> Self {
        match value {
            ContextError::Condensation(message) => AvaError::ValidationError(message),
            ContextError::TokenBudgetExceeded(current, max) => {
                AvaError::ValidationError(format!("token budget exceeded: {current} > {max}"))
            }
        }
    }
}
