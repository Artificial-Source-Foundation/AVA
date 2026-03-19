use thiserror::Error;

/// Errors from the edit tool's strategy pipeline.
///
/// The edit tool tries multiple strategies (exact match, fuzzy, regex) in order.
/// If none succeed, `NoMatch` or `NoMatchWithHints` is returned.
#[derive(Debug, Error)]
pub enum EditError {
    /// None of the edit strategies (exact, fuzzy, regex) matched the target text.
    #[error("no strategy could apply edit")]
    NoMatch,
    /// None of the edit strategies matched, but we found similar lines in the file
    /// that may help the caller self-correct.
    #[error("{message}")]
    NoMatchWithHints {
        message: String,
        /// The most similar lines found in the file, with their line numbers.
        hints: Vec<SimilarLineHint>,
    },
    /// The user-provided regex pattern failed to compile.
    #[error("invalid regex pattern: {0}")]
    InvalidRegex(String),
}

/// A hint about a similar line found in the file when an edit fails.
#[derive(Debug, Clone)]
pub struct SimilarLineHint {
    /// 1-based line number in the file.
    pub line_number: usize,
    /// The actual line content from the file.
    pub content: String,
    /// Similarity score (0.0 to 1.0).
    pub similarity: f64,
}
