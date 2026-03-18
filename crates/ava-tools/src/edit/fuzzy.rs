/// 3-tier fuzzy matching cascade: exact → line-trimmed → block-anchor.
///
/// These functions provide standalone matching that does not require
/// `EditRequest` metadata (anchors, line numbers, etc.).  They operate
/// purely on the file content, old_text, and new_text strings.
use crate::edit::error::EditError;
use crate::edit::request::EditRequest;
use crate::edit::strategies::EditStrategy;

/// Line-trimmed matching as an `EditStrategy`.
///
/// Sits in the engine cascade between `ExactMatchStrategy` and
/// `BlockAnchorStrategy`.
#[derive(Debug, Default)]
pub struct LineTrimmedStrategy;

impl EditStrategy for LineTrimmedStrategy {
    fn name(&self) -> &'static str {
        "line_trimmed"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        Ok(try_trimmed_match(
            &request.content,
            &request.old_text,
            &request.new_text,
        ))
    }
}

/// Auto block-anchor matching as an `EditStrategy`.
///
/// Derives anchors automatically from the first and last non-empty
/// lines of `old_text`, unlike the existing `BlockAnchorStrategy`
/// which requires explicit `before_anchor` / `after_anchor` fields.
#[derive(Debug, Default)]
pub struct AutoBlockAnchorStrategy;

impl EditStrategy for AutoBlockAnchorStrategy {
    fn name(&self) -> &'static str {
        "auto_block_anchor"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        Ok(try_anchor_match(
            &request.content,
            &request.old_text,
            &request.new_text,
        ))
    }
}

/// Tier 1 – exact substring match.
///
/// Returns `Some(replaced_content)` when `old` appears exactly once in
/// `content`.  Returns `None` if there are zero or multiple matches.
pub fn try_exact_match(content: &str, old: &str, new: &str) -> Option<String> {
    if old.is_empty() {
        return None;
    }
    let count = content.matches(old).count();
    if count != 1 {
        return None;
    }
    Some(content.replacen(old, new, 1))
}

/// Tier 2 – line-trimmed match.
///
/// Trims leading/trailing whitespace from each line of `old` and each
/// sliding window of `content`.  When the trimmed versions match
/// (and there is exactly one such window), replaces the **original**
/// (untrimmed) lines in the file with `new`.
pub fn try_trimmed_match(content: &str, old: &str, new: &str) -> Option<String> {
    if old.is_empty() {
        return None;
    }

    let old_lines: Vec<&str> = old.lines().collect();
    if old_lines.is_empty() {
        return None;
    }

    let content_lines: Vec<&str> = content.lines().collect();
    if content_lines.len() < old_lines.len() {
        return None;
    }

    let old_trimmed: Vec<&str> = old_lines.iter().map(|l| l.trim()).collect();

    let mut matches: Vec<usize> = Vec::new();
    for start in 0..=(content_lines.len() - old_lines.len()) {
        let window: Vec<&str> = content_lines[start..start + old_lines.len()]
            .iter()
            .map(|l| l.trim())
            .collect();
        if window == old_trimmed {
            matches.push(start);
        }
    }

    // Require exactly one match for safety.
    if matches.len() != 1 {
        return None;
    }

    let start = matches[0];
    let mut rebuilt: Vec<&str> = Vec::with_capacity(content_lines.len());
    rebuilt.extend_from_slice(&content_lines[..start]);

    // Push new_text lines.
    let new_lines: Vec<&str> = new.lines().collect();
    rebuilt.extend_from_slice(&new_lines);

    rebuilt.extend_from_slice(&content_lines[start + old_lines.len()..]);

    let mut out = rebuilt.join("\n");
    if content.ends_with('\n') {
        out.push('\n');
    }
    Some(out)
}

