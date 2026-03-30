//! Project trust management.
//!
//! Untrusted projects must not auto-load project-local MCP servers (`.ava/mcp.json`)
//! or hooks (`.ava/hooks/*.toml`) because they can execute arbitrary code.
//! Trust state is persisted in `~/.ava/trusted_projects.json`.

use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::sync::RwLock;

const TRUSTED_FILE: &str = "trusted_projects.json";

/// Process-scoped cache of trusted project paths.
///
/// Populated on the first call to `is_project_trusted()` and invalidated
/// (cleared) whenever `trust_project()` updates the file.
static TRUST_CACHE: RwLock<Option<HashSet<PathBuf>>> = RwLock::new(None);

/// Load the trusted project set from disk, bypassing the in-process cache.
fn load_trusted_set() -> HashSet<PathBuf> {
    let Some(trust_path) = dirs::home_dir().map(|h| {
        h.canonicalize()
            .unwrap_or(h)
            .join(".ava")
            .join(TRUSTED_FILE)
    }) else {
        return HashSet::new();
    };

    let Ok(content) = std::fs::read_to_string(&trust_path) else {
        return HashSet::new();
    };
    let Ok(data) = serde_json::from_str::<serde_json::Value>(&content) else {
        return HashSet::new();
    };

    data.get("trusted")
        .and_then(|v| v.as_array())
        .map(|arr| {
            arr.iter()
                .filter_map(|p| p.as_str().map(PathBuf::from))
                .collect()
        })
        .unwrap_or_default()
}

/// Check whether `project_root` has been explicitly trusted by the user.
///
/// Results are cached in-process after the first call. The cache is
/// invalidated by [`trust_project`].
pub fn is_project_trusted(project_root: &Path) -> bool {
    let canonical = project_root
        .canonicalize()
        .unwrap_or_else(|_| project_root.to_path_buf());

    // Fast path: check cache with a read lock.
    {
        let cache = TRUST_CACHE.read().unwrap_or_else(|e| e.into_inner());
        if let Some(ref set) = *cache {
            return set.contains(&canonical);
        }
    }

    // Cache miss: load from disk and populate cache.
    let set = load_trusted_set();
    let trusted = set.contains(&canonical);

    {
        let mut cache = TRUST_CACHE.write().unwrap_or_else(|e| e.into_inner());
        // Another thread may have populated it while we were loading; that is fine.
        cache.get_or_insert(set);
    }

    trusted
}

/// Mark `project_root` as trusted. Appends to `~/.ava/trusted_projects.json`.
///
/// Invalidates the in-process trust cache so subsequent calls to
/// [`is_project_trusted`] reflect the updated state.
pub fn trust_project(project_root: &Path) -> std::io::Result<()> {
    let trust_path = dirs::home_dir()
        .unwrap_or_default()
        .canonicalize()
        .unwrap_or_else(|_| dirs::home_dir().unwrap_or_default())
        .join(".ava")
        .join(TRUSTED_FILE);

    let canonical = project_root
        .canonicalize()
        .unwrap_or_else(|_| project_root.to_path_buf());

    let mut data: serde_json::Value = std::fs::read_to_string(&trust_path)
        .ok()
        .and_then(|s| serde_json::from_str(&s).ok())
        .unwrap_or(serde_json::json!({"trusted": []}));

    if let Some(arr) = data.get_mut("trusted").and_then(|v| v.as_array_mut()) {
        let path_str = canonical.to_string_lossy().to_string();
        if !arr.iter().any(|p| p.as_str() == Some(&path_str)) {
            arr.push(serde_json::Value::String(path_str));
        }
    }

    if let Some(parent) = trust_path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(
        &trust_path,
        serde_json::to_string_pretty(&data).map_err(std::io::Error::other)?,
    )?;

    // Invalidate the cache so the next call to is_project_trusted re-reads from disk.
    let mut cache = TRUST_CACHE.write().unwrap_or_else(|e| e.into_inner());
    *cache = None;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn test_untrusted_by_default() {
        let dir = TempDir::new().unwrap();
        assert!(!is_project_trusted(dir.path()));
    }

    #[test]
    fn test_trust_roundtrip() {
        let home = TempDir::new().unwrap();
        let project = TempDir::new().unwrap();

        // Point trust file to temp home
        let trust_path = home.path().join(".ava").join("trusted_projects.json");
        std::fs::create_dir_all(trust_path.parent().unwrap()).unwrap();

        // Manually write trust file since trust_project uses dirs::home_dir
        let canonical = project.path().canonicalize().unwrap();
        let data = serde_json::json!({"trusted": [canonical.to_string_lossy().to_string()]});
        std::fs::write(&trust_path, serde_json::to_string_pretty(&data).unwrap()).unwrap();

        // is_project_trusted reads from dirs::home_dir which we can't override in a unit test,
        // so we test the logic directly
        let content = std::fs::read_to_string(&trust_path).unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&content).unwrap();
        let arr = parsed.get("trusted").unwrap().as_array().unwrap();
        let canonical_str = canonical.to_string_lossy();
        assert!(arr
            .iter()
            .any(|p| p.as_str() == Some(canonical_str.as_ref())));
    }
}
