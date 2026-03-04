use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ComputeFuzzyReplaceInput {
    pub content: String,
    pub old_string: String,
    pub new_string: String,
    pub replace_all: Option<bool>,
}

#[derive(Debug, Serialize)]
pub struct ComputeFuzzyReplaceOutput {
    pub content: String,
    pub strategy: String,
}

#[tauri::command]
pub fn compute_fuzzy_replace(
    input: ComputeFuzzyReplaceInput,
) -> Result<ComputeFuzzyReplaceOutput, String> {
    let replace_all = input.replace_all.unwrap_or(false);
    let normalized_content = normalize_line_endings(&input.content);
    let normalized_old = normalize_line_endings(&input.old_string);

    if normalized_old.is_empty() {
        return Ok(ComputeFuzzyReplaceOutput {
            content: input.new_string,
            strategy: "empty_old_string".to_string(),
        });
    }

    let strategies = [
        (
            "simple",
            simple_candidate(&normalized_content, &normalized_old),
        ),
        (
            "line_trimmed",
            line_trimmed_candidate(&normalized_content, &normalized_old),
        ),
        (
            "block_anchor",
            block_anchor_candidate(&normalized_content, &normalized_old),
        ),
        (
            "indentation_flexible",
            indentation_flexible_candidate(&normalized_content, &normalized_old),
        ),
    ];

    for (strategy, candidate) in strategies {
        if let Some(found) = candidate {
            if let Some(content) =
                apply_candidate(&normalized_content, &found, &input.new_string, replace_all)
            {
                return Ok(ComputeFuzzyReplaceOutput {
                    content,
                    strategy: strategy.to_string(),
                });
            }
        }
    }

    Err("oldString not found in file content. Verify the text matches exactly.".to_string())
}

fn normalize_line_endings(text: &str) -> String {
    text.replace("\r\n", "\n")
}

fn simple_candidate(content: &str, find: &str) -> Option<String> {
    if content.contains(find) {
        Some(find.to_string())
    } else {
        None
    }
}

fn line_trimmed_candidate(content: &str, find: &str) -> Option<String> {
    let content_lines: Vec<&str> = content.split('\n').collect();
    let search_lines: Vec<&str> = find.split('\n').collect();
    if search_lines.is_empty() || content_lines.len() < search_lines.len() {
        return None;
    }

    for start in 0..=(content_lines.len() - search_lines.len()) {
        let mut matches = true;
        for offset in 0..search_lines.len() {
            if content_lines[start + offset].trim() != search_lines[offset].trim() {
                matches = false;
                break;
            }
        }
        if matches {
            return Some(content_lines[start..start + search_lines.len()].join("\n"));
        }
    }
    None
}

fn block_anchor_candidate(content: &str, find: &str) -> Option<String> {
    let content_lines: Vec<&str> = content.split('\n').collect();
    let search_lines: Vec<&str> = find.split('\n').collect();
    if search_lines.len() < 3 || content_lines.len() < search_lines.len() {
        return None;
    }

    let first = search_lines.first()?.trim();
    let last = search_lines.last()?.trim();
    let mut best: Option<(usize, usize, f64)> = None;

    for start in 0..=(content_lines.len() - search_lines.len()) {
        let end = start + search_lines.len() - 1;
        if content_lines[start].trim() != first || content_lines[end].trim() != last {
            continue;
        }

        let mut total = 0.0;
        for middle in 1..(search_lines.len() - 1) {
            total += similarity(
                content_lines[start + middle].trim(),
                search_lines[middle].trim(),
            );
        }

        let score = total / (search_lines.len() - 2) as f64;
        if best.map(|b| score > b.2).unwrap_or(true) {
            best = Some((start, end, score));
        }
    }

    let (start, end, score) = best?;
    if score < 0.3 {
        return None;
    }
    Some(content_lines[start..=end].join("\n"))
}

fn indentation_flexible_candidate(content: &str, find: &str) -> Option<String> {
    let content_lines: Vec<&str> = content.split('\n').collect();
    let search_lines: Vec<&str> = find.split('\n').collect();
    if search_lines.is_empty() || content_lines.len() < search_lines.len() {
        return None;
    }

    let stripped_search = strip_common_indent(&search_lines);
    for start in 0..=(content_lines.len() - search_lines.len()) {
        let block = &content_lines[start..start + search_lines.len()];
        let stripped_block = strip_common_indent(block);
        if stripped_block == stripped_search {
            return Some(block.join("\n"));
        }
    }
    None
}

fn strip_common_indent(lines: &[&str]) -> Vec<String> {
    let indent = lines
        .iter()
        .filter(|line| !line.trim().is_empty())
        .map(|line| line.len().saturating_sub(line.trim_start().len()))
        .min()
        .unwrap_or(0);

    lines
        .iter()
        .map(|line| {
            if line.trim().is_empty() {
                String::new()
            } else {
                line.chars().skip(indent).collect()
            }
        })
        .collect()
}

fn apply_candidate(
    content: &str,
    candidate: &str,
    replacement: &str,
    replace_all: bool,
) -> Option<String> {
    if replace_all {
        return Some(content.replace(candidate, replacement));
    }

    let first = content.find(candidate)?;
    let last = content.rfind(candidate)?;
    if first != last {
        return None;
    }

    let mut output = String::with_capacity(content.len() + replacement.len());
    output.push_str(&content[..first]);
    output.push_str(replacement);
    output.push_str(&content[first + candidate.len()..]);
    Some(output)
}

fn similarity(a: &str, b: &str) -> f64 {
    let max_len = a.chars().count().max(b.chars().count());
    if max_len == 0 {
        return 1.0;
    }
    let distance = levenshtein(a, b);
    1.0 - (distance as f64 / max_len as f64)
}

fn levenshtein(a: &str, b: &str) -> usize {
    let a_chars: Vec<char> = a.chars().collect();
    let b_chars: Vec<char> = b.chars().collect();

    if a_chars.is_empty() {
        return b_chars.len();
    }
    if b_chars.is_empty() {
        return a_chars.len();
    }

    let mut prev: Vec<usize> = (0..=b_chars.len()).collect();
    let mut curr = vec![0; b_chars.len() + 1];

    for (i, a_char) in a_chars.iter().enumerate() {
        curr[0] = i + 1;
        for (j, b_char) in b_chars.iter().enumerate() {
            let cost = if a_char == b_char { 0 } else { 1 };
            curr[j + 1] = (curr[j] + 1).min(prev[j + 1] + 1).min(prev[j] + cost);
        }
        prev.clone_from(&curr);
    }

    prev[b_chars.len()]
}
