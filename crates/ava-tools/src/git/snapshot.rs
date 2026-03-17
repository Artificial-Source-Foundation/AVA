use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use tokio::io::AsyncWriteExt;
use tokio::process::Command;

use crate::git::GitToolError;

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
    let mut child = Command::new("git")
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
    let output = Command::new("git")
        .arg("-C")
        .arg(repo_root)
        .args(args)
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
