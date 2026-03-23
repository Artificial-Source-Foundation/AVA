//! Shadow git snapshot system for full project-state undo/rollback.
//!
//! Unlike [`GhostSnapshotter`](crate::git::GhostSnapshotter) which stores
//! individual file blobs in the project's own `.git`, this module maintains a
//! **separate shadow git repository** at `~/.ava/snapshots/{project-hash}/`
//! that captures the full working-tree state as git commits.
//!
//! This enables:
//! - Reverting all files to any previous snapshot point (not just individual files)
//! - Viewing diffs between any two snapshots
//! - Listing all snapshots with metadata (message, timestamp, changed files)
//!
//! Design inspired by OpenCode's snapshot system. The shadow repo is a bare-ish
//! repo that uses `GIT_WORK_TREE` to point at the real project directory.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::sync::Arc;

use tokio::process::Command;
use tokio::sync::RwLock;
use tracing::{debug, info};

/// Metadata for a single snapshot.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotEntry {
    /// The git commit hash of this snapshot.
    pub commit_hash: String,
    /// Human-readable label (e.g. "before edit: src/main.rs").
    pub message: String,
    /// ISO 8601 timestamp.
    pub timestamp: String,
    /// Files changed relative to the previous snapshot.
    pub changed_files: Vec<String>,
}

/// Manages a shadow git repository for project-state snapshots.
///
/// Each snapshot is a full commit of the project's tracked files in a separate
/// git directory. This avoids polluting the project's own git history while
/// providing complete rollback capability.
#[derive(Debug, Clone)]
pub struct SnapshotManager {
    /// Path to the shadow git directory (~/.ava/snapshots/{project-hash}/).
    snapshot_dir: PathBuf,
    /// The real project root (used as GIT_WORK_TREE).
    project_root: PathBuf,
    /// Whether the shadow repo has been initialized.
    initialized: bool,
}

/// Thread-safe shared handle to a [`SnapshotManager`].
pub type SharedSnapshotManager = Arc<RwLock<Option<SnapshotManager>>>;

/// Create a new, empty shared snapshot manager handle.
pub fn new_shared_snapshot_manager() -> SharedSnapshotManager {
    Arc::new(RwLock::new(None))
}

impl SnapshotManager {
    /// Create a new `SnapshotManager` for the given project root.
    ///
    /// The shadow repo is stored at `~/.ava/snapshots/{hash}/` where `{hash}`
    /// is a stable hash of the canonicalized project root path.
    pub fn new(project_root: &Path) -> Result<Self, String> {
        let canonical = project_root
            .canonicalize()
            .unwrap_or_else(|_| project_root.to_path_buf());

        let home = dirs::home_dir().ok_or("could not determine home directory")?;
        let project_hash = hash_project_root(&canonical);
        let snapshot_dir = home.join(".ava").join("snapshots").join(&project_hash);

        Ok(Self {
            snapshot_dir,
            project_root: canonical,
            initialized: false,
        })
    }

