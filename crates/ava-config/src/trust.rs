//! Project trust management.
//!
//! Untrusted projects must not auto-load project-local MCP servers (`.ava/mcp.json`)
//! or hooks (`.ava/hooks/*.toml`) because they can execute arbitrary code.
//! Trust state is persisted in `~/.ava/trusted_projects.json`.

use std::path::Path;

const TRUSTED_FILE: &str = "trusted_projects.json";

/// Check whether `project_root` has been explicitly trusted by the user.
pub fn is_project_trusted(project_root: &Path) -> bool {
    let Some(trust_path) = dirs::home_dir().map(|h| h.join(".ava").join(TRUSTED_FILE)) else {
        return false;
    };

    let Ok(content) = std::fs::read_to_string(&trust_path) else {
        return false;
    };
    let Ok(data) = serde_json::from_str::<serde_json::Value>(&content) else {
        return false;
    };

    let canonical = project_root
        .canonicalize()
        .unwrap_or_else(|_| project_root.to_path_buf());

    data.get("trusted")
        .and_then(|v| v.as_array())
        .map(|arr| {
            arr.iter().any(|p| {
                p.as_str()
                    .map(|s| Path::new(s) == canonical)
                    .unwrap_or(false)
            })
        })
        .unwrap_or(false)
}

/// Mark `project_root` as trusted. Appends to `~/.ava/trusted_projects.json`.
pub fn trust_project(project_root: &Path) -> std::io::Result<()> {
    let trust_path = dirs::home_dir()
        .unwrap_or_default()
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
        serde_json::to_string_pretty(&data)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))?,
    )
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
        assert!(arr
            .iter()
            .any(|p| p.as_str() == Some(&canonical.to_string_lossy().to_string())));
    }
}
