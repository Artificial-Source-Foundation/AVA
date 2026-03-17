//! Startup context — auto-inject recent work history and workspace directory tree
//! into the first agent turn as background context (BG2-16).
//!
//! Inspired by Codex CLI's realtime startup context approach.

use std::path::{Path, PathBuf};

use chrono::{DateTime, Utc};

/// Directories to skip when building the workspace tree.
const NOISY_DIRS: &[&str] = &[
    ".git",
    "node_modules",
    "target",
    "__pycache__",
    ".next",
    "dist",
    "build",
    ".ava",
    "venv",
    ".venv",
    ".tox",
    ".mypy_cache",
    ".pytest_cache",
    ".cargo",
];

/// Maximum number of entries in the workspace tree.
const MAX_TREE_ENTRIES: usize = 100;

/// Maximum character length for the startup message.
const MAX_MESSAGE_CHARS: usize = 2000;

/// Summary of a recent session in this workspace.
#[derive(Debug, Clone)]
pub struct SessionSummary {
    pub title: String,
    pub message_count: usize,
    pub updated_at: DateTime<Utc>,
}

/// Collected startup context for injection into the first agent turn.
#[derive(Debug, Clone)]
pub struct StartupContext {
    pub recent_sessions: Vec<SessionSummary>,
    pub workspace_tree: String,
    pub shell_info: String,
}

impl StartupContext {
    /// Build a startup context for the given workspace root.
    pub fn build(root: &Path, recent_sessions: Vec<SessionSummary>) -> Self {
        let workspace_tree = build_workspace_tree(root, 2);
        let shell_info = build_shell_info();

        Self {
            recent_sessions,
            workspace_tree,
            shell_info,
        }
    }
}

/// Build an indented directory tree string for the workspace.
///
/// Walks up to `max_depth` levels deep, skipping noisy directories.
/// Each directory shows a file count. Limited to [`MAX_TREE_ENTRIES`] entries.
pub fn build_workspace_tree(root: &Path, max_depth: usize) -> String {
    let mut lines = Vec::new();
    let root_name = root
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| root.to_string_lossy().to_string());
    lines.push(format!("{}/", root_name));

    walk_tree(root, root, 1, max_depth, &mut lines);

    lines.truncate(MAX_TREE_ENTRIES);
    if lines.len() == MAX_TREE_ENTRIES {
        lines.push("  ... (truncated)".to_string());
    }

    lines.join("\n")
}

fn walk_tree(base: &Path, dir: &Path, depth: usize, max_depth: usize, lines: &mut Vec<String>) {
    if depth > max_depth || lines.len() >= MAX_TREE_ENTRIES {
        return;
    }

    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };

    let mut dirs: Vec<PathBuf> = Vec::new();
    let mut file_count: usize = 0;

    let mut items: Vec<_> = entries.filter_map(|e| e.ok()).collect();
    items.sort_by_key(|e| e.file_name());

    for entry in &items {
        let name = entry.file_name().to_string_lossy().to_string();
        let Ok(ft) = entry.file_type() else {
            continue;
        };

        if ft.is_dir() {
            if NOISY_DIRS.contains(&name.as_str()) {
                continue;
            }
            dirs.push(entry.path());
        } else if ft.is_file() {
            file_count += 1;
        }
    }

    let indent = "  ".repeat(depth);

    // List subdirectories first
    for sub_dir in &dirs {
        if lines.len() >= MAX_TREE_ENTRIES {
            break;
        }
        let dir_name = sub_dir
            .file_name()
            .map(|n| n.to_string_lossy().to_string())
            .unwrap_or_default();

        // Count files in this subdirectory (shallow)
        let sub_file_count = count_files_shallow(sub_dir);
        lines.push(format!(
            "{}{}/  ({} files)",
            indent, dir_name, sub_file_count
        ));

        walk_tree(base, sub_dir, depth + 1, max_depth, lines);
    }

    // Show file count for this level if there are files
    if file_count > 0 && depth > 0 {
        // File count is shown inline with the dir entry above, but for root-level files:
        if dir == base {
            lines.push(format!("{}({} files)", indent, file_count));
        }
    }
}

fn count_files_shallow(dir: &Path) -> usize {
    std::fs::read_dir(dir)
        .map(|entries| {
            entries
                .filter_map(|e| e.ok())
                .filter(|e| e.file_type().map(|ft| ft.is_file()).unwrap_or(false))
                .count()
        })
        .unwrap_or(0)
}

