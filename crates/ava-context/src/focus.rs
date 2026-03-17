//! Focus chain — tracks recently accessed files/symbols for context injection.
//!
//! The focus chain records file accesses (read, write, edit) and provides a ranked
//! list of recently touched files. This helps the agent prioritize context about
//! files the user is actively working with.

use std::collections::HashMap;
use std::time::{Duration, Instant};

/// Maximum number of entries before pruning oldest.
const MAX_ENTRIES: usize = 50;

/// Maximum age before an entry is pruned.
const MAX_AGE: Duration = Duration::from_secs(30 * 60); // 30 minutes

/// The type of file access.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AccessKind {
    Read,
    Write,
    Edit,
}

impl std::fmt::Display for AccessKind {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            AccessKind::Read => write!(f, "read"),
            AccessKind::Write => write!(f, "write"),
            AccessKind::Edit => write!(f, "edit"),
        }
    }
}

/// A single entry in the focus chain.
#[derive(Debug, Clone)]
pub struct FocusEntry {
    pub path: String,
    pub access_count: u32,
    pub last_accessed: Instant,
    pub kind: AccessKind,
}

/// Tracks recently accessed files for context-aware prioritization.
///
/// Records file accesses and provides a recency-ranked list of focused files.
/// Auto-prunes entries that exceed the maximum count or age threshold.
#[derive(Debug)]
pub struct FocusChain {
    entries: HashMap<String, FocusEntry>,
}

impl Default for FocusChain {
    fn default() -> Self {
        Self::new()
    }
}

impl FocusChain {
    /// Create a new empty focus chain.
    pub fn new() -> Self {
        Self {
            entries: HashMap::new(),
        }
    }

    /// Record a file access, updating the entry or creating a new one.
    pub fn record_access(&mut self, path: &str, kind: AccessKind) {
        let now = Instant::now();

        if let Some(entry) = self.entries.get_mut(path) {
            entry.access_count += 1;
            entry.last_accessed = now;
            entry.kind = kind;
        } else {
            self.entries.insert(
                path.to_string(),
                FocusEntry {
                    path: path.to_string(),
                    access_count: 1,
                    last_accessed: now,
                    kind,
                },
            );
        }

        self.prune();
    }

    /// Return a ranked list of focused files, most recent first.
    pub fn get_focused(&self) -> Vec<FocusEntry> {
        let now = Instant::now();
        let mut entries: Vec<FocusEntry> = self
            .entries
            .values()
            .filter(|e| now.duration_since(e.last_accessed) < MAX_AGE)
            .cloned()
            .collect();

        // Sort by most recent first
        entries.sort_by(|a, b| b.last_accessed.cmp(&a.last_accessed));
        entries
    }

    /// Generate a compact context hint summarizing the focus chain.
    ///
    /// Format: one line per focused file with access count and kind.
    pub fn context_hint(&self) -> String {
        let focused = self.get_focused();
        if focused.is_empty() {
            return String::new();
        }

        let mut lines = Vec::with_capacity(focused.len() + 1);
        lines.push("Recently focused files:".to_string());
        for entry in &focused {
            lines.push(format!(
                "  {} ({}, {}x)",
                entry.path, entry.kind, entry.access_count
            ));
        }
        lines.join("\n")
    }

    /// Prune entries that exceed the maximum count or age threshold.
    fn prune(&mut self) {
        let now = Instant::now();

        // Remove entries older than MAX_AGE
        self.entries
            .retain(|_, entry| now.duration_since(entry.last_accessed) < MAX_AGE);

        // If still over MAX_ENTRIES, remove oldest entries
        if self.entries.len() > MAX_ENTRIES {
            let mut entries: Vec<(String, Instant)> = self
                .entries
                .iter()
                .map(|(k, v)| (k.clone(), v.last_accessed))
                .collect();
            entries.sort_by(|a, b| b.1.cmp(&a.1));

            let to_remove: Vec<String> = entries
                .into_iter()
                .skip(MAX_ENTRIES)
                .map(|(k, _)| k)
                .collect();

            for key in to_remove {
                self.entries.remove(&key);
            }
        }
    }

