use thiserror::Error;

pub type Result<T> = std::result::Result<T, ContextError>;

#[derive(Debug, Error)]
pub enum ContextError {
    #[error("condensation failed: {0}")]
    Condensation(String),
    #[error("token budget exceeded: {0} > {1}")]
    TokenBudgetExceeded(usize, usize),
}
