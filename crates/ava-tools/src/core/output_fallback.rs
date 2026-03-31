//! Tool output disk fallback.
//!
//! When tool output exceeds a size limit, the full output is saved to disk
//! under `~/.ava/tool-output/` and the inline content is truncated with a
//! pointer to the saved file so the LLM can re-read it if needed.

use std::path::{Path, PathBuf};

/// Save tool output to disk when it exceeds `max_inline_size` bytes.
///
/// Returns the original content unchanged if it fits within the limit.
/// Otherwise writes the full output to `~/.ava/tool-output/{tool_name}-{timestamp}.txt`
/// and returns a truncated version with a path reference.
pub fn save_tool_output_fallback(tool_name: &str, content: &str, max_inline_size: usize) -> String {
    if content.len() <= max_inline_size {
        return content.to_string();
    }

    tracing::info!(
        tool = tool_name,
        original_size = content.len(),
        truncated_size = max_inline_size,
        "F6: truncating tool output (head)"
    );

    let output_dir = match dirs::home_dir() {
        Some(home) => home.join(".ava/tool-output"),
        None => return truncate_with_path_notice(content, max_inline_size, None),
    };

    if std::fs::create_dir_all(&output_dir).is_err() {
        return truncate_with_path_notice(content, max_inline_size, None);
    }

    let timestamp = chrono::Utc::now().format("%Y%m%d-%H%M%S%.3f");
    let filename = format!("{tool_name}-{timestamp}.txt");
    let path = output_dir.join(&filename);

    if std::fs::write(&path, content).is_err() {
        return truncate_with_path_notice(content, max_inline_size, None);
    }

    truncate_with_path_notice(content, max_inline_size, Some(&path))
}

/// Like [`save_tool_output_fallback`] but uses tail-truncation: keeps the last
/// `max_inline_size` bytes of content. This is better for bash output where
/// errors and exit codes appear at the end.
pub fn save_tool_output_fallback_tail(
    tool_name: &str,
    content: &str,
    max_inline_size: usize,
) -> String {
    if content.len() <= max_inline_size {
        return content.to_string();
    }

    tracing::info!(
        tool = tool_name,
        original_size = content.len(),
        truncated_size = max_inline_size,
        "F6: truncating tool output (tail)"
    );

    let output_dir = match dirs::home_dir() {
        Some(home) => home.join(".ava/tool-output"),
        None => return truncate_tail_with_path_notice(content, max_inline_size, None),
    };

    if std::fs::create_dir_all(&output_dir).is_err() {
        return truncate_tail_with_path_notice(content, max_inline_size, None);
    }

    let timestamp = chrono::Utc::now().format("%Y%m%d-%H%M%S%.3f");
    let filename = format!("{tool_name}-{timestamp}.txt");
    let path = output_dir.join(&filename);

    if std::fs::write(&path, content).is_err() {
        return truncate_tail_with_path_notice(content, max_inline_size, None);
    }

    truncate_tail_with_path_notice(content, max_inline_size, Some(&path))
}

/// Clean up tool output files older than the given number of days.
pub fn cleanup_old_outputs(max_age_days: u64) {
    let output_dir = match dirs::home_dir() {
        Some(home) => home.join(".ava/tool-output"),
        None => return,
    };

    let Ok(entries) = std::fs::read_dir(&output_dir) else {
        return;
    };

    let cutoff =
        std::time::SystemTime::now() - std::time::Duration::from_secs(max_age_days * 24 * 60 * 60);

    for entry in entries.flatten() {
        let Ok(meta) = entry.metadata() else {
            continue;
        };
        let Ok(modified) = meta.modified() else {
            continue;
        };
        if modified < cutoff {
            let _ = std::fs::remove_file(entry.path());
        }
    }
}

fn truncate_with_path_notice(content: &str, max_size: usize, path: Option<&Path>) -> String {
    truncate_head_with_path_notice(content, max_size, path)
}

/// Head-truncation: keep the first `max_size` bytes (for file reads, grep, etc.).
fn truncate_head_with_path_notice(content: &str, max_size: usize, path: Option<&Path>) -> String {
    let mut idx = max_size.min(content.len());
    while idx > 0 && !content.is_char_boundary(idx) {
        idx -= 1;
    }
    let truncated = &content[..idx];

    match path {
        Some(p) => format!(
            "{truncated}\n\n... [output truncated — full output saved to {}]\n\
             Use the read tool to access the full output if needed.",
            p.display()
        ),
        None => format!("{truncated}\n\n... [output truncated]"),
    }
}

