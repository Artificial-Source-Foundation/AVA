use std::path::{Component, Path, PathBuf};

use ava_types::{AvaError, Result};

#[cfg(test)]
static TEST_WORKSPACE_OVERRIDE: std::sync::LazyLock<std::sync::Mutex<Option<PathBuf>>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(None));
#[cfg(test)]
static TEST_WORKSPACE_LOCK: std::sync::LazyLock<std::sync::Mutex<()>> =
    std::sync::LazyLock::new(|| std::sync::Mutex::new(()));

/// Resolve a tool path to a workspace-safe absolute path.
///
/// Existing files are canonicalized to catch symlink escapes. For non-existing
/// paths, this function resolves the parent directory when possible and then
/// applies lexical normalization so new-file operations still cannot escape the
/// workspace.
pub fn enforce_workspace_path(input: &str, tool_name: &str) -> Result<PathBuf> {
    let workspace = workspace_root()?;
    let raw = Path::new(input);
    let absolute = if raw.is_absolute() {
        raw.to_path_buf()
    } else {
        workspace.join(raw)
    };

    let resolved = if absolute.exists() {
        std::fs::canonicalize(&absolute).map_err(|e| {
            AvaError::IoError(format!(
                "{tool_name} path resolution failed for {}: {e}",
                absolute.display()
            ))
        })?
    } else {
        let parent = absolute.parent().unwrap_or_else(|| Path::new("."));
        let parent_abs = if parent.is_absolute() {
            parent.to_path_buf()
        } else {
            workspace.join(parent)
        };
        let parent_resolved = if parent_abs.exists() {
            std::fs::canonicalize(&parent_abs).map_err(|e| {
                AvaError::IoError(format!(
                    "{tool_name} parent resolution failed for {}: {e}",
                    parent_abs.display()
                ))
            })?
        } else {
            lexical_normalize(&parent_abs)
        };

        if !parent_resolved.starts_with(&workspace) {
            return Err(AvaError::PermissionDenied(format!(
                "{tool_name} path {input} is outside workspace {}",
                workspace.display()
            )));
        }

        let Some(file_name) = absolute.file_name() else {
            return Ok(lexical_normalize(&absolute));
        };

        parent_resolved.join(file_name)
    };

    check_symlink_escape(&resolved, &workspace, input, tool_name)?;

    Ok(resolved)
}

/// Check if a resolved (canonicalized) path escapes the workspace boundary.
///
/// This catches symlink-based escapes: a symlink inside the workspace that
/// points to a location outside it. After `std::fs::canonicalize()` resolves
/// all symlinks, the canonical path must still reside under the workspace root.
///
/// For paths that don't exist yet, callers should canonicalize the parent
/// directory and append the filename before calling this function.
pub fn check_symlink_escape(
    resolved_path: &Path,
    workspace_root: &Path,
    original_input: &str,
    tool_name: &str,
) -> Result<()> {
    if !resolved_path.starts_with(workspace_root) {
        // Determine if this was a symlink escape vs a plain path escape
        let raw = Path::new(original_input);
        let inspected = if raw.is_absolute() {
            raw.to_path_buf()
        } else {
            workspace_root.join(raw)
        };
        let is_symlink = inspected.is_symlink()
            || inspected
                .parent()
                .map(|parent| parent.is_symlink())
                .unwrap_or(false);
        let reason = if is_symlink {
            "path escapes workspace boundary via symlink"
        } else {
            "path is outside workspace"
        };
        return Err(AvaError::PermissionDenied(format!(
            "{tool_name}: {reason} — resolved {} is outside {}",
            resolved_path.display(),
            workspace_root.display()
        )));
    }
    Ok(())
}

pub(crate) fn workspace_root() -> Result<PathBuf> {
    #[cfg(test)]
    if let Some(workspace) = TEST_WORKSPACE_OVERRIDE
        .lock()
        .unwrap_or_else(|error| error.into_inner())
        .clone()
    {
        return Ok(workspace);
    }

    if let Ok(workspace) = std::env::var("AVA_WORKSPACE") {
        let workspace = Path::new(&workspace);
        return Ok(std::fs::canonicalize(workspace).unwrap_or_else(|_| workspace.to_path_buf()));
    }

    std::env::current_dir()
        .and_then(|p| std::fs::canonicalize(&p).or(Ok(p)))
        .map_err(|e| AvaError::IoError(format!("failed to determine workspace root: {e}")))
}

