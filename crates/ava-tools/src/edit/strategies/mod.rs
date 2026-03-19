use crate::edit::error::EditError;
use crate::edit::request::EditRequest;

mod advanced;
mod merge_fallbacks;
mod relative_indent;

pub use advanced::{
    BlockAnchorStrategy, IndentationAwareStrategy, LineNumberStrategy, MultiOccurrenceStrategy,
    RegexMatchStrategy, TokenBoundaryStrategy,
};
pub use merge_fallbacks::{DiffMatchPatchStrategy, ThreeWayMergeStrategy};
pub use relative_indent::RelativeIndentStrategy;

pub trait EditStrategy: Send + Sync {
    fn name(&self) -> &'static str;
    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError>;
}

#[derive(Debug, Default)]
pub struct ExactMatchStrategy;

impl EditStrategy for ExactMatchStrategy {
    fn name(&self) -> &'static str {
        "exact_match"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.old_text.is_empty() || !request.content.contains(&request.old_text) {
            return Ok(None);
        }
        Ok(Some(request.content.replacen(
            &request.old_text,
            &request.new_text,
            1,
        )))
    }
}

#[derive(Debug, Default)]
pub struct FlexibleMatchStrategy;

impl EditStrategy for FlexibleMatchStrategy {
    fn name(&self) -> &'static str {
        "flexible_match"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        let old_lines: Vec<&str> = request.old_text.lines().collect();
        if old_lines.is_empty() {
            return Ok(None);
        }
        let content_lines: Vec<&str> = request.content.lines().collect();
        if content_lines.len() < old_lines.len() {
            return Ok(None);
        }

        let old_norm = normalize_ws(&request.old_text);
        for start in 0..=(content_lines.len() - old_lines.len()) {
            let candidate = content_lines[start..start + old_lines.len()].join("\n");
            if normalize_ws(&candidate) == old_norm {
                let mut rebuilt: Vec<String> = Vec::new();
                rebuilt.extend(content_lines[..start].iter().map(|s| (*s).to_string()));
                rebuilt.extend(request.new_text.lines().map(|s| s.to_string()));
                rebuilt.extend(
                    content_lines[start + old_lines.len()..]
                        .iter()
                        .map(|s| (*s).to_string()),
                );
                let mut out = rebuilt.join("\n");
                if request.content.ends_with('\n') {
                    out.push('\n');
                }
                return Ok(Some(out));
            }
        }
        Ok(None)
    }
}

fn normalize_ws(input: &str) -> String {
    input
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
        .to_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_match_replaces() {
        let req = EditRequest::new("hello world", "world", "ava");
        let out = ExactMatchStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "hello ava");
    }

    #[test]
    fn flexible_match_ignores_spacing() {
        let req = EditRequest::new("alpha   beta\ngamma", "alpha beta\ngamma", "delta");
        let out = FlexibleMatchStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "delta");
    }

    #[test]
    fn flexible_match_preserves_trailing_newline() {
        let req = EditRequest::new("a\n b\n", "a\nb", "x");
        let out = FlexibleMatchStrategy.apply(&req).unwrap().unwrap();
        assert!(out.ends_with('\n'));
    }
}
