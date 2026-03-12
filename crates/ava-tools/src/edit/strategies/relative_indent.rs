use crate::edit::error::EditError;
use crate::edit::request::EditRequest;
use crate::edit::strategies::EditStrategy;

#[derive(Debug, Default)]
pub struct RelativeIndentStrategy;

impl EditStrategy for RelativeIndentStrategy {
    fn name(&self) -> &'static str {
        "relative_indent"
    }

    fn apply(&self, request: &EditRequest) -> Result<Option<String>, EditError> {
        if request.before_anchor.is_some() || request.after_anchor.is_some() {
            return Ok(None);
        }

        if request.old_text.trim().is_empty() {
            return Ok(None);
        }

        let old_lines: Vec<&str> = request.old_text.lines().collect();
        if old_lines.len() < 2 {
            return Ok(None);
        }

        let content_lines: Vec<&str> = request.content.lines().collect();
        if content_lines.len() < old_lines.len() {
            return Ok(None);
        }

        let Some(old_shape) = RelativeBlock::from_lines(&old_lines) else {
            return Ok(None);
        };

        for start in 0..=(content_lines.len() - old_lines.len()) {
            let candidate_lines = &content_lines[start..start + old_lines.len()];
            let Some(candidate_shape) = RelativeBlock::from_lines(candidate_lines) else {
                continue;
            };

            if candidate_shape.lines != old_shape.lines {
                continue;
            }

            let replacement = reindent_block(&request.new_text, &candidate_shape);
            let mut rebuilt: Vec<String> = Vec::with_capacity(
                start
                    + replacement.len()
                    + content_lines.len().saturating_sub(start + old_lines.len()),
            );
            rebuilt.extend(
                content_lines[..start]
                    .iter()
                    .map(|line| (*line).to_string()),
            );
            rebuilt.extend(replacement);
            rebuilt.extend(
                content_lines[start + old_lines.len()..]
                    .iter()
                    .map(|line| (*line).to_string()),
            );

            let mut out = rebuilt.join("\n");
            if request.content.ends_with('\n') {
                out.push('\n');
            }
            return Ok(Some(out));
        }

        Ok(None)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct RelativeBlock {
    base_indent: String,
    indent_prefixes: Vec<String>,
    lines: Vec<RelativeLine>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum RelativeLine {
    Blank,
    Content { level: usize, text: String },
}

impl RelativeBlock {
    fn from_lines(lines: &[&str]) -> Option<Self> {
        let mut indent_levels: Vec<usize> = lines
            .iter()
            .filter_map(|line| (!line.trim().is_empty()).then_some(indent_width(line)))
            .collect();
        indent_levels.sort_unstable();
        indent_levels.dedup();

        let min_indent = *indent_levels.first()?;

        let indent_prefixes = indent_levels
            .iter()
            .map(|indent| {
                lines
                    .iter()
                    .find(|line| indent_width(line) == *indent)
                    .map(|line| leading_indent(line).to_string())
                    .unwrap_or_default()
            })
            .collect::<Vec<_>>();
        let base_indent = indent_prefixes
            .first()
            .cloned()
            .unwrap_or_else(|| " ".repeat(min_indent));

        let lines = lines
            .iter()
            .map(|line| {
                if line.trim().is_empty() {
                    RelativeLine::Blank
                } else {
                    RelativeLine::Content {
                        level: indent_levels
                            .binary_search(&indent_width(line))
                            .expect("indent must exist in deduped set"),
                        text: line.trim().to_string(),
                    }
                }
            })
            .collect();

        Some(Self {
            base_indent,
            indent_prefixes,
            lines,
        })
    }
}

fn reindent_block(input: &str, candidate_shape: &RelativeBlock) -> Vec<String> {
    if input.is_empty() {
        return Vec::new();
    }

    let lines: Vec<&str> = input.lines().collect();
    let Some(new_shape) = RelativeBlock::from_lines(&lines) else {
        return vec![String::new(); lines.len()];
    };

    lines
        .into_iter()
        .zip(new_shape.lines.iter())
        .map(|(line, shape_line)| {
            if line.trim().is_empty() {
                String::new()
            } else {
                let prefix = match shape_line {
                    RelativeLine::Blank => candidate_shape.base_indent.clone(),
                    RelativeLine::Content { level, .. } => mapped_indent_prefix(
                        *level,
                        &new_shape.indent_prefixes,
                        &candidate_shape.indent_prefixes,
                        &candidate_shape.base_indent,
                    ),
                };
                format!("{prefix}{}", line.trim_start())
            }
        })
        .collect()
}

fn mapped_indent_prefix(
    level: usize,
    source_prefixes: &[String],
    target_prefixes: &[String],
    base_indent: &str,
) -> String {
    if let Some(prefix) = target_prefixes.get(level) {
        return prefix.clone();
    }

    let deepest_target = target_prefixes
        .last()
        .cloned()
        .unwrap_or_else(|| base_indent.to_string());
    let Some(source_prefix) = source_prefixes.get(level) else {
        return deepest_target;
    };
    let ancestor_idx = target_prefixes.len().saturating_sub(1);
    let ancestor_source = source_prefixes
        .get(ancestor_idx)
        .cloned()
        .unwrap_or_default();
    let suffix = source_prefix
        .strip_prefix(&ancestor_source)
        .unwrap_or(source_prefix.as_str());
    format!("{deepest_target}{suffix}")
}

fn indent_width(line: &str) -> usize {
    line.chars().take_while(|ch| ch.is_whitespace()).count()
}

fn leading_indent(line: &str) -> &str {
    let bytes = line
        .char_indices()
        .find_map(|(idx, ch)| (!ch.is_whitespace()).then_some(idx))
        .unwrap_or(line.len());
    &line[..bytes]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relative_indent_matches_shifted_block() {
        let request = EditRequest::new(
            "fn outer() {\n        if ready {\n            run();\n        }\n}\n",
            "if ready {\n    run();\n}",
            "if ready {\n    run_fast();\n}",
        );

        let out = RelativeIndentStrategy.apply(&request).unwrap().unwrap();

        assert_eq!(
            out,
            "fn outer() {\n        if ready {\n            run_fast();\n        }\n}\n"
        );
    }

    #[test]
    fn relative_indent_preserves_blank_lines() {
        let request = EditRequest::new(
            "if ready {\n    alpha();\n\n    beta();\n}\n",
            "if ready {\n  alpha();\n\n  beta();\n}",
            "if ready {\n  gamma();\n\n  beta();\n}",
        );

        let out = RelativeIndentStrategy.apply(&request).unwrap().unwrap();

        assert_eq!(out, "if ready {\n    gamma();\n\n    beta();\n}\n");
    }

    #[test]
    fn relative_indent_skips_single_line_requests() {
        let request = EditRequest::new("    alpha()", "alpha()", "beta()");
        assert!(RelativeIndentStrategy.apply(&request).unwrap().is_none());
    }

    #[test]
    fn relative_indent_defers_when_anchors_present() {
        let request = EditRequest::new(
            "<start>\n    if ready {\n        run();\n    }\n<end>",
            "if ready {\n    run();\n}",
            "if ready {\n    run_fast();\n}",
        )
        .with_anchors("<start>", "<end>");

        assert!(RelativeIndentStrategy.apply(&request).unwrap().is_none());
    }
}
