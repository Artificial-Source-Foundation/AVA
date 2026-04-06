use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use tokio::io::AsyncWriteExt;
use tokio::process::Command;

use crate::git::GitToolError;

const GIT_ENV_VARS_TO_CLEAR: &[&str] = &[
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_COMMON_DIR",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_PREFIX",
    "GIT_CEILING_DIRECTORIES",
];

pub const GHOST_SNAPSHOT_PREFIX: &str = "refs/ava/snapshots";

static SNAPSHOT_SEQUENCE: AtomicU64 = AtomicU64::new(1);

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GhostSnapshot {
    pub repo_root: PathBuf,
    pub ref_name: String,
    pub object_id: String,
}

/// Metadata for a listed snapshot.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotInfo {
    /// The full ref name (e.g. `refs/ava/snapshots/17100...-1-src_lib.rs`).
    pub ref_name: String,
    /// The file path component extracted from the ref name.
    pub file_hint: String,
    /// The object ID of the stored blob.
    pub object_id: String,
}

#[derive(Debug, Default, Clone, Copy)]
pub struct GhostSnapshotter;

impl GhostSnapshotter {
    pub const fn new() -> Self {
        Self
    }

    /// Stores the pre-edit file contents as a hidden blob ref.
    ///
    /// This is intentionally lightweight: it captures file contents, not a full
    /// tree/commit snapshot of the repository state.
    /// TODO(sprint-61): add snapshot ref cleanup/retention once recovery UX lands.
    pub async fn snapshot_file_before_write(
        &self,
        path: &Path,
        content: &str,
    ) -> Result<Option<GhostSnapshot>, GitToolError> {
        let absolute_path = resolve_input_path(path)?;
        let lookup_dir = absolute_path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
            .unwrap_or(&absolute_path);
        let Some(repo_root) = repo_root(lookup_dir).await? else {
            return Ok(None);
        };

        let object_id = write_blob(&repo_root, content).await?;
        let relative_path = absolute_path
            .strip_prefix(&repo_root)
            .unwrap_or(&absolute_path);
        let ref_name = format!(
            "{GHOST_SNAPSHOT_PREFIX}/{}-{}-{}",
            timestamp_suffix(),
            sequence_suffix(),
            sanitize_path(relative_path)
        );

        run_git(
            &repo_root,
            &[
                "update-ref",
                "-m",
                "ava ghost snapshot",
                ref_name.as_str(),
                object_id.as_str(),
            ],
        )
        .await?;

        Ok(Some(GhostSnapshot {
            repo_root,
            ref_name,
            object_id,
        }))
    }

    /// List all ghost snapshots in the given repository directory.
    ///
    /// Returns snapshot metadata sorted by ref name (which embeds timestamp).
    pub async fn list_snapshots(
        &self,
        repo_path: &Path,
    ) -> Result<Vec<SnapshotInfo>, GitToolError> {
        let lookup = resolve_input_path(repo_path)?;
        let Some(root) = repo_root(&lookup).await? else {
            return Ok(Vec::new());
        };

        let output = match run_git(
            &root,
            &[
                "for-each-ref",
                "--format=%(refname) %(objectname)",
                GHOST_SNAPSHOT_PREFIX,
            ],
        )
        .await
        {
            Ok(stdout) => stdout,
            Err(GitToolError::CommandFailed { stdout, .. }) if stdout.is_empty() => {
                return Ok(Vec::new());
            }
            Err(e) => return Err(e),
        };

        let mut snapshots = Vec::new();
        for line in output.lines() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            let mut parts = line.splitn(2, ' ');
            let ref_name = match parts.next() {
                Some(r) => r.to_string(),
                None => continue,
            };
            let object_id = parts.next().unwrap_or("").to_string();

            // Extract file hint: everything after the third `-` in the suffix
            // Ref format: refs/ava/snapshots/{timestamp}-{seq}-{sanitized_path}
            let suffix = ref_name
                .strip_prefix(&format!("{GHOST_SNAPSHOT_PREFIX}/"))
                .unwrap_or(&ref_name);
            let file_hint = suffix.splitn(3, '-').nth(2).unwrap_or(suffix).to_string();

            snapshots.push(SnapshotInfo {
                ref_name,
                file_hint,
                object_id,
            });
        }

