use regex::Regex;

use crate::edit::error::EditError;
use crate::edit::request::EditRequest;
use crate::edit::strategies::EditStrategy;

#[derive(Debug, Default)]
pub struct BlockAnchorStrategy;

impl EditStrategy for BlockAnchorStrategy {
    fn name(&self) -> &'static str {
        "block_anchor"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        let (Some(before), Some(after)) = (&request.before_anchor, &request.after_anchor) else {
            return Ok(None);
        };

        let Some(before_idx) = request.content.find(before) else {
            return Ok(None);
        };
        let start = before_idx + before.len();
        let Some(after_rel) = request.content[start..].find(after) else {
            return Ok(None);
        };
        let end = start + after_rel;
        let block = &request.content[start..end];

        let replaced_block = if request.old_text.is_empty() {
            request.new_text.clone()
        } else if block.contains(&request.old_text) {
            block.replacen(&request.old_text, &request.new_text, 1)
        } else {
            return Ok(None);
        };

        let mut out = String::with_capacity(request.content.len() + request.new_text.len());
        out.push_str(&request.content[..start]);
        out.push_str(&replaced_block);
        out.push_str(&request.content[end..]);
        Ok(Some(out))
    }
}

#[derive(Debug, Default)]
pub struct RegexMatchStrategy;

impl EditStrategy for RegexMatchStrategy {
    fn name(&self) -> &'static str {
        "regex_match"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        let Some(pattern) = request.regex_pattern.clone() else {
            return Ok(None);
        };
        if pattern.is_empty() {
            return Ok(None);
        }

        let re = Regex::new(&pattern).map_err(|e| EditError::InvalidRegex(e.to_string()))?;
        if !re.is_match(&request.content) {
            return Ok(None);
        }
        Ok(Some(
            re.replacen(&request.content, 1, request.new_text.as_str())
                .to_string(),
        ))
    }
}

#[derive(Debug, Default)]
pub struct LineNumberStrategy;

impl EditStrategy for LineNumberStrategy {
    fn name(&self) -> &'static str {
        "line_number"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        let Some(line_number) = request.line_number else {
            return Ok(None);
        };
        if line_number == 0 {
            return Ok(None);
        }

        let mut lines: Vec<String> = request.content.lines().map(str::to_string).collect();
        if line_number > lines.len() {
            return Ok(None);
        }
        let idx = line_number - 1;
        if request.old_text.is_empty() {
            lines[idx] = request.new_text.clone();
            return Ok(Some(lines.join("\n")));
        }
        if lines[idx].contains(&request.old_text) {
            lines[idx] = lines[idx].replacen(&request.old_text, &request.new_text, 1);
            return Ok(Some(lines.join("\n")));
        }
        Ok(None)
    }
}

#[derive(Debug, Default)]
pub struct TokenBoundaryStrategy;

impl EditStrategy for TokenBoundaryStrategy {
    fn name(&self) -> &'static str {
        "token_boundary"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.old_text.is_empty() {
            return Ok(None);
        }
        let pattern = format!(r"\b{}\b", regex::escape(&request.old_text));
        let re = Regex::new(&pattern).map_err(|e| EditError::InvalidRegex(e.to_string()))?;
        if !re.is_match(&request.content) {
            return Ok(None);
        }
        Ok(Some(
            re.replacen(&request.content, 1, request.new_text.as_str())
                .to_string(),
        ))
    }
}

#[derive(Debug, Default)]
pub struct IndentationAwareStrategy;

impl EditStrategy for IndentationAwareStrategy {
    fn name(&self) -> &'static str {
        "indentation_aware"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.old_text.trim().is_empty() {
            return Ok(None);
        }
        let old_trimmed = request.old_text.trim();
        let mut lines: Vec<String> = request.content.lines().map(str::to_string).collect();

        for line in &mut lines {
            if line.trim() == old_trimmed || line.contains(old_trimmed) {
                let indent: String = line.chars().take_while(|c| c.is_whitespace()).collect();
                let dedented = dedent(&request.new_text);
                let mut replacement_lines = dedented.lines();
                let Some(first) = replacement_lines.next() else {
                    return Ok(None);
                };
                let mut replacement = String::new();
                replacement.push_str(&format!("{indent}{first}"));
                for rest in replacement_lines {
                    replacement.push('\n');
                    replacement.push_str(rest);
                }
                *line = replacement;
                return Ok(Some(lines.join("\n")));
            }
        }
        Ok(None)
    }
}

#[derive(Debug, Default)]
pub struct MultiOccurrenceStrategy;

impl EditStrategy for MultiOccurrenceStrategy {
    fn name(&self) -> &'static str {
        "multi_occurrence"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.old_text.is_empty() {
            return Ok(None);
        }
        let target_occurrence = request.occurrence.unwrap_or(2);
        let Some((start, matched)) = request
            .content
            .match_indices(&request.old_text)
            .nth(target_occurrence.saturating_sub(1))
        else {
            return Ok(None);
        };

        let end = start + matched.len();
        let mut out = String::with_capacity(request.content.len() + request.new_text.len());
        out.push_str(&request.content[..start]);
        out.push_str(&request.new_text);
        out.push_str(&request.content[end..]);
        Ok(Some(out))
    }
}

fn dedent(input: &str) -> String {
    let non_empty: Vec<&str> = input.lines().filter(|l| !l.trim().is_empty()).collect();
    let min_indent = non_empty
        .iter()
        .map(|line| line.chars().take_while(|c| c.is_whitespace()).count())
        .min()
        .unwrap_or(0);

    input
        .lines()
        .map(|line| line.chars().skip(min_indent).collect::<String>())
        .collect::<Vec<_>>()
        .join("\n")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn anchor_strategy_replaces_inside_block() {
        let req =
            EditRequest::new("A<start>mid<end>Z", "mid", "NEW").with_anchors("<start>", "<end>");
        let out = BlockAnchorStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "A<start>NEW<end>Z");
    }

    #[test]
    fn regex_strategy_works() {
        let req = EditRequest::new("value=123", "", "value=999").with_regex_pattern(r"value=\d+");
        let out = RegexMatchStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "value=999");
    }

    #[test]
    fn line_number_replaces_specific_line() {
        let req = EditRequest::new("a\nb\nc", "b", "B").with_line_number(2);
        let out = LineNumberStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "a\nB\nc");
    }

    #[test]
    fn token_boundary_only_whole_word() {
        let req = EditRequest::new("cat concatenate", "cat", "dog");
        let out = TokenBoundaryStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "dog concatenate");
    }

    #[test]
    fn indentation_strategy_preserves_relative_indentation() {
        let req = EditRequest::new("    return x;", "return x;", "if y {\n    return y;\n}");
        let out = IndentationAwareStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "    if y {\n    return y;\n}");
    }

    #[test]
    fn multi_occurrence_replaces_nth() {
        let req = EditRequest::new("x x x", "x", "y").with_occurrence(2);
        let out = MultiOccurrenceStrategy.apply(&req).unwrap().unwrap();
        assert_eq!(out, "x y x");
    }
}
