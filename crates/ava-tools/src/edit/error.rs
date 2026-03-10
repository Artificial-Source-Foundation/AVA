use thiserror::Error;

/// Errors from the edit tool's strategy pipeline.
///
/// The edit tool tries multiple strategies (exact match, fuzzy, regex) in order.
/// If none succeed, `NoMatch` is returned.
#[derive(Debug, Error)]
pub enum EditError {
    /// None of the edit strategies (exact, fuzzy, regex) matched the target text.
    #[error("no strategy could apply edit")]
    NoMatch,
    /// The user-provided regex pattern failed to compile.
    #[error("invalid regex pattern: {0}")]
    InvalidRegex(String),
}
