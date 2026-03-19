//! Ellipsis-aware edit strategy.
//!
//! When the LLM sends `old_text` containing `...` (or `// ...`, `# ...`, etc.)
//! placeholders, this strategy splits on those lines and matches the non-ellipsis
//! fragments against the file content. Everything between matched fragments is
//! kept as-is, allowing the LLM to reference a function without reproducing every line.

use crate::edit::error::EditError;
use crate::edit::request::EditRequest;
use crate::edit::strategies::EditStrategy;

/// Matches an ellipsis placeholder line: just `...` or with common comment prefixes.
fn is_ellipsis_line(line: &str) -> bool {
    let trimmed = line.trim();
    if trimmed == "..." {
        return true;
    }
    // Strip common comment prefixes and check for `...`
    for prefix in &["//", "#", "--", "/*", "*", "<!--", "%", ";"] {
        if let Some(rest) = trimmed.strip_prefix(prefix) {
            if rest.trim() == "..." {
                return true;
            }
        }
    }
    false
}

/// Split `old_text` on ellipsis lines into non-empty fragments.
fn split_on_ellipsis(old_text: &str) -> Vec<Vec<&str>> {
    let mut fragments: Vec<Vec<&str>> = Vec::new();
    let mut current: Vec<&str> = Vec::new();

    for line in old_text.lines() {
        if is_ellipsis_line(line) {
            if !current.is_empty() {
                fragments.push(std::mem::take(&mut current));
            }
        } else {
            current.push(line);
        }
    }
    if !current.is_empty() {
        fragments.push(current);
    }
    fragments
}

/// Find the line range in `content_lines` where `fragment` matches (trimmed comparison).
/// Returns `Some((start_line, end_line_exclusive))` for the first unique match,
/// or `None` if zero or multiple matches exist.
fn find_fragment(content_lines: &[&str], fragment: &[&str], search_start: usize) -> Option<usize> {
    if fragment.is_empty() || search_start + fragment.len() > content_lines.len() {
        return None;
    }

    let frag_trimmed: Vec<&str> = fragment.iter().map(|l| l.trim()).collect();

    for start in search_start..=(content_lines.len() - fragment.len()) {
        let window: Vec<&str> = content_lines[start..start + fragment.len()]
            .iter()
            .map(|l| l.trim())
            .collect();
        if window == frag_trimmed {
            return Some(start);
        }
    }
    None
}

/// Ellipsis-aware edit strategy.
#[derive(Debug, Default)]
pub struct EllipsisStrategy;

impl EditStrategy for EllipsisStrategy {
    fn name(&self) -> &'static str {
        "ellipsis"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        let fragments = split_on_ellipsis(&request.old_text);

        // Need at least 2 fragments (i.e., at least one ellipsis was present)
        if fragments.len() < 2 {
            return Ok(None);
        }

        let content_lines: Vec<&str> = request.content.lines().collect();

        // Find the position of each fragment in order
        let mut positions: Vec<(usize, usize)> = Vec::new(); // (start, end_exclusive)
        let mut search_from = 0;

        for fragment in &fragments {
            let Some(start) = find_fragment(&content_lines, fragment, search_from) else {
                return Ok(None); // Fragment not found
            };
            let end = start + fragment.len();
            positions.push((start, end));
            search_from = end;
        }

        // The matched region spans from the start of the first fragment
        // to the end of the last fragment.
        let region_start = positions[0].0;
        let region_end = positions[positions.len() - 1].1;

        // Build the replacement: substitute the region with new_text
        let new_lines: Vec<&str> = request.new_text.lines().collect();
        let mut rebuilt: Vec<&str> = Vec::with_capacity(content_lines.len());
        rebuilt.extend_from_slice(&content_lines[..region_start]);
        rebuilt.extend_from_slice(&new_lines);
        rebuilt.extend_from_slice(&content_lines[region_end..]);