/// Tail-truncation: keep the last `max_size` bytes (for bash output where errors
/// appear at the end).
fn truncate_tail_with_path_notice(content: &str, max_size: usize, path: Option<&Path>) -> String {
    let start = content.len().saturating_sub(max_size);
    let mut idx = start;
    while idx < content.len() && !content.is_char_boundary(idx) {
        idx += 1;
    }
    let truncated = &content[idx..];
    let omitted_lines = content[..idx].lines().count();

    let notice = match path {
        Some(p) => format!(
            "[... first {omitted_lines} lines omitted, showing last portion — full output saved to {}]\n\
             Use the read tool to access the full output if needed.\n\n",
            p.display()
        ),
        None => format!(
            "[... first {omitted_lines} lines omitted, showing last portion]\n\n"
        ),
    };

    format!("{notice}{truncated}")
}

/// Default inline size limit for tool outputs (chars).
/// Outputs exceeding this are saved to disk with a pointer.
pub const DEFAULT_MAX_INLINE_SIZE: usize = 50_000;

/// F6 — Per-tool inline size limits.
///
/// Returns the maximum inline size for a given tool. Tools that produce
/// large outputs (grep, bash) get smaller limits to keep the context lean.
/// `read` gets a generous limit since the LLM often needs full file content.
pub fn tool_inline_limit(tool_name: &str) -> usize {
    match tool_name {
        "grep" => 20_000,
        "bash" => 30_000,
        "web_fetch" => 30_000,
        "web_search" => 20_000,
        "glob" => 20_000,
        "read" => 100_000,           // generous — LLM needs file content
        "edit" | "write" => 100_000, // edit results include diffs
        _ => DEFAULT_MAX_INLINE_SIZE,
    }
}

/// Large response threshold (200K chars, inspired by Goose).
/// Used for very large tool outputs that should always spill to disk.
pub const LARGE_RESPONSE_THRESHOLD: usize = 200_000;

/// Check if a tool response is "large" and should be spilled to disk
/// even without explicit truncation. Returns the spillover path if saved.
pub fn spill_large_response(tool_name: &str, content: &str) -> Option<PathBuf> {
    if content.len() <= LARGE_RESPONSE_THRESHOLD {
        return None;
    }

    let output_dir = dirs::home_dir()?.join(".ava/tool-output");
    std::fs::create_dir_all(&output_dir).ok()?;

    let timestamp = chrono::Utc::now().format("%Y%m%d-%H%M%S%.3f");
    let filename = format!("{tool_name}-large-{timestamp}.txt");
    let path = output_dir.join(&filename);

    std::fs::write(&path, content).ok()?;
    Some(path)
}

/// Return the tool-output directory path.
pub fn output_dir() -> Option<PathBuf> {
    dirs::home_dir().map(|h| h.join(".ava/tool-output"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn small_output_returned_unchanged() {
        let content = "hello world";
        let result = save_tool_output_fallback("test", content, 1024);
        assert_eq!(result, content);
    }

    #[test]
    fn large_output_truncated_with_path() {
        let content = "x".repeat(2000);
        let result = save_tool_output_fallback("test", &content, 100);
        assert!(result.contains("[output truncated"));
        assert!(result.contains("read tool"));
        // The inline portion should be roughly max_inline_size
        let first_line_len = result.lines().next().unwrap().len();
        assert!(first_line_len <= 110); // some tolerance for char boundaries
    }

    #[test]
    fn cleanup_does_not_panic_on_missing_dir() {
        cleanup_old_outputs(7);
    }

    #[test]
    fn small_output_tail_returned_unchanged() {
        let content = "hello world";
        let result = save_tool_output_fallback_tail("test", content, 1024);
        assert_eq!(result, content);
    }

    #[test]
    fn large_output_tail_truncated_keeps_end() {
        // Build content with numbered lines so we can verify tail is kept
        let lines: Vec<String> = (1..=200).map(|i| format!("line {i}")).collect();
        let content = lines.join("\n");
        let result = save_tool_output_fallback_tail("test", &content, 200);
        assert!(result.contains("lines omitted, showing last portion"));
        assert!(result.contains("line 200"));
        // Should NOT contain the very first lines
        assert!(!result.contains("line 1\n"));
    }
}