fn build_shell_info() -> String {
    let shell = std::env::var("SHELL").unwrap_or_else(|_| "unknown".to_string());
    let cwd = std::env::current_dir()
        .map(|p| p.to_string_lossy().to_string())
        .unwrap_or_else(|_| "unknown".to_string());
    let os = std::env::consts::OS;
    let arch = std::env::consts::ARCH;

    format!("shell={}, os={}, arch={}, cwd={}", shell, os, arch, cwd)
}

/// Build a compact startup message from the collected context.
///
/// The message is formatted for injection into the system prompt.
/// It is kept under [`MAX_MESSAGE_CHARS`] total.
pub fn build_startup_message(context: &StartupContext) -> String {
    let mut msg = String::with_capacity(MAX_MESSAGE_CHARS);

    msg.push_str("## Workspace Context\n\n");

    // Environment info
    msg.push_str(&format!("**Environment**: {}\n\n", context.shell_info));

    // Recent sessions
    if !context.recent_sessions.is_empty() {
        msg.push_str("**Recent sessions in this workspace**:\n");
        for session in context.recent_sessions.iter().take(5) {
            let age = format_age(&session.updated_at);
            msg.push_str(&format!(
                "- {} ({} messages, {})\n",
                session.title, session.message_count, age
            ));
        }
        msg.push('\n');
    }

    // Workspace tree
    if !context.workspace_tree.is_empty() {
        msg.push_str("**Workspace structure**:\n```\n");

        // Calculate how much room we have for the tree
        let remaining = MAX_MESSAGE_CHARS.saturating_sub(msg.len() + 10); // +10 for closing ```\n
        let tree = if context.workspace_tree.len() > remaining {
            // Truncate at a line boundary
            let truncated = &context.workspace_tree[..remaining];
            if let Some(last_nl) = truncated.rfind('\n') {
                format!("{}\n  ... (truncated)", &truncated[..last_nl])
            } else {
                truncated.to_string()
            }
        } else {
            context.workspace_tree.clone()
        };

        msg.push_str(&tree);
        msg.push_str("\n```\n");
    }

    // Final truncation safety net
    if msg.len() > MAX_MESSAGE_CHARS {
        msg.truncate(MAX_MESSAGE_CHARS - 4);
        msg.push_str("...\n");
    }

    msg
}

