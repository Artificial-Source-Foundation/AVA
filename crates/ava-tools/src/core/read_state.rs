//! F10 — Stale file detection via a shared read-state cache.
//!
//! The `ReadStateCache` records file metadata (mtime, token count, turn number)
//! on every `read` call. Before `edit`/`write` executes, it checks whether the
//! file's mtime has changed since the last read — if so, a warning is prepended
//! to the tool result.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::{Arc, RwLock};
use std::time::SystemTime;

/// Maximum number of entries in the cache (LRU-style cap).
const MAX_ENTRIES: usize = 200;

/// Per-file read state recorded after each `read` tool call.
#[derive(Debug, Clone)]
pub struct ReadState {
    /// Last-known modification time at the time of read.
    pub mtime: SystemTime,
    /// Approximate token count of the content.
    pub token_count: usize,
    /// Agent turn number when the read occurred.
    pub turn: usize,
}

/// Thread-safe, shared cache for file read states.
pub type ReadStateCache = Arc<RwLock<ReadStateCacheInner>>;

/// Create a new empty `ReadStateCache`.
pub fn new_read_state_cache() -> ReadStateCache {
    Arc::new(RwLock::new(ReadStateCacheInner::new()))
}

/// Inner storage for the read-state cache with LRU eviction.
#[derive(Debug)]
pub struct ReadStateCacheInner {
    entries: HashMap<PathBuf, ReadState>,
    /// Insertion order for LRU eviction.
    order: Vec<PathBuf>,
}

impl ReadStateCacheInner {
    fn new() -> Self {
        Self {
            entries: HashMap::new(),
            order: Vec::new(),
        }
    }

    /// Record a file read. If the cache exceeds `MAX_ENTRIES`, the oldest entry
    /// is evicted.
    pub fn record_read(
        &mut self,
        path: PathBuf,
        mtime: SystemTime,
        token_count: usize,
        turn: usize,
    ) {
        tracing::debug!(path = %path.display(), turn, "F10: recorded file read");
        // Remove existing entry from order vec (promote to end)
        self.order.retain(|p| p != &path);
        self.order.push(path.clone());

        self.entries.insert(
            path,
            ReadState {
                mtime,
                token_count,
                turn,
            },
        );

        // Evict oldest if over capacity
        while self.order.len() > MAX_ENTRIES {
            if let Some(old) = self.order.first().cloned() {
                self.order.remove(0);
                self.entries.remove(&old);
            }
        }
    }

    /// Check if a file has been modified since the last read.
    /// Returns `Some(warning_message)` if stale, `None` if fresh or not tracked.
    pub fn check_stale(&self, path: &PathBuf, current_mtime: SystemTime) -> Option<String> {
        let state = self.entries.get(path)?;
        if current_mtime != state.mtime {
            tracing::warn!(path = %path.display(), read_turn = state.turn, "F10: stale file detected, modified since last read");
            Some(format!(
                "WARNING: File '{}' has been modified since you last read it (turn {}). \
                 The content may have changed. Consider re-reading before editing.",
                path.display(),
                state.turn,
            ))
        } else {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn record_and_check_fresh() {
        let cache = new_read_state_cache();
        let path = PathBuf::from("/tmp/test.rs");
        let mtime = SystemTime::now();

        cache
            .write()
            .unwrap()
            .record_read(path.clone(), mtime, 100, 1);
        let warning = cache.read().unwrap().check_stale(&path, mtime);
        assert!(warning.is_none());
    }

    #[test]
    fn check_stale_detects_mtime_change() {
        let cache = new_read_state_cache();
        let path = PathBuf::from("/tmp/test.rs");
        let old_mtime = SystemTime::now();
        let new_mtime = old_mtime + Duration::from_secs(5);

        cache
            .write()
            .unwrap()
            .record_read(path.clone(), old_mtime, 100, 1);
        let warning = cache.read().unwrap().check_stale(&path, new_mtime);
        assert!(warning.is_some());
        assert!(warning.unwrap().contains("modified since you last read it"));
    }

    #[test]
    fn lru_eviction_at_capacity() {
        let cache = new_read_state_cache();
        let mtime = SystemTime::now();

        // Fill beyond capacity
        for i in 0..=MAX_ENTRIES {
            let path = PathBuf::from(format!("/tmp/file_{i}.rs"));
            cache.write().unwrap().record_read(path, mtime, 10, i);
        }

        // First entry should be evicted
        let first = PathBuf::from("/tmp/file_0.rs");
        assert!(cache.read().unwrap().check_stale(&first, mtime).is_none());
        // But entries are not "stale" — they just don't exist anymore
        assert!(!cache.read().unwrap().entries.contains_key(&first));

        // Last entry should still exist
        let last = PathBuf::from(format!("/tmp/file_{MAX_ENTRIES}.rs"));
        assert!(cache.read().unwrap().entries.contains_key(&last));
    }

    #[test]
    fn unknown_file_returns_none() {
        let cache = new_read_state_cache();
        let path = PathBuf::from("/tmp/unknown.rs");
        let mtime = SystemTime::now();
        assert!(cache.read().unwrap().check_stale(&path, mtime).is_none());
    }
}