        Ok(snapshots)
    }

    /// Revert a file to the content stored in a ghost snapshot.
    ///
    /// `snapshot` must have been produced by `snapshot_file_before_write`.
    /// The original file path is reconstructed from the repo root and the
    /// sanitized path embedded in the ref name.
    pub async fn revert_snapshot(&self, snapshot: &GhostSnapshot) -> Result<(), GitToolError> {
        // Read the blob content back from git.
        let content = run_git(
            &snapshot.repo_root,
            &["cat-file", "-p", &snapshot.object_id],
        )
        .await?;

        // Reconstruct the file path from the ref name.
        let suffix = snapshot
            .ref_name
            .strip_prefix(&format!("{GHOST_SNAPSHOT_PREFIX}/"))
            .unwrap_or(&snapshot.ref_name);
        // Format: {timestamp}-{seq}-{sanitized_path}
        let sanitized = suffix.splitn(3, '-').nth(2).unwrap_or(suffix);
        // Reverse the sanitization: underscores that were path separators become `/`.
        let relative = sanitized.replace('_', "/");
        let file_path = snapshot.repo_root.join(&relative);

        // Ensure parent directories exist.
        if let Some(parent) = file_path.parent() {
            std::fs::create_dir_all(parent).map_err(|source| GitToolError::ExecutionFailed {
                program: "fs::create_dir_all".to_string(),
                source,
            })?;
        }

        std::fs::write(&file_path, content.as_bytes()).map_err(|source| {
            GitToolError::ExecutionFailed {
                program: "fs::write".to_string(),
                source,
            }
        })?;

        Ok(())
    }

    /// Revert a snapshot identified by its ref name, looking up the repo from
    /// the given working directory.
    pub async fn revert_by_ref(
        &self,
        repo_path: &Path,
        ref_name: &str,
    ) -> Result<(), GitToolError> {
        let lookup = resolve_input_path(repo_path)?;
        let root = repo_root(&lookup)
            .await?
            .ok_or_else(|| GitToolError::ExecutionFailed {
                program: "git".to_string(),
                source: std::io::Error::other("not a git repository"),
            })?;

        // Resolve ref to object ID.
        let object_id = run_git(&root, &["rev-parse", ref_name]).await?;

        let snapshot = GhostSnapshot {
            repo_root: root,
            ref_name: ref_name.to_string(),
            object_id: object_id.trim().to_string(),
        };

        self.revert_snapshot(&snapshot).await
    }

    /// Delete a snapshot ref after a successful revert (cleanup).
    pub async fn delete_snapshot(
        &self,
        repo_path: &Path,
        ref_name: &str,
    ) -> Result<(), GitToolError> {
        let lookup = resolve_input_path(repo_path)?;
        let root = repo_root(&lookup)
            .await?
            .ok_or_else(|| GitToolError::ExecutionFailed {
                program: "git".to_string(),
                source: std::io::Error::other("not a git repository"),
            })?;

        run_git(&root, &["update-ref", "-d", ref_name]).await?;
        Ok(())
    }
}

pub(crate) fn resolve_input_path(path: &Path) -> Result<PathBuf, GitToolError> {
    if path.is_absolute() {
        return Ok(path.to_path_buf());
    }

    let cwd = std::env::current_dir().map_err(|source| GitToolError::ExecutionFailed {
        program: "git".to_string(),
        source,
    })?;

    Ok(match path.as_os_str().is_empty() {
        true => cwd,
        false => cwd.join(path),
    })
}

async fn repo_root(path: &Path) -> Result<Option<PathBuf>, GitToolError> {
    match run_git(path, &["rev-parse", "--show-toplevel"]).await {
        Ok(stdout) => Ok(Some(PathBuf::from(stdout.trim()))),
        Err(GitToolError::CommandFailed { stderr, .. })
            if stderr.contains("not a git repository") =>
        {
            Ok(None)
        }
        Err(err) => Err(err),
    }
}

async fn write_blob(repo_root: &Path, content: &str) -> Result<String, GitToolError> {
    let mut command = Command::new("git");
    for key in GIT_ENV_VARS_TO_CLEAR {
        command.env_remove(key);
    }
    let mut child = command
        .arg("-C")
        .arg(repo_root)
        .arg("hash-object")
        .arg("-w")
        .arg("--stdin")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|source| GitToolError::ExecutionFailed {
            program: "git".to_string(),
            source,
        })?;

    let mut stdin = child
        .stdin
        .take()
        .ok_or_else(|| GitToolError::ExecutionFailed {
            program: "git".to_string(),
            source: std::io::Error::other("git hash-object stdin unavailable"),
        })?;
    stdin
        .write_all(content.as_bytes())
        .await
        .map_err(|source| GitToolError::ExecutionFailed {
            program: "git".to_string(),
            source,
        })?;
    drop(stdin);

    let output =
        child
            .wait_with_output()
            .await
            .map_err(|source| GitToolError::ExecutionFailed {
                program: "git".to_string(),
                source,
            })?;

    let exit_code = output.status.code().unwrap_or(-1);
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
    if !output.status.success() {
        return Err(GitToolError::CommandFailed {
            program: "git".to_string(),
            args: vec![
                "hash-object".to_string(),
                "-w".to_string(),
                "--stdin".to_string(),
            ],
            exit_code,
            stdout,
            stderr,
        });
    }

    Ok(stdout.trim().to_string())
}

async fn run_git(repo_root: &Path, args: &[&str]) -> Result<String, GitToolError> {
    let output = run_git_command(repo_root, args)
        .output()
        .await
        .map_err(|source| GitToolError::ExecutionFailed {
            program: "git".to_string(),
            source,
        })?;

    let exit_code = output.status.code().unwrap_or(-1);
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
    if !output.status.success() {
        return Err(GitToolError::CommandFailed {
            program: "git".to_string(),
            args: args.iter().map(|arg| (*arg).to_string()).collect(),
            exit_code,
            stdout,
            stderr,
        });
    }

    Ok(stdout)
}

pub(crate) fn run_git_command<'a>(repo_root: &'a Path, args: &[&'a str]) -> Command {
    let mut command = Command::new("git");
    for key in GIT_ENV_VARS_TO_CLEAR {
        command.env_remove(key);
    }
    command.arg("-C").arg(repo_root).args(args);
    command
}

fn sanitize_path(path: &Path) -> String {
    let raw = path.to_string_lossy();
    let mut out = String::with_capacity(raw.len());
    for ch in raw.chars() {
        if ch.is_ascii_alphanumeric() || matches!(ch, '.' | '_' | '-') {
            out.push(ch);
        } else {
            out.push('_');
        }
    }
    out.trim_matches('_').to_string()
}

fn timestamp_suffix() -> u128 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos()
}

fn sequence_suffix() -> u64 {
    SNAPSHOT_SEQUENCE.fetch_add(1, Ordering::Relaxed)
}