fn format_age(dt: &DateTime<Utc>) -> String {
    let now = Utc::now();
    let duration = now.signed_duration_since(*dt);

    if duration.num_minutes() < 1 {
        "just now".to_string()
    } else if duration.num_minutes() < 60 {
        format!("{}m ago", duration.num_minutes())
    } else if duration.num_hours() < 24 {
        format!("{}h ago", duration.num_hours())
    } else {
        format!("{}d ago", duration.num_days())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn create_test_workspace() -> TempDir {
        let tmp = TempDir::new().unwrap();
        let root = tmp.path();

        // Create some directories and files
        std::fs::create_dir_all(root.join("src")).unwrap();
        std::fs::create_dir_all(root.join("tests")).unwrap();
        std::fs::create_dir_all(root.join("docs")).unwrap();
        std::fs::write(root.join("Cargo.toml"), "").unwrap();
        std::fs::write(root.join("README.md"), "").unwrap();
        std::fs::write(root.join("src/main.rs"), "fn main() {}").unwrap();
        std::fs::write(root.join("src/lib.rs"), "").unwrap();
        std::fs::write(root.join("tests/test_basic.rs"), "").unwrap();

        // Create noisy directories that should be skipped
        std::fs::create_dir_all(root.join("node_modules/package")).unwrap();
        std::fs::write(root.join("node_modules/package/index.js"), "").unwrap();
        std::fs::create_dir_all(root.join("target/debug")).unwrap();
        std::fs::write(root.join("target/debug/binary"), "").unwrap();
        std::fs::create_dir_all(root.join(".git/objects")).unwrap();
        std::fs::write(root.join(".git/HEAD"), "ref: refs/heads/main").unwrap();
        std::fs::create_dir_all(root.join("__pycache__")).unwrap();
        std::fs::write(root.join("__pycache__/cache.pyc"), "").unwrap();

        tmp
    }

    #[test]
    fn tree_skips_noisy_dirs() {
        let tmp = create_test_workspace();
        let tree = build_workspace_tree(tmp.path(), 2);

        assert!(tree.contains("src/"), "should contain src/");
        assert!(tree.contains("tests/"), "should contain tests/");
        assert!(tree.contains("docs/"), "should contain docs/");

        assert!(!tree.contains("node_modules"), "should skip node_modules");
        assert!(!tree.contains("target"), "should skip target");
        assert!(!tree.contains(".git"), "should skip .git");
        assert!(!tree.contains("__pycache__"), "should skip __pycache__");
    }

    #[test]
    fn tree_respects_max_depth() {
        let tmp = TempDir::new().unwrap();
        let root = tmp.path();

        std::fs::create_dir_all(root.join("a/b/c/d")).unwrap();
        std::fs::write(root.join("a/b/c/d/deep.txt"), "").unwrap();

        let tree = build_workspace_tree(root, 2);

        // depth 0 = root, depth 1 = a/, depth 2 = b/
        // c/ is at depth 3 so should NOT appear
        assert!(tree.contains("a/"), "should contain a/");
        assert!(tree.contains("b/"), "should contain b/");
        assert!(!tree.contains("c/"), "should not contain c/ (too deep)");
    }

    #[test]
    fn tree_limits_entries() {
        let tmp = TempDir::new().unwrap();
        let root = tmp.path();

        // Create more than MAX_TREE_ENTRIES directories
        for i in 0..120 {
            std::fs::create_dir_all(root.join(format!("dir_{:04}", i))).unwrap();
        }

        let tree = build_workspace_tree(root, 2);
        let line_count = tree.lines().count();

        // Should be capped at MAX_TREE_ENTRIES + 1 (truncation notice)
        assert!(
            line_count <= MAX_TREE_ENTRIES + 2,
            "tree should be limited, got {} lines",
            line_count
        );
        assert!(tree.contains("truncated"), "should show truncation notice");
    }

    #[test]
    fn session_summary_formatting() {
        let sessions = vec![
            SessionSummary {
                title: "Fix login bug".to_string(),
                message_count: 12,
                updated_at: Utc::now() - chrono::Duration::minutes(30),
            },
            SessionSummary {
                title: "Add user dashboard".to_string(),
                message_count: 45,
                updated_at: Utc::now() - chrono::Duration::hours(3),
            },
        ];

        let ctx = StartupContext {
            recent_sessions: sessions,
            workspace_tree: "project/\n  src/  (3 files)".to_string(),
            shell_info: "shell=/bin/zsh, os=linux, arch=x86_64, cwd=/tmp".to_string(),
        };

        let msg = build_startup_message(&ctx);

        assert!(
            msg.contains("Fix login bug"),
            "should contain session title"
        );
        assert!(msg.contains("12 messages"), "should contain message count");
        assert!(msg.contains("30m ago"), "should contain age");
        assert!(
            msg.contains("Add user dashboard"),
            "should contain second session"
        );
        assert!(msg.contains("3h ago"), "should contain hours age");
    }

    #[test]
    fn message_stays_under_limit() {
        // Create a large workspace tree
        let mut big_tree = String::new();
        for i in 0..200 {
            big_tree.push_str(&format!("  dir_{}/  (10 files)\n", i));
        }

        let sessions: Vec<SessionSummary> = (0..10)
            .map(|i| SessionSummary {
                title: format!("Session with a reasonably long title number {}", i),
                message_count: 100 + i,
                updated_at: Utc::now() - chrono::Duration::hours(i as i64),
            })
            .collect();

        let ctx = StartupContext {
            recent_sessions: sessions,
            workspace_tree: big_tree,
            shell_info: "shell=/bin/zsh, os=linux, arch=x86_64, cwd=/home/user/project".to_string(),
        };

        let msg = build_startup_message(&ctx);

        assert!(
            msg.len() <= MAX_MESSAGE_CHARS,
            "message should be under {} chars, got {}",
            MAX_MESSAGE_CHARS,
            msg.len()
        );
    }

    #[test]
    fn empty_context_produces_valid_message() {
        let ctx = StartupContext {
            recent_sessions: vec![],
            workspace_tree: String::new(),
            shell_info: "shell=unknown, os=linux, arch=x86_64, cwd=/tmp".to_string(),
        };

        let msg = build_startup_message(&ctx);

        assert!(msg.contains("Workspace Context"), "should have header");
        assert!(msg.contains("Environment"), "should have env info");
        assert!(
            !msg.contains("Recent sessions"),
            "should skip empty sessions section"
        );
    }

    #[test]
    fn build_context_integration() {
        let tmp = create_test_workspace();
        let ctx = StartupContext::build(tmp.path(), vec![]);

        assert!(!ctx.workspace_tree.is_empty(), "tree should not be empty");
        assert!(!ctx.shell_info.is_empty(), "shell info should not be empty");

        let msg = build_startup_message(&ctx);
        assert!(msg.len() <= MAX_MESSAGE_CHARS);
        assert!(msg.contains("src/"));
    }

    #[test]
    fn tree_shows_file_counts() {
        let tmp = create_test_workspace();
        let tree = build_workspace_tree(tmp.path(), 2);

        // src/ has 2 files (main.rs, lib.rs)
        assert!(
            tree.contains("src/  (2 files)"),
            "should show file count for src/: {}",
            tree
        );
    }
}
