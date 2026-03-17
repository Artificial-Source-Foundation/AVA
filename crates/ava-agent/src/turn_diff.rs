//! Turn-level diff tracker for agent file modifications.
//!
//! Snapshots file content before tool execution and computes unified diffs
//! after modifications, providing per-turn change summaries.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use similar::{ChangeTag, TextDiff};

/// Aggregated diff information for a single agent turn.
#[derive(Debug, Clone, Default)]
pub struct TurnDiff {
    /// Per-file diff details.
    pub files_changed: Vec<FileDiff>,
    /// Total lines added across all files.
    pub total_additions: usize,
    /// Total lines deleted across all files.
    pub total_deletions: usize,
}

/// Diff details for a single file.
#[derive(Debug, Clone)]
pub struct FileDiff {
    /// Path to the changed file.
    pub path: PathBuf,
    /// Number of lines added.
    pub additions: usize,
    /// Number of lines deleted.
    pub deletions: usize,
    /// Unified diff text.
    pub diff_text: String,
}

/// Tracks file changes within a single agent turn.
///
/// Usage:
/// 1. Call `snapshot_file` before a tool modifies a file.
/// 2. Call `record_change` after the tool finishes.
/// 3. Call `get_turn_diff` to retrieve the aggregated diff.
/// 4. Call `clear` to reset for the next turn.
#[derive(Debug, Default)]
pub struct TurnDiffTracker {
    /// Pre-modification snapshots keyed by canonical path.
    snapshots: HashMap<PathBuf, String>,
    /// Computed diffs for this turn, keyed by canonical path.
    diffs: HashMap<PathBuf, FileDiff>,
}

impl TurnDiffTracker {
    /// Create a new empty tracker.
    pub fn new() -> Self {
        Self::default()
    }

    /// Snapshot a file's content before modification.
    ///
    /// If the file does not exist (e.g., it will be created), an empty
    /// snapshot is stored so that the creation diff shows all lines as added.
    pub fn snapshot_file(&mut self, path: &Path) {
        let canonical = normalize_path(path);
        let content = std::fs::read_to_string(path).unwrap_or_default();
        self.snapshots.insert(canonical, content);
    }

    /// Record a file change after modification by diffing against the snapshot.
    ///
    /// If no snapshot exists for this path, the file is treated as entirely new.
    /// If the file no longer exists (deleted), the diff shows all lines removed.
    pub fn record_change(&mut self, path: &Path) {
        let canonical = normalize_path(path);
        let before = self.snapshots.get(&canonical).cloned().unwrap_or_default();
        let after = std::fs::read_to_string(path).unwrap_or_default();

        if before == after {
            return;
        }

        let diff = compute_diff(&canonical, &before, &after);
        self.diffs.insert(canonical, diff);
    }

    /// Return the aggregated diff for this turn.
    pub fn get_turn_diff(&self) -> TurnDiff {
        let mut files_changed: Vec<FileDiff> = self.diffs.values().cloned().collect();
        files_changed.sort_by(|a, b| a.path.cmp(&b.path));

        let total_additions = files_changed.iter().map(|f| f.additions).sum();
        let total_deletions = files_changed.iter().map(|f| f.deletions).sum();

        TurnDiff {
            files_changed,
            total_additions,
            total_deletions,
        }
    }

    /// Reset the tracker for the next turn.
    pub fn clear(&mut self) {
        self.snapshots.clear();
        self.diffs.clear();
    }
}

/// Normalize a path for consistent map keys. Uses canonicalize when possible,
/// falls back to the original path (for files that don't exist yet).
fn normalize_path(path: &Path) -> PathBuf {
    std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf())
}

