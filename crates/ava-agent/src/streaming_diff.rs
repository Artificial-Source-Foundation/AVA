//! Streaming diff tracker for incremental file edit visualization.
//!
//! Tracks file modifications made by write/edit tool calls and computes
//! unified diffs that can be emitted as [`DiffEvent`]s for UI display.
//! This enables the TUI and desktop frontend to show what changed in each
//! file as tool calls complete, providing a "streaming diff" UX without
//! requiring changes to the LLM streaming parser.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use similar::{ChangeTag, TextDiff};

/// Tools whose output should be tracked for diff computation.
pub const DIFF_TRACKED_TOOLS: &[&str] = &["edit", "write", "multiedit", "apply_patch"];

/// Tracks pending file edits and computes diffs as tool calls complete.
#[derive(Debug, Default)]
pub struct StreamingDiffTracker {
    /// Pre-modification snapshots keyed by canonical path.
    pending_edits: HashMap<PathBuf, PendingEdit>,
}

/// State for a single file being edited.
#[derive(Debug, Clone)]
pub struct PendingEdit {
    /// Path to the file being edited.
    pub file_path: PathBuf,
    /// File content before modification (empty for new files).
    pub original_content: String,
    /// Accumulated new content from the completed edit.
    pub streamed_content: String,
    /// Whether the edit is complete.
    pub complete: bool,
}

/// Events emitted by the streaming diff tracker.
#[derive(Debug, Clone)]
pub enum DiffEvent {
    /// A file edit has started — the original content was snapshotted.
    EditStarted { file: PathBuf },
    /// The edit is complete — contains the unified diff.
    EditComplete {
        file: PathBuf,
        diff_text: String,
        additions: usize,
        deletions: usize,
    },
    /// Preview of what changed so far (summary only).
    DiffPreview {
        file: PathBuf,
        additions: usize,
        deletions: usize,
    },
}

impl StreamingDiffTracker {
    /// Create a new empty tracker.
    pub fn new() -> Self {
        Self::default()
    }

    /// Snapshot a file before a write/edit tool modifies it.
    ///
    /// Call this before executing a tool that modifies `path`.
    /// If the file does not exist yet, stores an empty snapshot
    /// so the diff shows all lines as additions.
    pub fn snapshot_before_edit(&mut self, path: &Path) -> DiffEvent {
        let canonical = normalize_path(path);
        let content = std::fs::read_to_string(path).unwrap_or_default();
        self.pending_edits.insert(
            canonical.clone(),
            PendingEdit {
                file_path: canonical.clone(),
                original_content: content,
                streamed_content: String::new(),
                complete: false,
            },
        );
        DiffEvent::EditStarted { file: canonical }
    }

    /// Record that a file edit is complete and compute the diff.
    ///
    /// Reads the file's current content from disk and diffs it against
    /// the pre-edit snapshot. Returns `None` if there was no snapshot
    /// for this file or if the content did not change.
    pub fn record_edit_complete(&mut self, path: &Path) -> Option<DiffEvent> {
        let canonical = normalize_path(path);
        let pending = self.pending_edits.get_mut(&canonical)?;

        let after = std::fs::read_to_string(path).unwrap_or_default();

        if pending.original_content == after {
            self.pending_edits.remove(&canonical);
            return None;
        }

        pending.streamed_content = after.clone();
        pending.complete = true;

        let (diff_text, additions, deletions) =
            compute_unified_diff(&pending.original_content, &after, &canonical);

        Some(DiffEvent::EditComplete {
            file: canonical,
            diff_text,
            additions,
            deletions,
        })
    }

    /// Compute a preview diff for a file that is still being edited.
    ///
    /// Reads the file's current content from disk and diffs it against
    /// the snapshot. Returns `None` if no snapshot exists or content
    /// has not changed.
    pub fn preview(&self, path: &Path) -> Option<DiffEvent> {
        let canonical = normalize_path(path);
        let pending = self.pending_edits.get(&canonical)?;

        let current = std::fs::read_to_string(path).unwrap_or_default();
        if pending.original_content == current {
            return None;
        }

        let (_diff_text, additions, deletions) =
            compute_unified_diff(&pending.original_content, &current, &canonical);

        Some(DiffEvent::DiffPreview {
            file: canonical,
            additions,
            deletions,
        })
    }

    /// Get all completed diffs and drain them from the tracker.
    pub fn drain_completed(&mut self) -> Vec<DiffEvent> {
        let completed: Vec<PathBuf> = self
            .pending_edits
            .iter()
            .filter(|(_, edit)| edit.complete)
            .map(|(path, _)| path.clone())
            .collect();

        let mut events = Vec::new();
        for path in completed {
            if let Some(edit) = self.pending_edits.remove(&path) {
                let (diff_text, additions, deletions) =
                    compute_unified_diff(&edit.original_content, &edit.streamed_content, &path);
                events.push(DiffEvent::EditComplete {
                    file: path,
                    diff_text,
                    additions,
                    deletions,
                });
            }
        }
        events
    }

