//! Shell environment snapshotting (BG2-15, inspired by Codex CLI).
//!
//! Captures the user's shell environment (env vars, cwd, shell) at session
//! start. Can be persisted to disk and restored on session resume for
//! consistent tool execution context.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

/// A snapshot of the shell environment at a point in time.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ShellSnapshot {
    /// Environment variables (sorted for deterministic serialization).
    pub env_vars: BTreeMap<String, String>,
    /// Current working directory.
    pub cwd: PathBuf,
    /// Shell path (e.g., /bin/zsh, /bin/bash).
    pub shell: String,
    /// Timestamp when the snapshot was taken.
    pub captured_at: chrono::DateTime<chrono::Utc>,
}

/// Environment variables that should never be captured (security/privacy).
const EXCLUDED_VARS: &[&str] = &[
    "SSH_AUTH_SOCK",
    "SSH_AGENT_PID",
    "GPG_AGENT_INFO",
    "AWS_SECRET_ACCESS_KEY",
    "AWS_SESSION_TOKEN",
    "GITHUB_TOKEN",
    "GH_TOKEN",
    "OPENAI_API_KEY",
    "ANTHROPIC_API_KEY",
    "DATABASE_URL",
    "DB_PASSWORD",
    "SECRET_KEY",
    "PRIVATE_KEY",
];

impl ShellSnapshot {
    /// Capture the current shell environment.
    pub fn capture() -> Self {
        let mut env_vars = BTreeMap::new();
        for (key, value) in std::env::vars() {
            // Skip excluded variables
            if EXCLUDED_VARS.iter().any(|e| key.eq_ignore_ascii_case(e)) {
                continue;
            }
            // Skip variables with very long values (likely not useful)
            if value.len() > 4096 {
                continue;
            }
            env_vars.insert(key, value);
        }

        let cwd = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        let shell = std::env::var("SHELL").unwrap_or_else(|_| "/bin/sh".to_string());

        Self {
            env_vars,
            cwd,
            shell,
            captured_at: chrono::Utc::now(),
        }
    }

    /// Compute the diff between this snapshot and a newer one.
    /// Returns (added, removed, changed) variable names.
    pub fn diff(&self, newer: &ShellSnapshot) -> SnapshotDiff {
        let mut added = Vec::new();
        let mut removed = Vec::new();
        let mut changed = Vec::new();

        // Find added and changed
        for (key, new_val) in &newer.env_vars {
            match self.env_vars.get(key) {
                None => added.push(key.clone()),
                Some(old_val) if old_val != new_val => changed.push(key.clone()),
                _ => {}
            }
        }

        // Find removed
        for key in self.env_vars.keys() {
            if !newer.env_vars.contains_key(key) {
                removed.push(key.clone());
            }
        }

        let cwd_changed = self.cwd != newer.cwd;

        SnapshotDiff {
            added,
            removed,
            changed,
            cwd_changed,
        }
    }

    /// Save snapshot to a file.
    pub fn save(&self, path: &Path) -> std::io::Result<()> {
        let json = serde_json::to_string_pretty(self).map_err(std::io::Error::other)?;
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::write(path, json)
    }

    /// Load snapshot from a file.
    pub fn load(path: &Path) -> std::io::Result<Self> {
        let data = std::fs::read_to_string(path)?;
        serde_json::from_str(&data).map_err(std::io::Error::other)
    }

    /// Generate a compact summary suitable for injecting into agent context.
    pub fn context_summary(&self) -> String {
        let mut lines = Vec::new();
        lines.push(format!("CWD: {}", self.cwd.display()));
        lines.push(format!("Shell: {}", self.shell));

        // Include select useful env vars
        let useful = [
            "LANG",
            "TERM",
            "EDITOR",
            "VISUAL",
            "GOPATH",
            "CARGO_HOME",
            "RUSTUP_HOME",
            "NODE_ENV",
            "VIRTUAL_ENV",
            "CONDA_DEFAULT_ENV",
        ];
        for key in useful {
            if let Some(val) = self.env_vars.get(key) {
                lines.push(format!("{key}={val}"));
            }
        }

        lines.join("\n")
    }
}

/// Differences between two shell snapshots.
#[derive(Debug, Clone)]
pub struct SnapshotDiff {
    pub added: Vec<String>,
    pub removed: Vec<String>,
    pub changed: Vec<String>,
    pub cwd_changed: bool,
}

impl SnapshotDiff {
    pub fn is_empty(&self) -> bool {
        self.added.is_empty()
            && self.removed.is_empty()
            && self.changed.is_empty()
            && !self.cwd_changed
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capture_snapshot() {
        let snap = ShellSnapshot::capture();
        assert!(!snap.env_vars.is_empty());
        assert!(!snap.cwd.as_os_str().is_empty());
    }

    #[test]
    fn excludes_sensitive_vars() {
        // Set a sensitive var temporarily
        std::env::set_var("AWS_SECRET_ACCESS_KEY", "test-secret");
        let snap = ShellSnapshot::capture();
        assert!(!snap.env_vars.contains_key("AWS_SECRET_ACCESS_KEY"));
        std::env::remove_var("AWS_SECRET_ACCESS_KEY");
    }

    #[test]
    fn diff_detects_changes() {
        let mut old = ShellSnapshot::capture();
        let mut new = old.clone();

        new.env_vars
            .insert("NEW_VAR".to_string(), "value".to_string());
        old.env_vars
            .insert("OLD_VAR".to_string(), "value".to_string());
        new.env_vars
            .insert("PATH".to_string(), "/new/path".to_string());
        old.env_vars
            .insert("PATH".to_string(), "/old/path".to_string());

        let diff = old.diff(&new);
        assert!(diff.added.contains(&"NEW_VAR".to_string()));
        assert!(diff.removed.contains(&"OLD_VAR".to_string()));
        assert!(diff.changed.contains(&"PATH".to_string()));
    }

    #[test]
    fn diff_no_changes() {
        let snap = ShellSnapshot::capture();
        let diff = snap.diff(&snap);
        assert!(diff.is_empty());
    }

    #[test]
    fn save_and_load_roundtrip() {
        let snap = ShellSnapshot::capture();
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("snapshot.json");

        snap.save(&path).unwrap();
        let loaded = ShellSnapshot::load(&path).unwrap();

        assert_eq!(snap.cwd, loaded.cwd);
        assert_eq!(snap.shell, loaded.shell);
        assert_eq!(snap.env_vars.len(), loaded.env_vars.len());
    }

    #[test]
    fn context_summary_includes_cwd() {
        let snap = ShellSnapshot::capture();
        let summary = snap.context_summary();
        assert!(summary.contains("CWD:"));
        assert!(summary.contains("Shell:"));
    }

    #[test]
    fn diff_detects_cwd_change() {
        let old = ShellSnapshot::capture();
        let mut new = old.clone();
        new.cwd = PathBuf::from("/some/other/dir");
        let diff = old.diff(&new);
        assert!(diff.cwd_changed);
    }
}