    /// Initialize the shadow git repo if it doesn't already exist.
    ///
    /// Creates a bare-style repo with `git init` and configures it to use
    /// the project root as its work tree. Idempotent -- safe to call multiple
    /// times.
    pub async fn init(&mut self) -> Result<(), String> {
        if self.initialized {
            return Ok(());
        }

        // Verify the project root is itself a git repo (we only snapshot git projects)
        let check = Command::new("git")
            .arg("-C")
            .arg(&self.project_root)
            .args(["rev-parse", "--is-inside-work-tree"])
            .output()
            .await
            .map_err(|e| format!("failed to check git repo: {e}"))?;

        if !check.status.success() {
            return Err("project root is not inside a git repository".to_string());
        }

        // Create the shadow directory
        tokio::fs::create_dir_all(&self.snapshot_dir)
            .await
            .map_err(|e| format!("failed to create snapshot dir: {e}"))?;

        // Check if already initialized (bare repo has HEAD directly in dir)
        let git_dir_check = self.snapshot_dir.join("HEAD");
        if !git_dir_check.exists() {
            // Initialize a bare git repo so snapshot_dir IS the GIT_DIR.
            // We use GIT_WORK_TREE env var at runtime to point at the project.
            let output = Command::new("git")
                .arg("init")
                .arg("--bare")
                .arg("--initial-branch=snapshots")
                .arg(&self.snapshot_dir)
                .output()
                .await
                .map_err(|e| format!("git init failed: {e}"))?;

            if !output.status.success() {
                let stderr = String::from_utf8_lossy(&output.stderr);
                return Err(format!("git init failed: {stderr}"));
            }

            // Configure the bare repo to allow a work tree via env var.
            // core.bare=false is needed so git accepts GIT_WORK_TREE.
            self.git_config("core.bare", "false").await?;
            self.git_config("core.worktree", &self.project_root.to_string_lossy())
                .await?;

            // Read the project's .gitignore if it exists and copy exclude patterns
            self.sync_gitignore().await?;

            debug!(
                snapshot_dir = %self.snapshot_dir.display(),
                project_root = %self.project_root.display(),
                "initialized shadow snapshot repo"
            );
        }

        self.initialized = true;
        Ok(())
    }

    /// Take a snapshot of the current project state.
    ///
    /// Stages all tracked files (respecting .gitignore) and creates a commit
    /// in the shadow repo. Returns the commit hash.
    ///
    /// `message` is a human-readable label for the snapshot (e.g.,
    /// "before edit: src/main.rs").
    pub async fn take_snapshot(&self, message: &str) -> Result<String, String> {
        if !self.initialized {
            return Err("snapshot manager not initialized".to_string());
        }

        // Stage all files (add + remove deleted)
        let add_output = self
            .run_git(&["add", "-A"])
            .await
            .map_err(|e| format!("git add failed: {e}"))?;

        if !add_output.status.success() {
            let stderr = String::from_utf8_lossy(&add_output.stderr);
            // If there's nothing to add, that's fine
            if !stderr.contains("nothing to commit") {
                debug!(stderr = %stderr, "git add warning (non-fatal)");
            }
        }

        // Check if there are staged changes; if not, return the current HEAD
        let status_output = self
            .run_git(&["status", "--porcelain"])
            .await
            .map_err(|e| format!("git status failed: {e}"))?;

        let status_text = String::from_utf8_lossy(&status_output.stdout);

        // If no changes and we have a HEAD, return the existing HEAD commit
        if status_text.trim().is_empty() {
            if let Ok(hash) = self.current_head().await {
                return Ok(hash);
            }
            // No HEAD yet — fall through to create the initial commit
        }

        // Create the commit with a timestamped message
        let timestamp = chrono::Local::now().format("%Y-%m-%dT%H:%M:%S").to_string();
        let full_message = format!("[{timestamp}] {message}");

        // Use environment variables to avoid needing user.name/email config
        let commit_output = Command::new("git")
            .env("GIT_DIR", &self.snapshot_dir)
            .env("GIT_WORK_TREE", &self.project_root)
            .env("GIT_AUTHOR_NAME", "ava-snapshot")
            .env("GIT_AUTHOR_EMAIL", "snapshot@ava.local")
            .env("GIT_COMMITTER_NAME", "ava-snapshot")
            .env("GIT_COMMITTER_EMAIL", "snapshot@ava.local")
            .args(["commit", "-m", &full_message, "--allow-empty"])
            .output()
            .await
            .map_err(|e| format!("git commit failed: {e}"))?;

        if !commit_output.status.success() {
            let stderr = String::from_utf8_lossy(&commit_output.stderr);
            // "nothing to commit" is not an error
            if stderr.contains("nothing to commit") {
                return self.current_head().await;
            }
            return Err(format!("git commit failed: {stderr}"));
        }

        let hash = self.current_head().await?;
        info!(
            hash = %hash,
            message = %message,
            "snapshot taken"
        );
        Ok(hash)
    }

