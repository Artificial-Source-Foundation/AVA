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
}
