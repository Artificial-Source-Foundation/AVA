use std::path::Path;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct ApplyPatchTool {
    platform: Arc<dyn Platform>,
}

impl ApplyPatchTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for ApplyPatchTool {
    fn name(&self) -> &str {
        "apply_patch"
    }

    fn description(&self) -> &str {
        "Apply a unified diff patch to one or more files"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["patch"],
            "properties": {
                "patch": { "type": "string", "description": "Unified diff string" },
                "strip": { "type": "integer", "description": "Number of leading path components to strip (default 1)" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let patch_text = args
            .get("patch")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: patch".to_string()))?;
        let strip = args
            .get("strip")
            .and_then(Value::as_u64)
            .unwrap_or(1) as usize;

        let file_patches = parse_unified_diff(patch_text, strip)?;
        if file_patches.is_empty() {
            return Err(AvaError::ToolError("No file patches found in diff".to_string()));
        }

        let mut total_applied = 0usize;
        let mut total_rejected = 0usize;
        let mut rejected_details: Vec<String> = Vec::new();
        let mut files_modified = 0usize;

        for file_patch in &file_patches {
            let file_path = Path::new(&file_patch.path);
            let content = if self.platform.exists(file_path).await {
                self.platform.read_file(file_path).await?
            } else {
                String::new()
            };

            let lines: Vec<String> = content.lines().map(String::from).collect();
            let mut result_lines = lines.clone();
            let mut offset: isize = 0;
            let mut file_applied = 0usize;

            for hunk in &file_patch.hunks {
                match apply_hunk(&result_lines, hunk, offset) {
                    Some((new_lines, new_offset)) => {
                        result_lines = new_lines;
                        offset = new_offset;
                        file_applied += 1;
                    }
                    None => {
                        total_rejected += 1;
                        rejected_details.push(format!(
                            "{}:{} - hunk @@ -{},{} +{},{} @@",
                            file_patch.path,
                            hunk.old_start,
                            hunk.old_start,
                            hunk.old_count,
                            hunk.new_start,
                            hunk.new_count
                        ));
                    }
                }
            }

            if file_applied > 0 {
                let mut output = result_lines.join("\n");
                // Preserve trailing newline if original had one
                if content.ends_with('\n') || !content.is_empty() {
                    output.push('\n');
                }
                // Create parent dirs if needed for new files
                if let Some(parent) = file_path.parent() {
                    if !parent.as_os_str().is_empty() && !self.platform.exists(parent).await {
                        // Use platform to create the file which handles parent dirs
                    }
                }
                self.platform.write_file(file_path, &output).await?;
                total_applied += file_applied;
                files_modified += 1;
            }
        }

        let mut msg = format!("Applied {total_applied} hunks to {files_modified} files.");
        if total_rejected > 0 {
            msg.push_str(&format!(
                " Rejected: {total_rejected} hunks\n{}",
                rejected_details.join("\n")
            ));
        }

        Ok(ToolResult {
            call_id: String::new(),
            content: msg,
            is_error: total_rejected > 0 && total_applied == 0,
        })
    }
}

#[derive(Debug)]
struct FilePatch {
    path: String,
    hunks: Vec<Hunk>,
}

#[derive(Debug)]
struct Hunk {
    old_start: usize,
    old_count: usize,
    new_start: usize,
    new_count: usize,
    lines: Vec<HunkLine>,
}

#[derive(Debug, Clone)]
enum HunkLine {
    Context(String),
    Remove(String),
    Add(String),
}

fn parse_unified_diff(patch: &str, strip: usize) -> ava_types::Result<Vec<FilePatch>> {
    let mut file_patches: Vec<FilePatch> = Vec::new();
    let lines: Vec<&str> = patch.lines().collect();
    let mut i = 0;

    while i < lines.len() {
        // Look for --- a/path header
        if lines[i].starts_with("--- ") && i + 1 < lines.len() && lines[i + 1].starts_with("+++ ") {
            let new_path_raw = lines[i + 1].trim_start_matches("+++ ");
            let path = strip_path(new_path_raw, strip);
            i += 2;

            let mut hunks = Vec::new();
            while i < lines.len() && lines[i].starts_with("@@ ") {
                if let Some(hunk) = parse_hunk(&lines, &mut i) {
                    hunks.push(hunk);
                } else {
                    i += 1;
                }
            }

            if !hunks.is_empty() {
                file_patches.push(FilePatch { path, hunks });
            }
        } else {
            i += 1;
        }
    }

    Ok(file_patches)
}

fn strip_path(path: &str, strip: usize) -> String {
    if strip == 0 {
        return path.to_string();
    }
    let parts: Vec<&str> = path.splitn(strip + 1, '/').collect();
    if parts.len() > strip {
        parts[strip].to_string()
    } else {
        path.to_string()
    }
}

fn parse_hunk(lines: &[&str], i: &mut usize) -> Option<Hunk> {
    let header = lines[*i];
    // Parse @@ -old_start,old_count +new_start,new_count @@
    let header = header.trim_start_matches("@@ ").split(" @@").next()?;
    let parts: Vec<&str> = header.split_whitespace().collect();
    if parts.len() < 2 {
        return None;
    }

    let (old_start, old_count) = parse_range(parts[0].trim_start_matches('-'))?;
    let (new_start, new_count) = parse_range(parts[1].trim_start_matches('+'))?;

    *i += 1;

    let mut hunk_lines = Vec::new();
    while *i < lines.len() {
        let line = lines[*i];
        if line.starts_with("@@ ") || line.starts_with("--- ") {
            break;
        }
        if let Some(content) = line.strip_prefix('-') {
            hunk_lines.push(HunkLine::Remove(content.to_string()));
        } else if let Some(content) = line.strip_prefix('+') {
            hunk_lines.push(HunkLine::Add(content.to_string()));
        } else if let Some(content) = line.strip_prefix(' ') {
            hunk_lines.push(HunkLine::Context(content.to_string()));
        } else if line == "\\ No newline at end of file" {
            // skip
        } else {
            // Treat as context (some diffs omit the leading space)
            hunk_lines.push(HunkLine::Context(line.to_string()));
        }
        *i += 1;
    }

    Some(Hunk {
        old_start,
        old_count,
        new_start,
        new_count,
        lines: hunk_lines,
    })
}

fn parse_range(s: &str) -> Option<(usize, usize)> {
    if let Some((start, count)) = s.split_once(',') {
        Some((start.parse().ok()?, count.parse().ok()?))
    } else {
        Some((s.parse().ok()?, 1))
    }
}

fn apply_hunk(file_lines: &[String], hunk: &Hunk, offset: isize) -> Option<(Vec<String>, isize)> {
    let target_start = hunk.old_start as isize - 1 + offset;

    // Try exact position, then fuzzy offsets up to 3 lines
    for fuzz in 0..=3isize {
        for direction in &[0isize, 1, -1] {
            let try_start = target_start + fuzz * direction;
            if try_start < 0 {
                continue;
            }
            let start = try_start as usize;
            if try_apply_at(file_lines, hunk, start) {
                let mut result = Vec::new();
                result.extend_from_slice(&file_lines[..start]);
                for line in &hunk.lines {
                    match line {
                        HunkLine::Context(s) | HunkLine::Add(s) => result.push(s.clone()),
                        HunkLine::Remove(_) => {}
                    }
                }
                let old_end = start + hunk.old_count;
                if old_end <= file_lines.len() {
                    result.extend_from_slice(&file_lines[old_end..]);
                }
                let new_offset = offset + (hunk.new_count as isize - hunk.old_count as isize);
                return Some((result, new_offset));
            }
        }
    }

    None
}

fn try_apply_at(file_lines: &[String], hunk: &Hunk, start: usize) -> bool {
    let mut file_idx = start;
    for line in &hunk.lines {
        match line {
            HunkLine::Context(s) | HunkLine::Remove(s) => {
                if file_idx >= file_lines.len() {
                    return false;
                }
                if file_lines[file_idx] != *s {
                    return false;
                }
                file_idx += 1;
            }
            HunkLine::Add(_) => {}
        }
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strip_path_removes_components() {
        assert_eq!(strip_path("a/src/main.rs", 1), "src/main.rs");
        assert_eq!(strip_path("a/b/main.rs", 2), "main.rs");
        assert_eq!(strip_path("src/main.rs", 0), "src/main.rs");
    }

    #[test]
    fn parse_range_works() {
        assert_eq!(parse_range("1,3"), Some((1, 3)));
        assert_eq!(parse_range("5"), Some((5, 1)));
    }

    #[test]
    fn parse_unified_diff_extracts_files_and_hunks() {
        let patch = "\
--- a/src/main.rs
+++ b/src/main.rs
@@ -1,3 +1,3 @@
 fn main() {
-    println!(\"hello\");
+    println!(\"world\");
 }
";
        let result = parse_unified_diff(patch, 1).unwrap();
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].path, "src/main.rs");
        assert_eq!(result[0].hunks.len(), 1);
        assert_eq!(result[0].hunks[0].old_start, 1);
        assert_eq!(result[0].hunks[0].old_count, 3);
    }
}