/// Tier 3 – block-anchor match.
///
/// Takes the first and last non-empty lines of `old` as anchors.
/// Finds a region in `content` where both anchors appear (trimmed)
/// in the right order.  Replaces everything between (and including)
/// the anchor lines with `new`.
///
/// Returns `None` if anchors are empty, not found, or ambiguous
/// (multiple matching regions).
pub fn try_anchor_match(content: &str, old: &str, new: &str) -> Option<String> {
    if old.is_empty() {
        return None;
    }

    let old_lines: Vec<&str> = old.lines().collect();
    let first_anchor = old_lines.iter().find(|l| !l.trim().is_empty())?.trim();
    let last_anchor = old_lines
        .iter()
        .rev()
        .find(|l| !l.trim().is_empty())?
        .trim();

    // Need two distinct anchors to avoid degenerate cases.
    if first_anchor == last_anchor {
        return None;
    }

    let content_lines: Vec<&str> = content.lines().collect();

    // Find all (start, end) pairs where first_anchor matches content[start].trim()
    // and last_anchor matches content[end].trim(), with start < end.
    let mut regions: Vec<(usize, usize)> = Vec::new();

    for (i, line) in content_lines.iter().enumerate() {
        if line.trim() != first_anchor {
            continue;
        }
        // Search forward for the last anchor.
        for (j, cline) in content_lines.iter().enumerate().skip(i + 1) {
            if cline.trim() == last_anchor {
                regions.push((i, j));
                break; // take the nearest match for this start
            }
        }
    }

    // Require exactly one matching region.
    if regions.len() != 1 {
        return None;
    }

    let (start, end) = regions[0];

    let mut rebuilt: Vec<&str> = Vec::with_capacity(content_lines.len());
    rebuilt.extend_from_slice(&content_lines[..start]);

    let new_lines: Vec<&str> = new.lines().collect();
    rebuilt.extend_from_slice(&new_lines);

    rebuilt.extend_from_slice(&content_lines[end + 1..]);

    let mut out = rebuilt.join("\n");
    if content.ends_with('\n') {
        out.push('\n');
    }
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── Tier 1: exact match ──

    #[test]
    fn exact_match_single_occurrence() {
        let content = "fn main() {\n    println!(\"hello\");\n}\n";
        let result = try_exact_match(content, "println!(\"hello\")", "println!(\"world\")");
        assert_eq!(
            result.unwrap(),
            "fn main() {\n    println!(\"world\");\n}\n"
        );
    }

    #[test]
    fn exact_match_returns_none_for_no_match() {
        let result = try_exact_match("abc", "xyz", "new");
        assert!(result.is_none());
    }

    #[test]
    fn exact_match_returns_none_for_multiple_matches() {
        let result = try_exact_match("aaa", "a", "b");
        assert!(result.is_none());
    }

    #[test]
    fn exact_match_returns_none_for_empty_old() {
        let result = try_exact_match("abc", "", "new");
        assert!(result.is_none());
    }

    // ── Tier 2: line-trimmed match ──

    #[test]
    fn trimmed_match_handles_indent_diff() {
        let content = "fn main() {\n    let x = 1;\n    let y = 2;\n}\n";
        let old = "let x = 1;\nlet y = 2;";
        let new = "let x = 10;\nlet y = 20;";
        let result = try_trimmed_match(content, old, new).unwrap();
        assert_eq!(result, "fn main() {\nlet x = 10;\nlet y = 20;\n}\n");
    }

    #[test]
    fn trimmed_match_returns_none_when_no_match() {
        let content = "alpha\nbeta\ngamma\n";
        let result = try_trimmed_match(content, "delta\nepsilon", "new");
        assert!(result.is_none());
    }

    #[test]
    fn trimmed_match_returns_none_for_multiple_matches() {
        let content = "    foo\n    bar\n    foo\n    bar\n";
        let result = try_trimmed_match(content, "foo\nbar", "baz\nqux");
        assert!(result.is_none());
    }

    #[test]
    fn trimmed_match_preserves_trailing_newline() {
        let content = "  a\n  b\n";
        let old = "a\nb";
        let new = "x\ny";
        let result = try_trimmed_match(content, old, new).unwrap();
        assert!(result.ends_with('\n'));
    }

    // ── Tier 3: block-anchor match ──

    #[test]
    fn anchor_match_replaces_block() {
        let content = "fn main() {\n    if true {\n        run();\n    }\n}\n";
        let old = "if true {\n    something_different();\n}";
        let new = "if true {\n        run_fast();\n    }";
        let result = try_anchor_match(content, old, new).unwrap();
        assert_eq!(
            result,
            "fn main() {\nif true {\n        run_fast();\n    }\n}\n"
        );
    }

    #[test]
    fn anchor_match_returns_none_when_anchors_identical() {
        // first and last non-empty lines are the same
        let content = "x\ny\nx\n";
        let old = "x\nstuff\nx";
        let result = try_anchor_match(content, old, "new");
        assert!(result.is_none());
    }

    #[test]
    fn anchor_match_returns_none_when_no_match() {
        let content = "alpha\nbeta\ngamma\n";
        let old = "delta {\n    inner\n}";
        let result = try_anchor_match(content, old, "new");
        assert!(result.is_none());
    }

    #[test]
    fn anchor_match_returns_none_for_multiple_regions() {
        let content = "if a {\n  x\n}\nif a {\n  y\n}\n";
        let old = "if a {\n  z\n}";
        let result = try_anchor_match(content, old, "new");
        assert!(result.is_none());
    }

    #[test]
    fn anchor_match_returns_none_for_empty_old() {
        let result = try_anchor_match("abc", "", "new");
        assert!(result.is_none());
    }

    // ── All tiers fail gracefully ──

    #[test]
    fn all_tiers_fail_gracefully() {
        let content = "completely unrelated content\n";
        let old = "this does not exist\nanywhere in the file";
        assert!(try_exact_match(content, old, "new").is_none());
        assert!(try_trimmed_match(content, old, "new").is_none());
        assert!(try_anchor_match(content, old, "new").is_none());
    }
}