fn lexical_normalize(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for part in path.components() {
        match part {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            Component::Normal(part) => out.push(part),
            Component::RootDir => {
                out.push("/");
            }
            Component::Prefix(prefix) => {
                out.push(prefix.as_os_str());
            }
        }
    }
    if out.as_os_str().is_empty() {
        out.push(".");
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn set_workspace(path: &Path) {
        *TEST_WORKSPACE_OVERRIDE
            .lock()
            .unwrap_or_else(|error| error.into_inner()) =
            Some(path.canonicalize().unwrap_or_else(|_| path.to_path_buf()));
    }

    fn restore_workspace(previous: Option<PathBuf>) {
        *TEST_WORKSPACE_OVERRIDE
            .lock()
            .unwrap_or_else(|error| error.into_inner()) = previous;
    }

    #[test]
    fn path_guard_enforces_workspace_boundary_checks() {
        let _guard = TEST_WORKSPACE_LOCK
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let ws = TempDir::new().unwrap();
        let ws_root = ws.path().canonicalize().unwrap();

        let previous = TEST_WORKSPACE_OVERRIDE
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone();
        set_workspace(ws.path());

        let target = ws.path().join("src/main.rs");
        std::fs::create_dir_all(target.parent().unwrap()).unwrap();
        std::fs::write(&target, "test").unwrap();

        let resolved = enforce_workspace_path("src/main.rs", "read").unwrap();
        assert_eq!(resolved, target.canonicalize().unwrap());

        let new_file = enforce_workspace_path("new/file.txt", "write").unwrap();
        assert!(new_file.starts_with(&ws_root));

        let outside_abs = enforce_workspace_path("/etc/passwd", "read");
        assert!(outside_abs.is_err());

        let traversal_escape = enforce_workspace_path("../outside.txt", "write");
        assert!(traversal_escape.is_err());

        restore_workspace(previous);
    }

    #[cfg(unix)]
    #[test]
    fn symlink_file_escape_is_detected() {
        let _guard = TEST_WORKSPACE_LOCK
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let ws = TempDir::new().unwrap();
        let outside = TempDir::new().unwrap();

        // Create a file outside the workspace
        let outside_file = outside.path().join("secret.txt");
        std::fs::write(&outside_file, "secret data").unwrap();

        // Create a symlink inside the workspace pointing outside
        let symlink_path = ws.path().join("escape.txt");
        std::os::unix::fs::symlink(&outside_file, &symlink_path).unwrap();

        let previous = TEST_WORKSPACE_OVERRIDE
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone();
        set_workspace(ws.path());

        let result = enforce_workspace_path("escape.txt", "write");
        assert!(result.is_err(), "symlink file escape should be blocked");
        let err_msg = result.unwrap_err().to_string();
        assert!(
            err_msg.contains("symlink") || err_msg.contains("outside workspace"),
            "error should mention symlink escape, got: {err_msg}"
        );

        restore_workspace(previous);
    }

    #[cfg(unix)]
    #[test]
    fn symlink_directory_escape_is_detected() {
        let _guard = TEST_WORKSPACE_LOCK
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let ws = TempDir::new().unwrap();
        let outside = TempDir::new().unwrap();

        // Create a directory outside the workspace with a file
        let outside_dir = outside.path().join("data");
        std::fs::create_dir_all(&outside_dir).unwrap();
        std::fs::write(outside_dir.join("config.toml"), "key = true").unwrap();

        // Create a symlinked directory inside the workspace
        let symlink_dir = ws.path().join("linked");
        std::os::unix::fs::symlink(&outside_dir, &symlink_dir).unwrap();

        let previous = TEST_WORKSPACE_OVERRIDE
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone();
        set_workspace(ws.path());

        let result = enforce_workspace_path("linked/config.toml", "edit");
        assert!(
            result.is_err(),
            "symlink directory escape should be blocked"
        );

        restore_workspace(previous);
    }

    #[cfg(unix)]
    #[test]
    fn symlink_within_workspace_is_allowed() {
        let _guard = TEST_WORKSPACE_LOCK
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let ws = TempDir::new().unwrap();

        // Create a real file inside the workspace
        let real_file = ws.path().join("real.txt");
        std::fs::write(&real_file, "data").unwrap();

        // Create a symlink inside the workspace pointing to another workspace file
        let symlink_path = ws.path().join("link.txt");
        std::os::unix::fs::symlink(&real_file, &symlink_path).unwrap();

        let previous = TEST_WORKSPACE_OVERRIDE
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone();
        set_workspace(ws.path());

        let result = enforce_workspace_path("link.txt", "write");
        assert!(result.is_ok(), "symlink within workspace should be allowed");

        restore_workspace(previous);
    }

    #[cfg(unix)]
    #[test]
    fn new_file_under_symlinked_parent_escape_is_detected() {
        let _guard = TEST_WORKSPACE_LOCK
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let ws = TempDir::new().unwrap();
        let outside = TempDir::new().unwrap();

        // Symlink a directory inside workspace to outside
        let outside_dir = outside.path().join("ext");
        std::fs::create_dir_all(&outside_dir).unwrap();
        let symlink_dir = ws.path().join("ext");
        std::os::unix::fs::symlink(&outside_dir, &symlink_dir).unwrap();

        let previous = TEST_WORKSPACE_OVERRIDE
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone();
        set_workspace(ws.path());

        // Try to create a new file under the symlinked directory
        let result = enforce_workspace_path("ext/newfile.rs", "write");
        assert!(
            result.is_err(),
            "new file under symlinked escape directory should be blocked"
        );

        restore_workspace(previous);
    }
}
