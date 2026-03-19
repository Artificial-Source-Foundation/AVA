pub mod ellipsis;
pub mod error;
pub mod fuzzy;
pub mod fuzzy_match;
pub mod recovery;
pub mod request;
pub mod strategies;

pub use ellipsis::EllipsisStrategy;
pub use error::EditError;
pub use fuzzy::{
    try_anchor_match, try_exact_match, try_trimmed_match, AutoBlockAnchorStrategy,
    LineTrimmedStrategy,
};
pub use fuzzy_match::{FuzzyMatchStrategy, StreamMatch, StreamingMatcher};
pub use recovery::{RecoveryPipeline, RecoveryResult, SelfCorrector};
pub use request::EditRequest;
pub use strategies::{
    BlockAnchorStrategy, DiffMatchPatchStrategy, EditStrategy, ExactMatchStrategy,
    FlexibleMatchStrategy, IndentationAwareStrategy, LineNumberStrategy, MultiOccurrenceStrategy,
    RegexMatchStrategy, RelativeIndentStrategy, ThreeWayMergeStrategy, TokenBoundaryStrategy,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EditResult {
    pub content: String,
    pub strategy: String,
}

pub struct EditEngine {
    strategies: Vec<Box<dyn EditStrategy>>,
}

impl Default for EditEngine {
    fn default() -> Self {
        Self {
            strategies: vec![
                Box::new(ExactMatchStrategy),
                Box::new(LineTrimmedStrategy),
                Box::new(AutoBlockAnchorStrategy),
                Box::new(EllipsisStrategy),
                Box::new(FlexibleMatchStrategy),
                Box::new(RelativeIndentStrategy),
                Box::new(BlockAnchorStrategy),
                Box::new(RegexMatchStrategy),
                Box::new(FuzzyMatchStrategy::new()),
                Box::new(LineNumberStrategy),
                Box::new(TokenBoundaryStrategy),
                Box::new(IndentationAwareStrategy),
                Box::new(MultiOccurrenceStrategy),
                Box::new(ThreeWayMergeStrategy),
                Box::new(DiffMatchPatchStrategy),
            ],
        }
    }
}

impl EditEngine {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn apply(&self, request: &EditRequest) -> Result<EditResult, EditError> {
        for strategy in &self.strategies {
            if let Some(content) = strategy.apply(request)? {
                return Ok(EditResult {
                    content,
                    strategy: strategy.name().to_string(),
                });
            }
        }

        // All strategies failed — produce rich error feedback with similar lines.
        let hints = find_similar_lines(&request.content, &request.old_text, 3);
        if hints.is_empty() {
            return Err(EditError::NoMatch);
        }

        let mut message =
            String::from("No strategy could apply edit. The most similar lines in the file are:\n");
        // Group consecutive hints into a range suggestion
        let first_line = hints[0].line_number;
        let last_line = hints[hints.len() - 1].line_number;
        for hint in &hints {
            message.push_str(&format!(
                "  line {}: {}\n",
                hint.line_number,
                hint.content.trim()
            ));
        }
        message.push_str(&format!(
            "Did you mean this section? (lines {first_line}-{last_line})"
        ));

        Err(EditError::NoMatchWithHints { message, hints })
    }

    pub fn strategy_count(&self) -> usize {
        self.strategies.len()
    }
}

/// Find the N most similar lines in `content` to the first line of `old_text`.
///
/// Uses the `similar` crate's `TextDiff` to compute per-line similarity ratios.
/// Returns results sorted by similarity (highest first), then by line number.
fn find_similar_lines(
    content: &str,
    old_text: &str,
    max_hints: usize,
) -> Vec<error::SimilarLineHint> {
    // Use the first non-empty line of old_text as the needle
    let needle = match old_text.lines().find(|l| !l.trim().is_empty()) {
        Some(l) => l.trim(),
        None => return Vec::new(),
    };

    if needle.len() < 3 {
        return Vec::new(); // Too short to meaningfully match
    }

    let mut scored: Vec<(usize, String, f64)> = content
        .lines()
        .enumerate()
        .filter(|(_, line)| !line.trim().is_empty())
        .map(|(i, line)| {
            let trimmed = line.trim();
            let ratio = line_similarity_ratio(needle, trimmed);
            (i + 1, line.to_string(), ratio) // 1-based line numbers
        })
        .filter(|(_, _, ratio)| *ratio > 0.4) // Only reasonably similar lines
        .collect();

    scored.sort_by(|a, b| b.2.partial_cmp(&a.2).unwrap_or(std::cmp::Ordering::Equal));
    scored.truncate(max_hints);

    // Sort by line number for display
    scored.sort_by_key(|(line_num, _, _)| *line_num);

    scored
        .into_iter()
        .map(
            |(line_number, content, similarity)| error::SimilarLineHint {
                line_number,
                content,
                similarity,
            },
        )
        .collect()
}

/// Compute similarity ratio between two strings using character-level diff.
/// Returns a value between 0.0 (completely different) and 1.0 (identical).
fn line_similarity_ratio(a: &str, b: &str) -> f64 {
    if a.is_empty() && b.is_empty() {
        return 1.0;
    }
    if a.is_empty() || b.is_empty() {
        return 0.0;
    }
    let diff = similar::TextDiff::from_chars(a, b);
    diff.ratio() as f64
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn engine_has_fifteen_strategies() {
        let engine = EditEngine::new();
        assert_eq!(engine.strategy_count(), 15);
    }

    #[test]
    fn engine_applies_first_matching_strategy() {
        let engine = EditEngine::new();
        let req = EditRequest::new("hello world", "world", "ava");
        let out = engine.apply(&req).unwrap();
        assert_eq!(out.content, "hello ava");
        assert_eq!(out.strategy, "exact_match");
    }
}
