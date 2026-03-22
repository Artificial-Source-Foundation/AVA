//! Persistent file edit backups.
//!
//! Before every file write or edit, the original content is saved to
//! `~/.ava/file-history/{session_id}/{path_hash}@v{N}` so that changes survive
//! crashes. A companion `.meta` sidecar records the original absolute path.
//!
//! The session ID is held in a shared [`FileBackupSession`] that is set when
//! the agent run starts and read by the write/edit tools before each mutation.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::sync::Arc;

use tokio::sync::RwLock;

/// Shared session identifier for file backups.
///
/// Constructed once during tool registration and later populated via
/// [`set_session_id`](FileBackupSession::write) when the agent run begins.
pub type FileBackupSession = Arc<RwLock<Option<String>>>;

/// Create a new, empty backup session handle.
pub fn new_backup_session() -> FileBackupSession {
    Arc::new(RwLock::new(None))
}

/// Back up a file's current content before it is modified.
///
/// If the file does not exist yet (new file), this is a no-op.
/// If no session ID has been set, the backup is silently skipped.
///
/// The backup is written to `~/.ava/file-history/{session_id}/{hash}@v{N}`
/// with a sidecar `{hash}@v{N}.meta` containing the original path.
pub async fn backup_file_before_edit(
    session: &FileBackupSession,
    file_path: &Path,
) -> Result<Option<PathBuf>, String> {
    // Read the session ID; skip if not set.
    let session_id = {
        let guard = session.read().await;
        match guard.as_ref() {
            Some(id) => id.clone(),
            None => return Ok(None),
        }
    };

    // Only back up files that already exist (new files have no content to save).
    if !file_path.exists() {
        return Ok(None);
    }

    let content = tokio::fs::read(file_path)
        .await
        .map_err(|e| format!("failed to read file for backup: {e}"))?;

    let base_dir = backup_dir(&session_id)?;
    tokio::fs::create_dir_all(&base_dir)
        .await
        .map_err(|e| format!("failed to create backup dir: {e}"))?;

    let path_hash = hash_path(file_path);
    let version = next_version(&base_dir, &path_hash).await;
    let backup_name = format!("{path_hash}@v{version}");
    let backup_path = base_dir.join(&backup_name);
    let meta_path = base_dir.join(format!("{backup_name}.meta"));

    tokio::fs::write(&backup_path, &content)
        .await
        .map_err(|e| format!("failed to write backup: {e}"))?;

    // Sidecar contains the original absolute path for later restoration.
    let abs_path = file_path
        .canonicalize()
        .unwrap_or_else(|_| file_path.to_path_buf());
    tokio::fs::write(&meta_path, abs_path.to_string_lossy().as_bytes())
        .await
        .map_err(|e| format!("failed to write backup meta: {e}"))?;

    tracing::debug!(
        backup = %backup_path.display(),
        original = %file_path.display(),
        version,
        "file backup saved"
    );

    Ok(Some(backup_path))
}

/// Restore a file from a backup.
///
/// If `version` is `None`, the latest version is restored.
pub async fn restore_file_backup(
    session_id: &str,
    file_path: &Path,
    version: Option<usize>,
) -> Result<PathBuf, String> {
    let base_dir = backup_dir(session_id)?;
    let path_hash = hash_path(file_path);

    let target_version = match version {
        Some(v) => v,
        None => {
            let latest = next_version(&base_dir, &path_hash).await;
            if latest == 0 {
                return Err(format!("no backups found for {}", file_path.display()));
            }
            latest - 1
        }
    };

    let backup_name = format!("{path_hash}@v{target_version}");
    let backup_path = base_dir.join(&backup_name);

    if !backup_path.exists() {
        return Err(format!(
            "backup version {target_version} not found for {}",
            file_path.display()
        ));
    }

    let content = tokio::fs::read(&backup_path)
        .await
        .map_err(|e| format!("failed to read backup: {e}"))?;

    // Ensure parent directories exist.
    if let Some(parent) = file_path.parent() {
        tokio::fs::create_dir_all(parent)
            .await
            .map_err(|e| format!("failed to create parent dir: {e}"))?;
    }

    tokio::fs::write(file_path, &content)
        .await
        .map_err(|e| format!("failed to restore file: {e}"))?;

    tracing::info!(
        backup = %backup_path.display(),
        restored = %file_path.display(),
        version = target_version,
        "file restored from backup"
    );

    Ok(backup_path)
}