    /// Return the number of entries currently tracked.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Return true if no entries are tracked.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn record_access_creates_entry() {
        let mut chain = FocusChain::new();
        chain.record_access("src/main.rs", AccessKind::Read);

        assert_eq!(chain.len(), 1);
        let focused = chain.get_focused();
        assert_eq!(focused.len(), 1);
        assert_eq!(focused[0].path, "src/main.rs");
        assert_eq!(focused[0].access_count, 1);
        assert_eq!(focused[0].kind, AccessKind::Read);
    }

    #[test]
    fn record_access_increments_count() {
        let mut chain = FocusChain::new();
        chain.record_access("src/main.rs", AccessKind::Read);
        chain.record_access("src/main.rs", AccessKind::Edit);
        chain.record_access("src/main.rs", AccessKind::Write);

        let focused = chain.get_focused();
        assert_eq!(focused.len(), 1);
        assert_eq!(focused[0].access_count, 3);
        // Kind should be the most recent
        assert_eq!(focused[0].kind, AccessKind::Write);
    }

    #[test]
    fn ranking_by_recency() {
        let mut chain = FocusChain::new();
        chain.record_access("src/old.rs", AccessKind::Read);
        chain.record_access("src/mid.rs", AccessKind::Read);
        chain.record_access("src/new.rs", AccessKind::Read);

        let focused = chain.get_focused();
        assert_eq!(focused.len(), 3);
        // Most recent first
        assert_eq!(focused[0].path, "src/new.rs");
        assert_eq!(focused[1].path, "src/mid.rs");
        assert_eq!(focused[2].path, "src/old.rs");
    }

    #[test]
    fn pruning_by_count() {
        let mut chain = FocusChain::new();

        // Add more than MAX_ENTRIES
        for i in 0..60 {
            chain.record_access(&format!("src/file_{i}.rs"), AccessKind::Read);
        }

        // Should be pruned to MAX_ENTRIES
        assert!(chain.len() <= MAX_ENTRIES);
    }

    #[test]
    fn context_hint_format() {
        let mut chain = FocusChain::new();
        chain.record_access("src/main.rs", AccessKind::Read);
        chain.record_access("src/lib.rs", AccessKind::Edit);

        let hint = chain.context_hint();
        assert!(hint.starts_with("Recently focused files:"));
        assert!(hint.contains("src/main.rs"));
        assert!(hint.contains("src/lib.rs"));
        assert!(hint.contains("read"));
        assert!(hint.contains("edit"));
    }

    #[test]
    fn context_hint_empty() {
        let chain = FocusChain::new();
        assert_eq!(chain.context_hint(), "");
    }

    #[test]
    fn empty_chain() {
        let chain = FocusChain::new();
        assert!(chain.is_empty());
        assert_eq!(chain.len(), 0);
        assert!(chain.get_focused().is_empty());
    }

    #[test]
    fn multiple_files_different_kinds() {
        let mut chain = FocusChain::new();
        chain.record_access("Cargo.toml", AccessKind::Read);
        chain.record_access("src/main.rs", AccessKind::Edit);
        chain.record_access("README.md", AccessKind::Write);

        assert_eq!(chain.len(), 3);

        let focused = chain.get_focused();
        assert_eq!(focused[0].path, "README.md");
        assert_eq!(focused[0].kind, AccessKind::Write);
    }

    #[test]
    fn re_access_moves_to_front() {
        let mut chain = FocusChain::new();
        chain.record_access("src/old.rs", AccessKind::Read);
        chain.record_access("src/new.rs", AccessKind::Read);
        // Re-access the old file
        chain.record_access("src/old.rs", AccessKind::Edit);

        let focused = chain.get_focused();
        // old.rs should now be first since it was accessed most recently
        assert_eq!(focused[0].path, "src/old.rs");
        assert_eq!(focused[0].access_count, 2);
    }
}
