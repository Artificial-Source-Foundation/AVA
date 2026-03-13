use crate::widgets::autocomplete::AutocompleteItem;
use std::path::{Path, PathBuf};

/// Directories to skip when scanning project files.
const SKIP_DIRS: &[&str] = &[
    ".git",
    "node_modules",
    "target",
    ".next",
    "dist",
    "build",
    "__pycache__",
    ".cache",
    ".venv",
    "venv",
];

/// Maximum depth for recursive file scanning.
const MAX_SCAN_DEPTH: usize = 4;
/// Maximum number of results to return.
const MAX_SCAN_RESULTS: usize = 50;

/// Scan project files from the current directory, returning autocomplete items.
/// If `folders_only` is true, only directories are returned.
pub(crate) fn scan_project_files(query: &str, folders_only: bool) -> Vec<AutocompleteItem> {
    let cwd = match std::env::current_dir() {
        Ok(p) => p,
        Err(_) => return Vec::new(),
    };

    let mut results = Vec::new();
    scan_dir_recursive(&cwd, &cwd, query, folders_only, 0, &mut results);
    results.truncate(MAX_SCAN_RESULTS);
    results
}

fn scan_dir_recursive(
    base: &Path,
    dir: &Path,
    _query: &str,
    folders_only: bool,
    depth: usize,
    results: &mut Vec<AutocompleteItem>,
) {
    if depth > MAX_SCAN_DEPTH || results.len() >= MAX_SCAN_RESULTS {
        return;
    }

    let entries = match std::fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return,
    };

    let mut entries: Vec<_> = entries.filter_map(|e| e.ok()).collect();
    entries.sort_by_key(|e| e.file_name());

    for entry in entries {
        let name = entry.file_name();
        let name_str = name.to_string_lossy();

        if name_str.starts_with('.') && name_str != ".ava" {
            continue;
        }
        if SKIP_DIRS.contains(&name_str.as_ref()) {
            continue;
        }

        let path = entry.path();
        let rel = path
            .strip_prefix(base)
            .unwrap_or(&path)
            .to_string_lossy()
            .to_string();

        let is_dir = path.is_dir();

        if is_dir {
            let display = format!("{rel}/");
            let detail = "folder".to_string();
            results.push(AutocompleteItem::new(display, detail));
            scan_dir_recursive(base, &path, _query, folders_only, depth + 1, results);
        } else if !folders_only {
            let detail = "file".to_string();
            results.push(AutocompleteItem::new(rel, detail));
        }
    }
}

/// Cached results from a project file scan, keyed by (cwd, folders_only).
/// Avoids re-scanning the filesystem on every keystroke within an `@`-mention session.
#[derive(Debug, Default)]
pub(crate) struct MentionFileCache {
    /// The working directory at the time of the scan.
    pub(crate) cwd: Option<PathBuf>,
    /// Whether the scan was folders-only.
    pub(crate) folders_only: bool,
    /// The full (unfiltered) scan results.
    pub(crate) items: Vec<AutocompleteItem>,
}

impl MentionFileCache {
    /// Return cached items if the cache key matches, otherwise re-scan and update.
    pub(crate) fn get_or_scan(&mut self, folders_only: bool) -> &[AutocompleteItem] {
        let current_cwd = std::env::current_dir().ok();

        let hit =
            self.cwd.is_some() && self.cwd == current_cwd && self.folders_only == folders_only;

        if !hit {
            self.items = scan_project_files("", folders_only);
            self.cwd = current_cwd;
            self.folders_only = folders_only;
        }

        &self.items
    }

    /// Discard cached data so the next access triggers a fresh scan.
    pub(crate) fn invalidate(&mut self) {
        self.cwd = None;
        self.items.clear();
    }
}
