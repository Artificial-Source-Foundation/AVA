use std::path::{Component, Path, PathBuf};

use ava_types::{AvaError, Result};

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

    if !resolved.starts_with(&workspace) {
        return Err(AvaError::PermissionDenied(format!(
            "{tool_name} path {} is outside workspace {}",
            resolved.display(),
            workspace.display()
        )));
    }

    Ok(resolved)
}

fn workspace_root() -> Result<PathBuf> {
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

    #[test]
    fn path_guard_enforces_workspace_boundary_checks() {
        let ws = TempDir::new().unwrap();

        let previous = std::env::var_os("AVA_WORKSPACE");
        std::env::set_var("AVA_WORKSPACE", ws.path());

        let target = ws.path().join("src/main.rs");
        std::fs::create_dir_all(target.parent().unwrap()).unwrap();
        std::fs::write(&target, "test").unwrap();

        let resolved = enforce_workspace_path("src/main.rs", "read").unwrap();
        assert_eq!(resolved, target.canonicalize().unwrap());

        let new_file = enforce_workspace_path("new/file.txt", "write").unwrap();
        assert!(new_file.starts_with(ws.path()));

        let outside_abs = enforce_workspace_path("/etc/passwd", "read");
        assert!(outside_abs.is_err());

        let traversal_escape = enforce_workspace_path("../outside.txt", "write");
        assert!(traversal_escape.is_err());

        match previous {
            Some(previous_path) => std::env::set_var("AVA_WORKSPACE", previous_path),
            None => std::env::remove_var("AVA_WORKSPACE"),
        }
    }
}