/// List all backup versions for a file in a session.
pub async fn list_backups(
    session_id: &str,
    file_path: &Path,
) -> Result<Vec<(usize, PathBuf)>, String> {
    let base_dir = backup_dir(session_id)?;
    let path_hash = hash_path(file_path);
    let mut results = Vec::new();

    let mut version = 0usize;
    loop {
        let backup_path = base_dir.join(format!("{path_hash}@v{version}"));
        if backup_path.exists() {
            results.push((version, backup_path));
            version += 1;
        } else {
            break;
        }
    }

    Ok(results)
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Compute the backup base directory: `~/.ava/file-history/{session_id}/`.
fn backup_dir(session_id: &str) -> Result<PathBuf, String> {
    let home = dirs::home_dir().ok_or("could not determine home directory")?;
    Ok(home.join(".ava").join("file-history").join(session_id))
}

/// Produce a stable, filesystem-safe hash of a file path.
fn hash_path(file_path: &Path) -> String {
    let mut hasher = DefaultHasher::new();
    // Canonicalize if possible for stability, otherwise use as-is.
    let canonical = file_path
        .canonicalize()
        .unwrap_or_else(|_| file_path.to_path_buf());
    canonical.to_string_lossy().hash(&mut hasher);
    format!("{:016x}", hasher.finish())
}

/// Find the next available version number for a given path hash prefix.
async fn next_version(base_dir: &Path, path_hash: &str) -> usize {
    let mut version = 0usize;
    loop {
        let candidate = base_dir.join(format!("{path_hash}@v{version}"));
        if candidate.exists() {
            version += 1;
        } else {
            return version;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    /// Override backup_dir for tests by using a custom base.
    async fn backup_to_dir(
        base: &Path,
        file_path: &Path,
        content: &[u8],
    ) -> Result<PathBuf, String> {
        tokio::fs::create_dir_all(base)
            .await
            .map_err(|e| format!("{e}"))?;
        let path_hash = hash_path(file_path);
        let version = next_version(base, &path_hash).await;
        let name = format!("{path_hash}@v{version}");
        let backup_path = base.join(&name);
        let meta_path = base.join(format!("{name}.meta"));
        tokio::fs::write(&backup_path, content)
            .await
            .map_err(|e| format!("{e}"))?;
        tokio::fs::write(&meta_path, file_path.to_string_lossy().as_bytes())
            .await
            .map_err(|e| format!("{e}"))?;
        Ok(backup_path)
    }

    #[tokio::test]
    async fn backup_and_restore_roundtrip() {
        let tmp = TempDir::new().unwrap();
        let file = tmp.path().join("test.txt");
        let backup_base = tmp.path().join("backups");

        // Write original content.
        tokio::fs::write(&file, b"hello v0").await.unwrap();

        // Backup v0.
        let bp = backup_to_dir(&backup_base, &file, b"hello v0")
            .await
            .unwrap();
        assert!(bp.exists());

        // Modify and backup v1.
        tokio::fs::write(&file, b"hello v1").await.unwrap();
        let bp2 = backup_to_dir(&backup_base, &file, b"hello v1")
            .await
            .unwrap();
        assert!(bp2.exists());
        assert_ne!(bp, bp2);

        // Restore v0.
        let path_hash = hash_path(&file);
        let v0_path = backup_base.join(format!("{path_hash}@v0"));
        let content = tokio::fs::read(&v0_path).await.unwrap();
        assert_eq!(content, b"hello v0");
    }

    #[tokio::test]
    async fn version_increments() {
        let tmp = TempDir::new().unwrap();
        let base = tmp.path().join("backups");
        tokio::fs::create_dir_all(&base).await.unwrap();

        let hash = "abc123";
        assert_eq!(next_version(&base, hash).await, 0);

        tokio::fs::write(base.join(format!("{hash}@v0")), b"x")
            .await
            .unwrap();
        assert_eq!(next_version(&base, hash).await, 1);

        tokio::fs::write(base.join(format!("{hash}@v1")), b"y")
            .await
            .unwrap();
        assert_eq!(next_version(&base, hash).await, 2);
    }

    #[tokio::test]
    async fn skip_when_no_session() {
        let session = new_backup_session();
        let tmp = TempDir::new().unwrap();
        let file = tmp.path().join("test.txt");
        tokio::fs::write(&file, b"content").await.unwrap();

        let result = backup_file_before_edit(&session, &file).await.unwrap();
        assert!(result.is_none(), "should skip backup when no session set");
    }

    #[tokio::test]
    async fn skip_when_file_missing() {
        let session = new_backup_session();
        {
            let mut guard = session.write().await;
            *guard = Some("test-session".to_string());
        }
        let path = Path::new("/tmp/nonexistent-ava-test-file.txt");

        let result = backup_file_before_edit(&session, path).await.unwrap();
        assert!(result.is_none(), "should skip backup for nonexistent file");
    }

    #[test]
    fn hash_path_is_stable() {
        let p = Path::new("/some/test/path.rs");
        let h1 = hash_path(p);
        let h2 = hash_path(p);
        assert_eq!(h1, h2);
        assert_eq!(h1.len(), 16); // 16 hex chars
    }
}
