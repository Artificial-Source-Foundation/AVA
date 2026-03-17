//! File watcher with AI comment detection.
//!
//! Scans source files for special `// ai` and `# ai` comments that signal
//! the developer wants the agent to pay attention to a specific location.

use std::fs;
use std::path::{Path, PathBuf};

/// Urgency level of an AI comment.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Urgency {
    /// Normal priority (e.g., `// ai: refactor this`).
    Normal,
    /// Urgent priority (e.g., `// ai!: fix this bug`).
    Urgent,
}

/// A detected AI comment in a source file.
#[derive(Debug, Clone)]
pub struct AiComment {
    /// 1-based line number where the comment was found.
    pub line_number: usize,
    /// The full comment text (trimmed).
    pub comment_text: String,
    /// Whether this is a normal or urgent comment.
    pub urgency: Urgency,
}

/// AI comment prefixes to detect. Order matters — check urgent (`!`) first.
const AI_PREFIXES: &[(&str, bool)] = &[
    ("// ai!", true),
    ("# ai!", true),
    ("/* ai!", true),
    ("// ai", false),
    ("# ai", false),
    ("/* ai", false),
];

/// Detector for AI-directed comments in source files.
pub struct AiCommentDetector;

impl AiCommentDetector {
    /// Scan a single file for AI comments.
    pub fn scan_file(path: &Path) -> Vec<AiComment> {
        let Ok(content) = fs::read_to_string(path) else {
            return Vec::new();
        };

        Self::scan_content(&content)
    }

    /// Scan a string of content for AI comments (useful for testing).
    pub fn scan_content(content: &str) -> Vec<AiComment> {
        let mut comments = Vec::new();

        for (line_number, line) in content.lines().enumerate() {
            let trimmed = line.trim().to_lowercase();

            for &(prefix, is_urgent) in AI_PREFIXES {
                if trimmed.starts_with(prefix) {
                    comments.push(AiComment {
                        line_number: line_number + 1,
                        comment_text: line.trim().to_string(),
                        urgency: if is_urgent {
                            Urgency::Urgent
                        } else {
                            Urgency::Normal
                        },
                    });
                    break; // Only match the first (most specific) prefix
                }
            }
        }

        comments
    }

    /// Scan a directory for AI comments in files with the given extensions.
    ///
    /// Returns a list of (path, comments) pairs, excluding files with no AI comments.
    pub fn scan_directory(dir: &Path, extensions: &[&str]) -> Vec<(PathBuf, Vec<AiComment>)> {
        let mut results = Vec::new();

        let Ok(entries) = fs::read_dir(dir) else {
            return results;
        };

        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_file() {
                let matches_ext = path
                    .extension()
                    .and_then(|e| e.to_str())
                    .map(|e| extensions.contains(&e))
                    .unwrap_or(false);

                if matches_ext {
                    let comments = Self::scan_file(&path);
                    if !comments.is_empty() {
                        results.push((path, comments));
                    }
                }
            } else if path.is_dir() {
                results.extend(Self::scan_directory(&path, extensions));
            }
        }

        results
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn detect_rust_ai_comment() {
        let comments = AiCommentDetector::scan_content("fn main() {\n    // ai: refactor this\n}");
        assert_eq!(comments.len(), 1);
        assert_eq!(comments[0].line_number, 2);
        assert_eq!(comments[0].urgency, Urgency::Normal);
    }

    #[test]
    fn detect_urgent_ai_comment() {
        let comments = AiCommentDetector::scan_content("# ai!: fix this bug immediately");
        assert_eq!(comments.len(), 1);
        assert_eq!(comments[0].urgency, Urgency::Urgent);
    }

    #[test]
    fn detect_python_ai_comment() {
        let comments = AiCommentDetector::scan_content("# ai: optimize this loop\nx = 1");
        assert_eq!(comments.len(), 1);
        assert!(comments[0].comment_text.contains("ai"));
    }

    #[test]
    fn no_false_positives() {
        let comments =
            AiCommentDetector::scan_content("// this is a normal comment\nlet aim = 42;");
        assert_eq!(comments.len(), 0);
    }

    #[test]
    fn scan_directory_finds_comments() {
        let dir = TempDir::new().unwrap();
        let file_path = dir.path().join("test.rs");
        let mut file = std::fs::File::create(&file_path).unwrap();
        writeln!(file, "// ai: todo").unwrap();

        let results = AiCommentDetector::scan_directory(dir.path(), &["rs"]);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].1.len(), 1);
    }
}
