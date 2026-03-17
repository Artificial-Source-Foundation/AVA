//! File watcher for skill hot-reload — polls TOML files for changes.
//!
//! Uses a simple polling approach (no `notify` crate) to detect when
//! custom tool TOML files have been modified since the last check.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

/// Watches directories for changed TOML files using polling.
#[derive(Debug)]
pub struct SkillWatcher {
    /// Directories to watch for TOML changes.
    watched_dirs: Vec<PathBuf>,
    /// Last known modification time per file.
    last_seen: HashMap<PathBuf, SystemTime>,
}

impl SkillWatcher {
    /// Create a new watcher for the given directories.
    pub fn new(dirs: Vec<PathBuf>) -> Self {
        Self {
            watched_dirs: dirs,
            last_seen: HashMap::new(),
        }
    }

    /// Check for changed or new TOML files since the last call.
    ///
    /// On the first call, all existing TOML files are returned as "changed".
    /// Subsequent calls return only files whose modification time has advanced.
    pub fn check_for_changes(&mut self) -> Vec<PathBuf> {
        let mut changed = Vec::new();

        for dir in &self.watched_dirs {
            if !dir.is_dir() {
                continue;
            }
            Self::scan_dir(dir, &mut self.last_seen, &mut changed);
        }

        changed
    }

    /// Recursively scan a directory for TOML files.
    fn scan_dir(
        dir: &Path,
        last_seen: &mut HashMap<PathBuf, SystemTime>,
        changed: &mut Vec<PathBuf>,
    ) {
        let Ok(entries) = std::fs::read_dir(dir) else {
            return;
        };

        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                Self::scan_dir(&path, last_seen, changed);
                continue;
            }

            let is_toml = path
                .extension()
                .and_then(|e| e.to_str())
                .map(|e| e == "toml")
                .unwrap_or(false);

            if !is_toml {
                continue;
            }

            let Ok(modified) = std::fs::metadata(&path).and_then(|m| m.modified()) else {
                continue;
            };

            let is_changed = last_seen
                .get(&path)
                .map(|prev| *prev < modified)
                .unwrap_or(true); // New file counts as changed

            if is_changed {
                last_seen.insert(path.clone(), modified);
                changed.push(path);
            }
        }
    }

    /// Get the list of watched directories.
    pub fn watched_dirs(&self) -> &[PathBuf] {
        &self.watched_dirs
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::TempDir;

    #[test]
    fn first_scan_reports_all_toml_files() {
        let dir = TempDir::new().unwrap();
        std::fs::write(dir.path().join("tool1.toml"), "name = \"t1\"").unwrap();
        std::fs::write(dir.path().join("tool2.toml"), "name = \"t2\"").unwrap();
        std::fs::write(dir.path().join("readme.md"), "# readme").unwrap();

        let mut watcher = SkillWatcher::new(vec![dir.path().to_path_buf()]);
        let changed = watcher.check_for_changes();
        assert_eq!(changed.len(), 2);
    }

    #[test]
    fn second_scan_reports_no_changes() {
        let dir = TempDir::new().unwrap();
        std::fs::write(dir.path().join("tool.toml"), "name = \"t\"").unwrap();

        let mut watcher = SkillWatcher::new(vec![dir.path().to_path_buf()]);
        let _ = watcher.check_for_changes();
        let changed = watcher.check_for_changes();
        assert!(changed.is_empty());
    }

    #[test]
    fn modified_file_is_detected() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("tool.toml");
        std::fs::write(&path, "name = \"v1\"").unwrap();

        let mut watcher = SkillWatcher::new(vec![dir.path().to_path_buf()]);
        let _ = watcher.check_for_changes();

        // Sleep briefly to ensure filesystem timestamp advances
        std::thread::sleep(std::time::Duration::from_millis(50));

        let mut f = std::fs::File::create(&path).unwrap();
        writeln!(f, "name = \"v2\"").unwrap();

        let changed = watcher.check_for_changes();
        assert_eq!(changed.len(), 1);
    }
}
