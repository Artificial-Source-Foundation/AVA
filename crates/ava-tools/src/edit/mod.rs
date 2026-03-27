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
    /// Tier 1: High-confidence strategies. Stop at first success.
    /// These are reliable — if they match, the result is correct.
    confident: Vec<Box<dyn EditStrategy>>,
    /// Tier 2: Speculative strategies. Run ALL, pick the best result.
    /// These may produce multiple valid results — we want the most surgical one.
    speculative: Vec<Box<dyn EditStrategy>>,
}

const SPECULATIVE_EARLY_EXIT_SIMILARITY: f64 = 0.95;
const MAX_EDIT_HINTS: usize = 5;
const MAX_EXPENSIVE_CANDIDATES: usize = 3;

impl Default for EditEngine {
    fn default() -> Self {
        Self {
            confident: vec![
                Box::new(ExactMatchStrategy),
                Box::new(LineTrimmedStrategy),
                Box::new(AutoBlockAnchorStrategy),
                Box::new(EllipsisStrategy),
                Box::new(FlexibleMatchStrategy),
                Box::new(RelativeIndentStrategy),
            ],
            speculative: vec![
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
        // Tier 1: Confident strategies — stop at first success.
        for strategy in &self.confident {
            if let Some(content) = strategy.apply(request)? {
                return Ok(EditResult {
                    content,
                    strategy: strategy.name().to_string(),
                });
            }
        }

        // Tier 2: Speculative strategies — run ALL, pick the best (most surgical) result.
        let mut candidates: Vec<EditResult> = Vec::new();
        for strategy in &self.speculative {
            match strategy.apply(request) {
                Ok(Some(content)) => {
                    let candidate = EditResult {
                        content,
                        strategy: strategy.name().to_string(),
                    };
                    let similarity = candidate_similarity(&request.content, &candidate.content);
                    if similarity > SPECULATIVE_EARLY_EXIT_SIMILARITY {
                        return Ok(candidate);
                    }
                    candidates.push(candidate);
                }
                Ok(None) => {}
                Err(_) => {}
            }
        }

        if !candidates.is_empty() {
            // Pick the most surgical edit: smallest diff from original content.
            // This ensures we prefer strategies that change only the intended region.
            let best = pick_best_candidate(&candidates, &request.content, &request.new_text);
            return Ok(best);
        }

        // Tier 3: Line-level fuzzy auto-correct (last resort).
        if let Some(result) = try_line_fuzzy_autocorrect(request) {
            return Ok(result);
        }

        // All strategies failed — produce rich error feedback with similar lines.
        let hints = find_similar_lines(&request.content, &request.old_text, MAX_EDIT_HINTS);
        let best_similarity = hints.iter().map(|hint| hint.similarity).fold(0.0, f64::max);
        let replacement_exists = replacement_already_exists(&request.content, &request.new_text);

        let mut message = format!(
            "No strategy could apply edit. Best similarity score achieved: {:.2}.\n\nMost similar lines in the file are:\n",
            best_similarity
        );
        if hints.is_empty() {
            message.push_str("  (no similar lines found)\n");
        } else {
            for hint in &hints {
                message.push_str(&format!(
                    "  line {} (similarity {:.2}): {}\n",
                    hint.line_number,
                    hint.similarity,
                    hint.content.trim()
                ));
            }
        }
        if replacement_exists {
            message.push_str(
                "\nReplacement text already exists in the file. This edit may have already been applied.\n",
            );
        }
        message.push_str("\nReview the line numbers above and retry with an exact match.");

        Err(EditError::NoMatchWithHints { message, hints })
    }

    pub fn strategy_count(&self) -> usize {
        self.confident.len() + self.speculative.len()
    }
}

/// Pick the best candidate from multiple successful speculative results.
///
/// Scoring heuristic:
/// 1. Prefer results that contain the new_text (sanity check)
/// 2. Among valid results, prefer the one with the smallest diff from original
///    (most surgical = least chance of unintended side effects)
/// 3. Tiebreak: prefer results from earlier strategies (implicit ordering)
fn pick_best_candidate(candidates: &[EditResult], original: &str, new_text: &str) -> EditResult {
    if candidates.len() == 1 {
        return candidates[0].clone();
    }

    let finalists =
        select_finalist_indexes(candidates, original, new_text, MAX_EXPENSIVE_CANDIDATES);
    let mut scored: Vec<(usize, f64)> = finalists
        .iter()
        .map(|&i| {
            let result = &candidates[i];
            let mut score = 0.0;

            // Bonus: result contains the new text (sanity check)
            if result.content.contains(new_text) {
                score += 10.0;
            }

            // Prefer minimal changes: higher similarity to original = more surgical
            let diff = similar::TextDiff::from_lines(original, &result.content);
            let change_ratio = 1.0 - diff.ratio() as f64;
            // Lower change_ratio = better (fewer changes)
            score -= change_ratio * 5.0;

            // Small tiebreak: prefer earlier strategies
            score -= i as f64 * 0.01;

            (i, score)
        })
        .collect();

    scored.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
    candidates[scored[0].0].clone()
}

fn select_finalist_indexes(
    candidates: &[EditResult],
    original: &str,
    new_text: &str,
    max_candidates: usize,
) -> Vec<usize> {
    let mut scored: Vec<(usize, bool, usize, usize)> = candidates
        .iter()
        .enumerate()
        .map(|(index, candidate)| {
            (
                index,
                candidate.content.contains(new_text),
                cheap_line_change_count(original, &candidate.content),
                original.len().abs_diff(candidate.content.len()),
            )
        })
        .collect();

    scored.sort_by(|left, right| {
        right
            .1
            .cmp(&left.1)
            .then_with(|| left.2.cmp(&right.2))
            .then_with(|| left.3.cmp(&right.3))
            .then_with(|| left.0.cmp(&right.0))
    });

    scored
        .into_iter()
        .take(max_candidates.max(1))
        .map(|(index, _, _, _)| index)
        .collect()
}

fn cheap_line_change_count(original: &str, edited: &str) -> usize {
    let original_lines: Vec<&str> = original.lines().collect();
    let edited_lines: Vec<&str> = edited.lines().collect();
    let max_len = original_lines.len().max(edited_lines.len());
    (0..max_len)
        .filter(|&idx| original_lines.get(idx) != edited_lines.get(idx))
        .count()
}

fn candidate_similarity(original: &str, edited: &str) -> f64 {
    similar::TextDiff::from_lines(original, edited).ratio() as f64
}

/// Minimum similarity ratio for auto-correcting an edit.
/// Higher = safer (fewer false positives), lower = more forgiving.
const AUTOCORRECT_THRESHOLD: f64 = 0.85;

/// Try to find a block of lines in the file that closely matches `old_text`.
/// If found with similarity >= AUTOCORRECT_THRESHOLD, apply the edit automatically.
///
/// This is the "typo forgiver" — when the LLM gets the old_text slightly wrong
/// (whitespace, minor typos, wrong variable name), find the closest matching block
/// and use it instead of failing.
fn try_line_fuzzy_autocorrect(request: &EditRequest) -> Option<EditResult> {
    let old_lines: Vec<&str> = request.old_text.lines().collect();
    let content_lines: Vec<&str> = request.content.lines().collect();

    if old_lines.is_empty() || content_lines.is_empty() {
        return None;
    }

    // Skip if old_text is very short (1 line, few chars) — too ambiguous for fuzzy
    let old_text_trimmed = request.old_text.trim();
    if old_text_trimmed.len() < 10 || old_lines.is_empty() {
        return None;
    }

    let old_line_count = old_lines.len();
    let mut best_match: Option<(usize, f64)> = None; // (start_line_idx, similarity)

    // Slide a window of old_line_count lines across the file content
    for start in 0..=content_lines.len().saturating_sub(old_line_count) {
        let window = &content_lines[start..start + old_line_count];
        let window_text = window.join("\n");
        let ratio = block_similarity(&request.old_text, &window_text);

        if ratio >= AUTOCORRECT_THRESHOLD {
            match best_match {
                None => best_match = Some((start, ratio)),
                Some((_, best_ratio)) if ratio > best_ratio => {
                    best_match = Some((start, ratio));
                }
                _ => {}
            }
        }
    }

    // Also try windows of ±1 line to handle off-by-one in block boundaries
    for delta in [1_isize, -1_isize] {
        let adjusted_count = (old_line_count as isize + delta) as usize;
        if adjusted_count == 0 || adjusted_count > content_lines.len() {
            continue;
        }
        for start in 0..=content_lines.len().saturating_sub(adjusted_count) {
            let window = &content_lines[start..start + adjusted_count];
            let window_text = window.join("\n");
            let ratio = block_similarity(&request.old_text, &window_text);

            if ratio >= AUTOCORRECT_THRESHOLD {
                match best_match {
                    None => best_match = Some((start, ratio)),
                    Some((_, best_ratio)) if ratio > best_ratio => {
                        best_match = Some((start, ratio));
                    }
                    _ => {}
                }
            }
        }
    }

    let (best_start, _best_ratio) = best_match?;

    // Find the end of the matched block — try exact line count first, then ±1
    let best_end_line = find_best_end_line(
        &content_lines,
        best_start,
        old_line_count,
        &request.old_text,
    );
    let line_ranges = line_byte_ranges(&request.content);
    let &(byte_start, _) = line_ranges.get(best_start)?;
    let (_, byte_end) = line_ranges.get(best_end_line.saturating_sub(1)).copied()?;
    // Ensure we don't go out of bounds
    if byte_end > request.content.len() {
        return None;
    }

    // Build the result
    let mut out = String::with_capacity(request.content.len() + request.new_text.len());
    out.push_str(&request.content[..byte_start]);
    out.push_str(&request.new_text);
    out.push_str(&request.content[byte_end..]);

    Some(EditResult {
        content: out,
        strategy: "line_fuzzy_autocorrect".to_string(),
    })
}

/// Find the best end line for the fuzzy match by trying exact and ±1 windows.
fn find_best_end_line(
    content_lines: &[&str],
    start: usize,
    old_line_count: usize,
    old_text: &str,
) -> usize {
    let mut best_end = (start + old_line_count).min(content_lines.len());
    let mut best_ratio = block_similarity(old_text, &content_lines[start..best_end].join("\n"));

    // Try +1 line
    let end_plus = (start + old_line_count + 1).min(content_lines.len());
    if end_plus > best_end {
        let ratio = block_similarity(old_text, &content_lines[start..end_plus].join("\n"));
        if ratio > best_ratio {
            best_end = end_plus;
            best_ratio = ratio;
        }
    }

    // Try -1 line
    if old_line_count > 1 {
        let end_minus = start + old_line_count - 1;
        if end_minus > start {
            let ratio = block_similarity(old_text, &content_lines[start..end_minus].join("\n"));
            if ratio > best_ratio {
                best_end = end_minus;
            }
        }
    }

    best_end
}

fn line_byte_ranges(content: &str) -> Vec<(usize, usize)> {
    let mut ranges = Vec::new();
    let mut start = 0;

    for chunk in content.split_inclusive('\n') {
        let line = chunk
            .strip_suffix("\r\n")
            .or_else(|| chunk.strip_suffix('\n'))
            .unwrap_or(chunk);
        let end = start + line.len();
        ranges.push((start, end));
        start += chunk.len();
    }

    if content.is_empty() {
        return ranges;
    }

    if !content.ends_with('\n') && ranges.is_empty() {
        ranges.push((0, content.len()));
    }

    ranges
}

/// Compute block-level similarity using line-by-line comparison.
/// Normalizes whitespace per-line for fairer comparison.
fn block_similarity(a: &str, b: &str) -> f64 {
    let a_normalized = normalize_block(a);
    let b_normalized = normalize_block(b);

    if a_normalized.is_empty() && b_normalized.is_empty() {
        return 1.0;
    }
    if a_normalized.is_empty() || b_normalized.is_empty() {
        return 0.0;
    }

    let diff = similar::TextDiff::from_lines(&a_normalized, &b_normalized);
    diff.ratio() as f64
}

/// Normalize a block of text for comparison: trim each line, collapse multiple spaces.
fn normalize_block(text: &str) -> String {
    text.lines()
        .map(|l| l.trim())
        .collect::<Vec<_>>()
        .join("\n")
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

fn replacement_already_exists(content: &str, new_text: &str) -> bool {
    if new_text.trim().is_empty() {
        return false;
    }
    if content.contains(new_text) {
        return true;
    }

    let new_lines: Vec<&str> = new_text.lines().collect();
    if new_lines.is_empty() {
        return false;
    }

    let target: Vec<&str> = new_lines.iter().map(|line| line.trim()).collect();
    let content_lines: Vec<&str> = content.lines().collect();
    if content_lines.len() < target.len() {
        return false;
    }

    (0..=content_lines.len() - target.len()).any(|start| {
        content_lines[start..start + target.len()]
            .iter()
            .map(|line| line.trim())
            .eq(target.iter().copied())
    })
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
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    use super::*;
    use crate::edit::strategies::EditStrategy;

    struct TestStrategy {
        name: &'static str,
        result: Option<String>,
        calls: Arc<AtomicUsize>,
    }

    impl EditStrategy for TestStrategy {
        fn name(&self) -> &'static str {
            self.name
        }

        fn apply(&self, _request: &EditRequest) -> Result<Option<String>, EditError> {
            self.calls.fetch_add(1, Ordering::SeqCst);
            Ok(self.result.clone())
        }
    }

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

    #[test]
    fn line_fuzzy_autocorrect_fixes_whitespace_mismatch() {
        // LLM sends old_text with wrong indentation
        let content = "fn main() {\n    let x = 42;\n    println!(\"{}\", x);\n}";
        let old_text = "  let x = 42;\n  println!(\"{}\", x);"; // wrong indent (2 vs 4)
        let new_text = "    let x = 99;\n    println!(\"{}\", x);";

        let req = EditRequest::new(content, old_text, new_text);
        let result = try_line_fuzzy_autocorrect(&req);
        assert!(result.is_some(), "should auto-correct whitespace mismatch");
        let result = result.unwrap();
        assert!(
            result.content.contains("let x = 99"),
            "should contain new text"
        );
        assert_eq!(result.strategy, "line_fuzzy_autocorrect");
    }

    #[test]
    fn line_fuzzy_autocorrect_fixes_minor_typo() {
        // Longer block where a single typo is a small fraction of the total
        let content = "impl Parser {\n    pub fn new(tokens: Vec<Token>) -> Self {\n        Self {\n            tokens,\n            position: 0,\n            errors: Vec::new(),\n        }\n    }\n}";
        // LLM writes "positon" instead of "position" — typo on one line in a 9-line block
        let old_text = "impl Parser {\n    pub fn new(tokens: Vec<Token>) -> Self {\n        Self {\n            tokens,\n            positon: 0,\n            errors: Vec::new(),\n        }\n    }\n}";
        let new_text = "impl Parser {\n    pub fn new(tokens: Vec<Token>) -> Self {\n        Self {\n            tokens,\n            position: 0,\n            errors: Vec::new(),\n            warnings: Vec::new(),\n        }\n    }\n}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = try_line_fuzzy_autocorrect(&req);
        assert!(
            result.is_some(),
            "should auto-correct minor typo in larger block"
        );
        assert!(result.unwrap().content.contains("warnings: Vec::new()"));
    }

    #[test]
    fn line_fuzzy_autocorrect_rejects_dissimilar() {
        let content = "fn alpha() {}\nfn beta() {}\nfn gamma() {}";
        let old_text = "fn totally_different_function() {\n    something_else();\n}";
        let new_text = "fn replaced() {}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = try_line_fuzzy_autocorrect(&req);
        assert!(result.is_none(), "should reject dissimilar content");
    }

    #[test]
    fn line_fuzzy_autocorrect_too_short() {
        // Very short old_text should be rejected (too ambiguous)
        let content = "let x = 1;\nlet y = 2;\nlet z = 3;";
        let old_text = "x = 1;";
        let new_text = "x = 99;";

        let req = EditRequest::new(content, old_text, new_text);
        let result = try_line_fuzzy_autocorrect(&req);
        assert!(result.is_none(), "should reject too-short old_text");
    }

    #[test]
    fn line_fuzzy_autocorrect_via_engine() {
        // Test that the engine actually invokes the autocorrect as last resort
        let engine = EditEngine::new();
        let content = "fn process() {\n    let result = compute();\n    save(result);\n}";
        // Slightly wrong: "compuet" instead of "compute"
        let old_text = "fn process() {\n    let result = compuet();\n    save(result);\n}";
        let new_text = "fn process() {\n    let result = compute_v2();\n    save(result);\n}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = engine.apply(&req);
        assert!(
            result.is_ok(),
            "engine should auto-correct via fuzzy: {result:?}"
        );
        let result = result.unwrap();
        assert!(result.content.contains("compute_v2()"));
    }

    #[test]
    fn line_fuzzy_autocorrect_handles_crlf_offsets() {
        let content = "fn main() {\r\n    let x = 42;\r\n    println!(\"{}\", x);\r\n}\r\n";
        let old_text = "  let x = 42;\r\n  println!(\"{}\", x);";
        let new_text = "    let x = 99;\r\n    println!(\"{}\", x);";

        let req = EditRequest::new(content, old_text, new_text);
        let result = try_line_fuzzy_autocorrect(&req).expect("should auto-correct CRLF content");
        assert_eq!(
            result.content,
            "fn main() {\r\n    let x = 99;\r\n    println!(\"{}\", x);\r\n}\r\n"
        );
    }

    #[test]
    fn tiered_racing_confident_tier_short_circuits() {
        // ExactMatch is in confident tier — should short-circuit without running speculative
        let engine = EditEngine::new();
        let req = EditRequest::new("fn foo() { bar(); }", "bar()", "baz()");
        let result = engine.apply(&req).unwrap();
        assert_eq!(result.strategy, "exact_match");
        assert!(result.content.contains("baz()"));
    }

    #[test]
    fn tiered_racing_speculative_picks_best() {
        // When confident strategies fail, speculative strategies should race
        // and the best (most surgical) result should win
        let engine = EditEngine::new();
        // Content where exact match fails but multiple speculative strategies could work
        let content =
            "fn process(data: &[u8]) {\n    let result = parse(data);\n    save(result);\n}\n";
        // Slightly different whitespace that defeats confident strategies
        let old_text =
            "fn process(data:  &[u8]) {\n    let result = parse(data);\n    save(result);\n}";
        let new_text =
            "fn process(data: &[u8]) {\n    let result = transform(data);\n    save(result);\n}";

        let req = EditRequest::new(content, old_text, new_text);
        let result = engine.apply(&req);
        assert!(
            result.is_ok(),
            "speculative tier should find a match: {result:?}"
        );
        let result = result.unwrap();
        assert!(
            result.content.contains("transform(data)"),
            "should contain new text"
        );
    }

    #[test]
    fn pick_best_candidate_prefers_surgical() {
        let original = "line1\nline2\nline3\nline4\nline5";
        let new_text = "REPLACED";

        // Candidate A: changes only line2 (surgical)
        let candidate_a = EditResult {
            content: "line1\nREPLACED\nline3\nline4\nline5".to_string(),
            strategy: "strategy_a".to_string(),
        };
        // Candidate B: changes more of the file (less surgical)
        let candidate_b = EditResult {
            content: "REPLACED\nline4\nline5".to_string(),
            strategy: "strategy_b".to_string(),
        };

        let best = pick_best_candidate(&[candidate_a.clone(), candidate_b], original, new_text);
        assert_eq!(
            best.strategy, "strategy_a",
            "should prefer more surgical edit"
        );
    }

    #[test]
    fn speculative_early_exit_skips_late_strategies() {
        let original = (0..100)
            .map(|index| format!("line {index}"))
            .collect::<Vec<_>>()
            .join("\n");
        let surgical = original.replacen("line 50", "line 50 updated", 1);
        let first_calls = Arc::new(AtomicUsize::new(0));
        let second_calls = Arc::new(AtomicUsize::new(0));
        let engine = EditEngine {
            confident: Vec::new(),
            speculative: vec![
                Box::new(TestStrategy {
                    name: "surgical",
                    result: Some(surgical.clone()),
                    calls: first_calls.clone(),
                }),
                Box::new(TestStrategy {
                    name: "expensive",
                    result: Some(original.replace("line 90", "line 90 changed")),
                    calls: second_calls.clone(),
                }),
            ],
        };

        let result = engine
            .apply(&EditRequest::new(&original, "line 50", "line 50 updated"))
            .unwrap();

        assert_eq!(result.strategy, "surgical");
        assert_eq!(first_calls.load(Ordering::SeqCst), 1);
        assert_eq!(second_calls.load(Ordering::SeqCst), 0);
    }

    #[test]
    fn select_finalists_prefers_candidates_with_fewer_changed_lines() {
        let original = "a\nb\nc\nd\ne";
        let candidates = vec![
            EditResult {
                content: "x\ny\nz".to_string(),
                strategy: "large_change".to_string(),
            },
            EditResult {
                content: "a\nB\nc\nd\ne".to_string(),
                strategy: "small_change".to_string(),
            },
            EditResult {
                content: "a\nb\nc\nD\ne".to_string(),
                strategy: "small_change_two".to_string(),
            },
            EditResult {
                content: "a\nb\nc\nd\nE\nF".to_string(),
                strategy: "medium_change".to_string(),
            },
        ];

        let finalists = select_finalist_indexes(&candidates, original, "B", 2);
        assert_eq!(finalists, vec![1, 2]);
    }

    #[test]
    fn replacement_already_exists_detects_trimmed_blocks() {
        let content = "fn main() {\n    println!(\"done\");\n}\n";
        assert!(replacement_already_exists(
            content,
            "fn main() {\nprintln!(\"done\");\n}"
        ));
    }

    #[test]
    fn failure_message_reports_similarity_and_existing_replacement() {
        let engine = EditEngine {
            confident: Vec::new(),
            speculative: Vec::new(),
        };
        let request = EditRequest::new("alpha\nbeta\ngamma\nreplacement\n", "betta", "replacement");

        let error = engine.apply(&request).unwrap_err().to_string();
        assert!(error.contains("Best similarity score achieved"));
        assert!(error.contains("line 2"));
        assert!(error.contains("Replacement text already exists in the file"));
    }

    #[test]
    fn block_similarity_works() {
        let a = "fn foo() {\n    bar();\n}";
        let b = "fn foo() {\n    bar();\n}";
        assert!((block_similarity(a, b) - 1.0).abs() < 0.001);

        let c = "fn foo() {\n    baz();\n}";
        let ratio = block_similarity(a, c);
        assert!(
            ratio > 0.5 && ratio < 1.0,
            "should be partially similar: {ratio}"
        );
    }
}