        let mut out = rebuilt.join("\n");
        if request.content.ends_with('\n') {
            out.push('\n');
        }
        Ok(Some(out))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::edit::request::EditRequest;

    #[test]
    fn is_ellipsis_detects_plain() {
        assert!(is_ellipsis_line("..."));
        assert!(is_ellipsis_line("  ...  "));
    }

    #[test]
    fn is_ellipsis_detects_comments() {
        assert!(is_ellipsis_line("// ..."));
        assert!(is_ellipsis_line("# ..."));
        assert!(is_ellipsis_line("  // ..."));
        assert!(is_ellipsis_line("* ..."));
        assert!(is_ellipsis_line("-- ..."));
    }

    #[test]
    fn is_ellipsis_rejects_non_ellipsis() {
        assert!(!is_ellipsis_line("some code..."));
        assert!(!is_ellipsis_line("// some code"));
        assert!(!is_ellipsis_line("...."));
        assert!(!is_ellipsis_line(".."));
    }

    #[test]
    fn split_on_ellipsis_works() {
        let text = "fn main() {\n...\n    println!(\"end\");\n}";
        let frags = split_on_ellipsis(text);
        assert_eq!(frags.len(), 2);
        assert_eq!(frags[0], vec!["fn main() {"]);
        assert_eq!(frags[1], vec!["    println!(\"end\");", "}"]);
    }

    #[test]
    fn ellipsis_strategy_replaces_function() {
        let content = "fn main() {\n    let x = 1;\n    let y = 2;\n    let z = 3;\n    println!(\"done\");\n}\n";
        let old = "fn main() {\n// ...\n    println!(\"done\");\n}";
        let new = "fn main() {\n    let a = 10;\n    println!(\"done\");\n}";

        let req = EditRequest::new(content, old, new);
        let strategy = EllipsisStrategy;
        let result = strategy.apply(&req).unwrap().unwrap();
        assert_eq!(
            result,
            "fn main() {\n    let a = 10;\n    println!(\"done\");\n}\n"
        );
    }

    #[test]
    fn ellipsis_strategy_returns_none_without_ellipsis() {
        let content = "hello world";
        let old = "hello";
        let new = "goodbye";
        let req = EditRequest::new(content, old, new);
        let strategy = EllipsisStrategy;
        assert!(strategy.apply(&req).unwrap().is_none());
    }

    #[test]
    fn ellipsis_strategy_returns_none_if_fragment_not_found() {
        let content = "fn foo() {\n    bar();\n}\n";
        let old = "fn baz() {\n...\n}";
        let new = "fn baz() {}";
        let req = EditRequest::new(content, old, new);
        let strategy = EllipsisStrategy;
        assert!(strategy.apply(&req).unwrap().is_none());
    }

    #[test]
    fn ellipsis_strategy_multiple_ellipsis() {
        let content = "struct Foo {\n    a: i32,\n    b: String,\n    c: bool,\n}\n\nimpl Foo {\n    fn new() -> Self {\n        Self { a: 0, b: String::new(), c: false }\n    }\n}\n";
        let old = "struct Foo {\n...\n}\n\nimpl Foo {\n...\n}";
        let new = "struct Bar {\n    x: i32,\n}\n\nimpl Bar {\n    fn new() -> Self {\n        Self { x: 42 }\n    }\n}";

        let req = EditRequest::new(content, old, new);
        let strategy = EllipsisStrategy;
        let result = strategy.apply(&req).unwrap().unwrap();
        assert!(result.contains("struct Bar"));
        assert!(result.contains("x: 42"));
    }

    #[test]
    fn ellipsis_preserves_trailing_newline() {
        let content = "a\nb\nc\nd\n";
        let old = "a\n...\nd";
        let new = "x\ny";
        let req = EditRequest::new(content, old, new);
        let strategy = EllipsisStrategy;
        let result = strategy.apply(&req).unwrap().unwrap();
        assert!(result.ends_with('\n'));
    }
}