    /// Get the diff between two snapshots.
    ///
    /// Returns a unified diff string. If `from` is `None`, diffs against the
    /// parent of `to`.
    pub async fn diff(&self, from: Option<&str>, to: &str) -> Result<String, String> {
        if !self.initialized {
            return Err("snapshot manager not initialized".to_string());
        }

        let parent_ref = format!("{to}~1");
        let args = match from {
            Some(from_hash) => vec!["diff", from_hash, to],
            None => vec!["diff", &parent_ref, to],
        };

        let output = self
            .run_git(&args)
            .await
            .map_err(|e| format!("git diff failed: {e}"))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Err(format!("git diff failed: {stderr}"));
        }

        Ok(String::from_utf8_lossy(&output.stdout).to_string())
    }

    /// Restore all project files to the state captured in a specific snapshot.
    ///
    /// This performs a hard checkout of the snapshot commit's tree into the
    /// project working directory. **Destructive** -- overwrites current files.
    pub async fn restore(&self, snapshot_hash: &str) -> Result<Vec<String>, String> {
        if !self.initialized {
            return Err("snapshot manager not initialized".to_string());
        }

        // First, find which files differ between current state and the target
        let current_head = self.current_head().await.ok();
        let changed = if let Some(ref head) = current_head {
            self.changed_files_between(head, snapshot_hash).await?
        } else {
            Vec::new()
        };

        // Checkout the target snapshot's tree into the working directory
        // Use read-tree + checkout-index to avoid moving HEAD permanently
        let read_output = self
            .run_git(&["read-tree", snapshot_hash])
            .await
            .map_err(|e| format!("git read-tree failed: {e}"))?;

        if !read_output.status.success() {
            let stderr = String::from_utf8_lossy(&read_output.stderr);
            return Err(format!("git read-tree failed: {stderr}"));
        }

        let checkout_output = self
            .run_git(&["checkout-index", "-a", "-f"])
            .await
            .map_err(|e| format!("git checkout-index failed: {e}"))?;

        if !checkout_output.status.success() {
            let stderr = String::from_utf8_lossy(&checkout_output.stderr);
            return Err(format!("git checkout-index failed: {stderr}"));
        }

        // Also handle files that were deleted between the snapshot and HEAD:
        // any file that exists now but didn't in the snapshot should be removed.
        if let Some(ref head) = current_head {
            let deleted_files = self.files_added_between(snapshot_hash, head).await?;
            for file in &deleted_files {
                let full_path = self.project_root.join(file);
                if full_path.exists() {
                    let _ = tokio::fs::remove_file(&full_path).await;
                    debug!(file = %file, "removed file not present in snapshot");
                }
            }
        }

        info!(
            snapshot = %snapshot_hash,
            files_changed = changed.len(),
            "restored project to snapshot"
        );

        Ok(changed)
    }

    /// List all snapshots (most recent first).
    pub async fn list_snapshots(&self) -> Result<Vec<SnapshotEntry>, String> {
        if !self.initialized {
            return Err("snapshot manager not initialized".to_string());
        }

        // Check if there are any commits
        let head_check = self.current_head().await;
        if head_check.is_err() {
            return Ok(Vec::new());
        }

        let output = self
            .run_git(&["log", "--format=%H%n%s%n%aI%n---", "--name-only"])
            .await
            .map_err(|e| format!("git log failed: {e}"))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            if stderr.contains("does not have any commits") {
                return Ok(Vec::new());
            }
            return Err(format!("git log failed: {stderr}"));
        }

        let text = String::from_utf8_lossy(&output.stdout);
        let mut snapshots = Vec::new();

        // Parse the git log output. Format per entry:
        //   {hash}
        //   {subject}
        //   {iso-date}
        //   ---
        //   {changed files, newline-separated}
        //   (empty line)
        let mut entries: Vec<&str> = text.split("---\n").collect();
        // Remove trailing empty entry
        if entries.last().map(|e| e.trim().is_empty()).unwrap_or(false) {
            entries.pop();
        }

        for entry in entries {
            let lines: Vec<&str> = entry.lines().collect();
            if lines.len() < 3 {
                continue;
            }
            let commit_hash = lines[0].trim().to_string();
            let message = lines[1].trim().to_string();
            let timestamp = lines[2].trim().to_string();
            let changed_files: Vec<String> = lines[3..]
                .iter()
                .map(|l| l.trim())
                .filter(|l| !l.is_empty())
                .map(|l| l.to_string())
                .collect();

            snapshots.push(SnapshotEntry {
                commit_hash,
                message,
                timestamp,
                changed_files,
            });
        }

        Ok(snapshots)
    }

    /// Get the number of snapshots taken.
    pub async fn snapshot_count(&self) -> usize {
        if !self.initialized {
            return 0;
        }

        let output = self.run_git(&["rev-list", "--count", "HEAD"]).await;
        match output {
            Ok(o) if o.status.success() => {
                let text = String::from_utf8_lossy(&o.stdout);
                text.trim().parse().unwrap_or(0)
            }
            _ => 0,
        }
    }

    /// Get files that changed between two commits.
    pub async fn changed_files_between(&self, from: &str, to: &str) -> Result<Vec<String>, String> {
        let output = self
            .run_git(&["diff", "--name-only", from, to])
            .await
            .map_err(|e| format!("git diff --name-only failed: {e}"))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Err(format!("git diff failed: {stderr}"));
        }

        let text = String::from_utf8_lossy(&output.stdout);
        Ok(text
            .lines()
            .map(|l| l.trim())
            .filter(|l| !l.is_empty())
            .map(String::from)
            .collect())
    }

    /// Get files that were added (exist in `to` but not in `from`).
    async fn files_added_between(&self, from: &str, to: &str) -> Result<Vec<String>, String> {
        let output = self
            .run_git(&["diff", "--name-only", "--diff-filter=A", from, to])
            .await
            .map_err(|e| format!("git diff --diff-filter=A failed: {e}"))?;

        if !output.status.success() {
            return Ok(Vec::new()); // Non-fatal
        }

        let text = String::from_utf8_lossy(&output.stdout);
        Ok(text
            .lines()
            .map(|l| l.trim())
            .filter(|l| !l.is_empty())
            .map(String::from)
            .collect())
    }

    /// Get the current HEAD commit hash.
    async fn current_head(&self) -> Result<String, String> {
        let output = self
            .run_git(&["rev-parse", "HEAD"])
            .await
            .map_err(|e| format!("git rev-parse HEAD failed: {e}"))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Err(format!("no HEAD commit: {stderr}"));
        }

        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    }

    /// Set a git config value in the shadow repo.
    async fn git_config(&self, key: &str, value: &str) -> Result<(), String> {
        let output = self
            .run_git(&["config", key, value])
            .await
            .map_err(|e| format!("git config failed: {e}"))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Err(format!("git config {key} failed: {stderr}"));
        }
        Ok(())
    }

    /// Copy the project's .gitignore patterns into the shadow repo's exclude file.
    async fn sync_gitignore(&self) -> Result<(), String> {
        let gitignore_path = self.project_root.join(".gitignore");
        if !gitignore_path.exists() {
            return Ok(());
        }

        let content = tokio::fs::read_to_string(&gitignore_path)
            .await
            .map_err(|e| format!("failed to read .gitignore: {e}"))?;

        // Write to the shadow repo's info/exclude
        let exclude_dir = self.snapshot_dir.join("info");
        tokio::fs::create_dir_all(&exclude_dir)
            .await
            .map_err(|e| format!("failed to create info dir: {e}"))?;

        let exclude_path = exclude_dir.join("exclude");
        tokio::fs::write(&exclude_path, content)
            .await
            .map_err(|e| format!("failed to write exclude file: {e}"))?;

        Ok(())
    }

    /// Run a git command in the shadow repo context.
    async fn run_git(&self, args: &[&str]) -> Result<std::process::Output, String> {
        Command::new("git")
            .env("GIT_DIR", &self.snapshot_dir)
            .env("GIT_WORK_TREE", &self.project_root)
            .args(args)
            .output()
            .await
            .map_err(|e| format!("failed to execute git: {e}"))
    }

    /// Get the snapshot directory path (for diagnostics).
    pub fn snapshot_dir(&self) -> &Path {
        &self.snapshot_dir
    }

    /// Get the project root path.
    pub fn project_root(&self) -> &Path {
        &self.project_root
    }
}

