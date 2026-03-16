use std::path::{Path, PathBuf};
use std::time::SystemTime;

use async_trait::async_trait;
use serde_json::{json, Value};

use ava_types::{AvaError, ToolResult};

use crate::registry::Tool;

const MAX_RESULTS: usize = 1000;

pub struct GlobTool;

impl GlobTool {
    pub fn new() -> Self {
        Self
    }
}

impl Default for GlobTool {
    fn default() -> Self {
        Self::new()
    }
}

#[async_trait]
impl Tool for GlobTool {
    fn name(&self) -> &str {
        "glob"
    }

    fn description(&self) -> &str {
        "Find files by glob pattern"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["pattern"],
            "properties": {
                "pattern": { "type": "string" },
                "path": { "type": "string" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let pattern = args.get("pattern").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: pattern".to_string())
        })?;
        let base = args.get("path").and_then(Value::as_str).unwrap_or(".");

        // Workspace boundary enforcement: prevent glob from searching outside the working directory.
        // Uses AVA_WORKSPACE env var if set, otherwise falls back to current directory.
        if let Ok(base_canonical) = std::fs::canonicalize(base) {
            let workspace = std::env::var("AVA_WORKSPACE")
                .ok()
                .and_then(|w| std::fs::canonicalize(w).ok())
                .unwrap_or_else(|| std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")));
            if !base_canonical.starts_with(&workspace) {
                return Err(AvaError::PermissionDenied(format!(
                    "glob base path {} is outside workspace {}",
                    base_canonical.display(),
                    workspace.display()
                )));
            }
        }

        let query = Path::new(base).join(pattern).to_string_lossy().to_string();

        let mut matches: Vec<(PathBuf, SystemTime)> = Vec::new();
        for entry in glob::glob(&query).map_err(|e| AvaError::ToolError(e.to_string()))? {
            let path = entry.map_err(|e| AvaError::ToolError(e.to_string()))?;
            let modified = std::fs::metadata(&path)
                .and_then(|m| m.modified())
                .unwrap_or(SystemTime::UNIX_EPOCH);
            matches.push((path, modified));
        }

        matches.sort_by(|a, b| b.1.cmp(&a.1));
        if matches.len() > MAX_RESULTS {
            matches.truncate(MAX_RESULTS);
        }

        let content = matches
            .into_iter()
            .map(|(path, _)| path.to_string_lossy().to_string())
            .collect::<Vec<String>>()
            .join("\n");

        Ok(ToolResult {
            call_id: String::new(),
            content,
            is_error: false,
        })
    }
}