    /// Check whether a tool name should be tracked for diffs.
    pub fn is_tracked_tool(tool_name: &str) -> bool {
        DIFF_TRACKED_TOOLS.contains(&tool_name)
    }

    /// Reset the tracker, discarding all pending edits.
    pub fn clear(&mut self) {
        self.pending_edits.clear();
    }

    /// Number of files currently being tracked.
    pub fn pending_count(&self) -> usize {
        self.pending_edits.len()
    }
}

/// Normalize a path for consistent map keys. Uses canonicalize when possible,
/// falls back to the original path (for files that don't exist yet).
fn normalize_path(path: &Path) -> PathBuf {
    std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf())
}

/// Compute a unified diff between `old` and `new` content.
///
/// Returns `(diff_text, additions, deletions)`.
pub fn compute_unified_diff(old: &str, new: &str, path: &Path) -> (String, usize, usize) {
    let text_diff = TextDiff::from_lines(old, new);

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

    (diff_text, additions, deletions)
}

/// Format a compact diff summary suitable for tool result output.
pub fn format_diff_summary(additions: usize, deletions: usize) -> String {
    match (additions, deletions) {
        (0, 0) => "no changes".to_string(),
        (a, 0) => format!("+{a} lines"),
        (0, d) => format!("-{d} lines"),
        (a, d) => format!("+{a} -{d} lines"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn compute_diff_basic() {
        let old = "line1\nline2\nline3\n";
        let new = "line1\nmodified\nline3\n";
        let path = PathBuf::from("test.rs");

        let (diff_text, additions, deletions) = compute_unified_diff(old, new, &path);

        assert_eq!(additions, 1);
        assert_eq!(deletions, 1);
        assert!(diff_text.contains("-line2"));
        assert!(diff_text.contains("+modified"));
    }

    #[test]
    fn compute_diff_additions_only() {
        let old = "line1\n";
        let new = "line1\nline2\nline3\n";
        let path = PathBuf::from("test.rs");

        let (_, additions, deletions) = compute_unified_diff(old, new, &path);

        assert_eq!(additions, 2);
        assert_eq!(deletions, 0);
    }

    #[test]
    fn compute_diff_deletions_only() {
        let old = "line1\nline2\nline3\n";
        let new = "line1\n";
        let path = PathBuf::from("test.rs");

        let (_, additions, deletions) = compute_unified_diff(old, new, &path);

        assert_eq!(additions, 0);
        assert_eq!(deletions, 2);
    }

    #[test]
    fn compute_diff_no_changes() {
        let content = "unchanged\n";
        let path = PathBuf::from("test.rs");

        let (diff_text, additions, deletions) = compute_unified_diff(content, content, &path);

        assert_eq!(additions, 0);
        assert_eq!(deletions, 0);
        assert!(diff_text.is_empty());
    }

    #[test]
    fn tracker_snapshot_and_complete() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("test.rs");
        fs::write(&file, "fn main() {\n    println!(\"hello\");\n}\n").unwrap();

        let mut tracker = StreamingDiffTracker::new();

        // Snapshot before edit
        let start_event = tracker.snapshot_before_edit(&file);
        assert!(matches!(start_event, DiffEvent::EditStarted { .. }));
        assert_eq!(tracker.pending_count(), 1);

        // Modify the file
        fs::write(&file, "fn main() {\n    println!(\"hello world\");\n}\n").unwrap();

        // Record completion
        let event = tracker.record_edit_complete(&file).unwrap();
        match event {
            DiffEvent::EditComplete {
                additions,
                deletions,
                diff_text,
                ..
            } => {
                assert_eq!(additions, 1);
                assert_eq!(deletions, 1);
                assert!(diff_text.contains("-    println!(\"hello\");"));
                assert!(diff_text.contains("+    println!(\"hello world\");"));
            }
            _ => panic!("expected EditComplete event"),
        }
    }

    #[test]
    fn tracker_no_change_returns_none() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("stable.txt");
        fs::write(&file, "unchanged\n").unwrap();

        let mut tracker = StreamingDiffTracker::new();
        tracker.snapshot_before_edit(&file);

        // Don't modify the file
        let event = tracker.record_edit_complete(&file);
        assert!(event.is_none());
    }

    #[test]
    fn tracker_new_file_creation() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("new.txt");

        let mut tracker = StreamingDiffTracker::new();
        tracker.snapshot_before_edit(&file);

        // Create the file
        fs::write(&file, "first line\nsecond line\n").unwrap();

        let event = tracker.record_edit_complete(&file).unwrap();
        match event {
            DiffEvent::EditComplete {
                additions,
                deletions,
                ..
            } => {
                assert_eq!(additions, 2);
                assert_eq!(deletions, 0);
            }
            _ => panic!("expected EditComplete event"),
        }
    }

    #[test]
    fn tracker_preview() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("preview.txt");
        fs::write(&file, "original\n").unwrap();

        let mut tracker = StreamingDiffTracker::new();
        tracker.snapshot_before_edit(&file);

        // Modify file
        fs::write(&file, "original\nnew line\n").unwrap();

        let event = tracker.preview(&file).unwrap();
        match event {
            DiffEvent::DiffPreview {
                additions,
                deletions,
                ..
            } => {
                assert_eq!(additions, 1);
                assert_eq!(deletions, 0);
            }
            _ => panic!("expected DiffPreview event"),
        }
    }

    #[test]
    fn tracker_preview_no_change_returns_none() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("stable.txt");
        fs::write(&file, "unchanged\n").unwrap();

        let mut tracker = StreamingDiffTracker::new();
        tracker.snapshot_before_edit(&file);

        assert!(tracker.preview(&file).is_none());
    }

    #[test]
    fn tracker_drain_completed() {
        let dir = TempDir::new().unwrap();
        let file_a = dir.path().join("a.txt");
        let file_b = dir.path().join("b.txt");
        fs::write(&file_a, "old a\n").unwrap();
        fs::write(&file_b, "old b\n").unwrap();

        let mut tracker = StreamingDiffTracker::new();
        tracker.snapshot_before_edit(&file_a);
        tracker.snapshot_before_edit(&file_b);

        fs::write(&file_a, "new a\n").unwrap();
        fs::write(&file_b, "new b\n").unwrap();

        // Complete both
        tracker.record_edit_complete(&file_a);
        tracker.record_edit_complete(&file_b);

        // Drain should return the completed ones (they were already consumed
        // by record_edit_complete removing unchanged, but the complete ones
        // remain in pending_edits with complete=true)
        let events = tracker.drain_completed();
        assert_eq!(events.len(), 2);
        assert_eq!(tracker.pending_count(), 0);
    }

    #[test]
    fn tracker_clear() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("clear.txt");
        fs::write(&file, "content\n").unwrap();

        let mut tracker = StreamingDiffTracker::new();
        tracker.snapshot_before_edit(&file);
        assert_eq!(tracker.pending_count(), 1);

        tracker.clear();
        assert_eq!(tracker.pending_count(), 0);
    }

    #[test]
    fn tracker_multiple_edits_same_file() {
        let dir = TempDir::new().unwrap();
        let file = dir.path().join("multi.txt");
        fs::write(&file, "v1\n").unwrap();

        let mut tracker = StreamingDiffTracker::new();

        // First edit
        tracker.snapshot_before_edit(&file);
        fs::write(&file, "v2\n").unwrap();
        let event = tracker.record_edit_complete(&file).unwrap();
        match &event {
            DiffEvent::EditComplete {
                additions,
                deletions,
                ..
            } => {
                assert_eq!(*additions, 1);
                assert_eq!(*deletions, 1);
            }
            _ => panic!("expected EditComplete"),
        }

        // Second edit — new snapshot should use current content
        tracker.snapshot_before_edit(&file);
        fs::write(&file, "v3\nv3b\n").unwrap();
        let event = tracker.record_edit_complete(&file).unwrap();
        match &event {
            DiffEvent::EditComplete {
                additions,
                deletions,
                ..
            } => {
                assert_eq!(*additions, 2);
                assert_eq!(*deletions, 1);
            }
            _ => panic!("expected EditComplete"),
        }
    }

    #[test]
    fn is_tracked_tool_check() {
        assert!(StreamingDiffTracker::is_tracked_tool("edit"));
        assert!(StreamingDiffTracker::is_tracked_tool("write"));
        assert!(StreamingDiffTracker::is_tracked_tool("multiedit"));
        assert!(StreamingDiffTracker::is_tracked_tool("apply_patch"));
        assert!(!StreamingDiffTracker::is_tracked_tool("read"));
        assert!(!StreamingDiffTracker::is_tracked_tool("bash"));
        assert!(!StreamingDiffTracker::is_tracked_tool("glob"));
    }

    #[test]
    fn format_diff_summary_variants() {
        assert_eq!(format_diff_summary(0, 0), "no changes");
        assert_eq!(format_diff_summary(5, 0), "+5 lines");
        assert_eq!(format_diff_summary(0, 3), "-3 lines");
        assert_eq!(format_diff_summary(10, 7), "+10 -7 lines");
    }

    #[test]
    fn compute_diff_header_format() {
        let old = "a\n";
        let new = "b\n";
        let path = PathBuf::from("src/main.rs");

        let (diff_text, _, _) = compute_unified_diff(old, new, &path);

        assert!(diff_text.contains("--- a/src/main.rs"));
        assert!(diff_text.contains("+++ b/src/main.rs"));
    }
}