/// Compute a unified diff between `before` and `after` content.
fn compute_diff(path: &Path, before: &str, after: &str) -> FileDiff {
    let text_diff = TextDiff::from_lines(before, after);

    let mut additions = 0usize;
    let mut deletions = 0usize;

    for change in text_diff.iter_all_changes() {
        match change.tag() {
            ChangeTag::Insert => additions += 1,
            ChangeTag::Delete => deletions += 1,
            ChangeTag::Equal => {}
        }
    }

    let diff_text = text_diff
        .unified_diff()
        .context_radius(3)
        .header(
            &format!("a/{}", path.display()),
            &format!("b/{}", path.display()),
        )
        .to_string();

    FileDiff {
        path: path.to_path_buf(),
        additions,
        deletions,
        diff_text,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn snapshot_and_diff_modification() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("test.rs");
        fs::write(&file, "fn main() {\n    println!(\"hello\");\n}\n").unwrap();

        let mut tracker = TurnDiffTracker::new();
        tracker.snapshot_file(&file);

        fs::write(&file, "fn main() {\n    println!(\"hello world\");\n}\n").unwrap();
        tracker.record_change(&file);

        let diff = tracker.get_turn_diff();
        assert_eq!(diff.files_changed.len(), 1);
        assert_eq!(diff.total_additions, 1);
        assert_eq!(diff.total_deletions, 1);

        let fd = &diff.files_changed[0];
        assert!(fd.diff_text.contains("-    println!(\"hello\");"));
        assert!(fd.diff_text.contains("+    println!(\"hello world\");"));
    }

    #[test]
    fn multiple_files() {
        let dir = TempDir::new().unwrap();
        let file_a = dir.path().join("a.txt");
        let file_b = dir.path().join("b.txt");
        fs::write(&file_a, "line1\nline2\n").unwrap();
        fs::write(&file_b, "alpha\nbeta\n").unwrap();

        let mut tracker = TurnDiffTracker::new();
        tracker.snapshot_file(&file_a);
        tracker.snapshot_file(&file_b);

        fs::write(&file_a, "line1\nline2\nline3\n").unwrap();
        fs::write(&file_b, "alpha\ngamma\n").unwrap();
        tracker.record_change(&file_a);
        tracker.record_change(&file_b);

        let diff = tracker.get_turn_diff();
        assert_eq!(diff.files_changed.len(), 2);
        // file_a: +1 addition (line3)
        // file_b: +1 addition (gamma), +1 deletion (beta)
        assert_eq!(diff.total_additions, 2);
        assert_eq!(diff.total_deletions, 1);
    }

    #[test]
    fn new_file_creation() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("new.txt");

        let mut tracker = TurnDiffTracker::new();
        // Snapshot a file that doesn't exist yet — stores empty content.
        tracker.snapshot_file(&file);

        fs::write(&file, "first line\nsecond line\n").unwrap();
        tracker.record_change(&file);

        let diff = tracker.get_turn_diff();
        assert_eq!(diff.files_changed.len(), 1);
        assert_eq!(diff.total_additions, 2);
        assert_eq!(diff.total_deletions, 0);
    }

    #[test]
    fn file_deletion() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("doomed.txt");
        fs::write(&file, "goodbye\ncruel\nworld\n").unwrap();

        let mut tracker = TurnDiffTracker::new();
        tracker.snapshot_file(&file);

        fs::remove_file(&file).unwrap();
        tracker.record_change(&file);

        let diff = tracker.get_turn_diff();
        assert_eq!(diff.files_changed.len(), 1);
        assert_eq!(diff.total_additions, 0);
        assert_eq!(diff.total_deletions, 3);
    }

    #[test]
    fn no_change_produces_empty_diff() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("stable.txt");
        fs::write(&file, "unchanged\n").unwrap();

        let mut tracker = TurnDiffTracker::new();
        tracker.snapshot_file(&file);
        // Don't modify the file.
        tracker.record_change(&file);

        let diff = tracker.get_turn_diff();
        assert!(diff.files_changed.is_empty());
        assert_eq!(diff.total_additions, 0);
        assert_eq!(diff.total_deletions, 0);
    }

    #[test]
    fn clear_resets_tracker() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("reset.txt");
        fs::write(&file, "before\n").unwrap();

        let mut tracker = TurnDiffTracker::new();
        tracker.snapshot_file(&file);
        fs::write(&file, "after\n").unwrap();
        tracker.record_change(&file);

        assert_eq!(tracker.get_turn_diff().files_changed.len(), 1);

        tracker.clear();

        let diff = tracker.get_turn_diff();
        assert!(diff.files_changed.is_empty());
        assert_eq!(diff.total_additions, 0);
    }

    #[test]
    fn record_without_snapshot_treats_as_new() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("surprise.txt");
        fs::write(&file, "appeared\n").unwrap();

        let mut tracker = TurnDiffTracker::new();
        // No snapshot — treated as new file (empty before).
        tracker.record_change(&file);

        let diff = tracker.get_turn_diff();
        assert_eq!(diff.files_changed.len(), 1);
        assert_eq!(diff.total_additions, 1);
        assert_eq!(diff.total_deletions, 0);
    }
}
