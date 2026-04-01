use std::path::PathBuf;

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

    fn search_hint(&self) -> &str {
        "find files pattern directory match"
    }

    fn activity_description(&self, args: &Value) -> Option<String> {
        let pattern = args.get("pattern").and_then(Value::as_str)?;
        Some(format!("Searching for {pattern}"))
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let pattern = args.get("pattern").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: pattern".to_string())
        })?;
        let base = args.get("path").and_then(Value::as_str).unwrap_or(".");

        tracing::debug!(tool = "glob", %pattern, base = %base, "executing glob tool");

        let base_path = crate::core::path_guard::enforce_workspace_path(base, "glob")?;

        let query = base_path.join(pattern).to_string_lossy().to_string();

        let mut matches: Vec<PathBuf> = Vec::new();
        for entry in glob::glob(&query).map_err(|e| AvaError::ToolError(e.to_string()))? {
            let path = entry.map_err(|e| AvaError::ToolError(e.to_string()))?;
            matches.push(path);
        }

        matches.sort_by(|left, right| left.to_string_lossy().cmp(&right.to_string_lossy()));
        let truncated = matches.len() > MAX_RESULTS;
        if matches.len() > MAX_RESULTS {
            matches.truncate(MAX_RESULTS);
        }

        let mut lines = matches
            .into_iter()
            .map(|path| path.to_string_lossy().to_string())
            .collect::<Vec<String>>();
        if truncated {
            lines.push(String::new());
            lines.push(format!(
                "(Results are truncated: showing first {MAX_RESULTS} results. Consider using a more specific path or pattern.)"
            ));
        }

        let content = lines.join("\n");

        Ok(ToolResult {
            call_id: String::new(),
            content,
            is_error: false,
        })
    }

    fn is_concurrency_safe(&self, _args: &serde_json::Value) -> bool {
        true
    }
}