/// Produce a stable, filesystem-safe hash of a project root path.
fn hash_project_root(path: &Path) -> String {
    let mut hasher = DefaultHasher::new();
    path.to_string_lossy().hash(&mut hasher);
    format!("{:016x}", hasher.finish())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    /// Helper: create a temporary git repo for testing.
    async fn setup_test_repo() -> TempDir {
        let tmp = TempDir::new().unwrap();
        let output = Command::new("git")
            .arg("init")
            .arg("--initial-branch=main")
            .arg(tmp.path())
            .output()
            .await
            .unwrap();
        assert!(output.status.success(), "git init failed in test setup");

        // Configure user for the test repo
        Command::new("git")
            .arg("-C")
            .arg(tmp.path())
            .args(["config", "user.name", "test"])
            .output()
            .await
            .unwrap();
        Command::new("git")
            .arg("-C")
            .arg(tmp.path())
            .args(["config", "user.email", "test@test.local"])
            .output()
            .await
            .unwrap();

        // Create an initial commit so the repo has a HEAD
        tokio::fs::write(tmp.path().join(".gitkeep"), "")
            .await
            .unwrap();
        Command::new("git")
            .arg("-C")
            .arg(tmp.path())
            .args(["add", "."])
            .output()
            .await
            .unwrap();
        Command::new("git")
            .arg("-C")
            .arg(tmp.path())
            .args(["commit", "-m", "init"])
            .output()
            .await
            .unwrap();

        tmp
    }

    #[test]
    fn hash_is_stable() {
        let p = Path::new("/some/project/root");
        let h1 = hash_project_root(p);
        let h2 = hash_project_root(p);
        assert_eq!(h1, h2);
        assert_eq!(h1.len(), 16);
    }

    #[test]
    fn hash_differs_for_different_paths() {
        let h1 = hash_project_root(Path::new("/project/a"));
        let h2 = hash_project_root(Path::new("/project/b"));
        assert_ne!(h1, h2);
    }

    #[tokio::test]
    async fn init_creates_shadow_repo() {
        let repo = setup_test_repo().await;
        let mut manager = SnapshotManager::new(repo.path()).unwrap();

        // Override snapshot_dir to use temp dir
        let tmp_snap = TempDir::new().unwrap();
        manager.snapshot_dir = tmp_snap.path().join("shadow");

        manager.init().await.unwrap();
        assert!(manager.initialized);
        assert!(manager.snapshot_dir.join("HEAD").exists());
    }

    #[tokio::test]
    async fn take_and_list_snapshots() {
        let repo = setup_test_repo().await;
        let mut manager = SnapshotManager::new(repo.path()).unwrap();
        let tmp_snap = TempDir::new().unwrap();
        manager.snapshot_dir = tmp_snap.path().join("shadow");
        manager.init().await.unwrap();

        // Create a file and take a snapshot
        tokio::fs::write(repo.path().join("hello.txt"), "hello v1")
            .await
            .unwrap();
        let hash1 = manager.take_snapshot("initial state").await.unwrap();
        assert!(!hash1.is_empty());

        // Modify and take another snapshot
        tokio::fs::write(repo.path().join("hello.txt"), "hello v2")
            .await
            .unwrap();
        let hash2 = manager.take_snapshot("modified hello").await.unwrap();
        assert_ne!(hash1, hash2);

        // List should show both
        let snapshots = manager.list_snapshots().await.unwrap();
        assert!(snapshots.len() >= 2);
        assert_eq!(snapshots[0].commit_hash, hash2); // most recent first
    }

    #[tokio::test]
    async fn restore_reverts_files() {
        let repo = setup_test_repo().await;
        let mut manager = SnapshotManager::new(repo.path()).unwrap();
        let tmp_snap = TempDir::new().unwrap();
        manager.snapshot_dir = tmp_snap.path().join("shadow");
        manager.init().await.unwrap();

        // Create file and snapshot
        let file = repo.path().join("data.txt");
        tokio::fs::write(&file, "original content").await.unwrap();
        let hash1 = manager.take_snapshot("v1").await.unwrap();

        // Modify file
        tokio::fs::write(&file, "modified content").await.unwrap();
        let _hash2 = manager.take_snapshot("v2").await.unwrap();

        // Verify file is modified
        let content = tokio::fs::read_to_string(&file).await.unwrap();
        assert_eq!(content, "modified content");

        // Restore to first snapshot
        let changed = manager.restore(&hash1).await.unwrap();
        let content = tokio::fs::read_to_string(&file).await.unwrap();
        assert_eq!(content, "original content");
    }

    #[tokio::test]
    async fn diff_between_snapshots() {
        let repo = setup_test_repo().await;
        let mut manager = SnapshotManager::new(repo.path()).unwrap();
        let tmp_snap = TempDir::new().unwrap();
        manager.snapshot_dir = tmp_snap.path().join("shadow");
        manager.init().await.unwrap();

        tokio::fs::write(repo.path().join("file.txt"), "line1\n")
            .await
            .unwrap();
        let hash1 = manager.take_snapshot("first").await.unwrap();

        tokio::fs::write(repo.path().join("file.txt"), "line1\nline2\n")
            .await
            .unwrap();
        let hash2 = manager.take_snapshot("second").await.unwrap();

        let diff = manager.diff(Some(&hash1), &hash2).await.unwrap();
        assert!(diff.contains("+line2"));
    }

    #[tokio::test]
    async fn snapshot_count_tracks_commits() {
        let repo = setup_test_repo().await;
        let mut manager = SnapshotManager::new(repo.path()).unwrap();
        let tmp_snap = TempDir::new().unwrap();
        manager.snapshot_dir = tmp_snap.path().join("shadow");
        manager.init().await.unwrap();

        tokio::fs::write(repo.path().join("a.txt"), "a")
            .await
            .unwrap();
        manager.take_snapshot("one").await.unwrap();

        tokio::fs::write(repo.path().join("b.txt"), "b")
            .await
            .unwrap();
        manager.take_snapshot("two").await.unwrap();

        let count = manager.snapshot_count().await;
        assert!(count >= 2);
    }

    #[tokio::test]
    async fn init_is_idempotent() {
        let repo = setup_test_repo().await;
        let mut manager = SnapshotManager::new(repo.path()).unwrap();
        let tmp_snap = TempDir::new().unwrap();
        manager.snapshot_dir = tmp_snap.path().join("shadow");

        manager.init().await.unwrap();
        manager.init().await.unwrap(); // should not fail
        assert!(manager.initialized);
    }

    #[tokio::test]
    async fn non_git_project_returns_error() {
        let tmp = TempDir::new().unwrap();
        let mut manager = SnapshotManager::new(tmp.path()).unwrap();
        let tmp_snap = TempDir::new().unwrap();
        manager.snapshot_dir = tmp_snap.path().join("shadow");

        let result = manager.init().await;
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("not inside a git repository"));
    }
}
