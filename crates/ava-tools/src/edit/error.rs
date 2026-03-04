use thiserror::Error;

#[derive(Debug, Error)]
pub enum EditError {
    #[error("no strategy could apply edit")]
    NoMatch,
    #[error("invalid regex pattern: {0}")]
    InvalidRegex(String),
}
