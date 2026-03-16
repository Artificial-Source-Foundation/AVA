use std::path::PathBuf;

use async_trait::async_trait;
use ava_codebase::{index_project, SearchQuery};
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct CodeSearchTool;

impl Default for CodeSearchTool {
    fn default() -> Self {
        Self::new()
    }
}

impl CodeSearchTool {
    pub fn new() -> Self {
        Self
    }
}

#[async_trait]
impl Tool for CodeSearchTool {
    fn name(&self) -> &str {
        "code_search"
    }

    fn description(&self) -> &str {
        "Search indexed project code with lexical or hybrid ranking"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["query"],
            "properties": {
                "query": {
                    "type": "string",
                    "description": "Search query string"
                },
                "root": {
                    "type": "string",
                    "description": "Project root to index (default: current working directory)"
                },
                "max_results": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 100,
                    "description": "Maximum number of results to return (default: 10)"
                },
                "mode": {
                    "type": "string",
                    "enum": ["lexical", "hybrid"],
                    "description": "Search mode (default: lexical)"
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let query = args
            .get("query")
            .and_then(Value::as_str)
            .map(str::trim)
            .filter(|q| !q.is_empty())
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: query".to_string())
            })?;

        let root = args
            .get("root")
            .and_then(Value::as_str)
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("."));
        let root = validate_root_path(root)?;

        let max_results = args
            .get("max_results")
            .and_then(Value::as_u64)
            .unwrap_or(10)
            .clamp(1, 100) as usize;

        let mode = args
            .get("mode")
            .and_then(Value::as_str)
            .unwrap_or("lexical");

        // TODO: Reuse shared CodebaseIndex instead of rebuilding per invocation.
        // This is a performance issue, not a security issue.
        let index = index_project(&root)
            .await
            .map_err(|e| AvaError::ToolError(format!("failed to index project: {e}")))?;

        let search_query = SearchQuery::new(query).with_max_results(max_results);
        let hits = match mode {
            "lexical" => index
                .search
                .search(&search_query)
                .map_err(|e| AvaError::ToolError(format!("search failed: {e}")))?,
            "hybrid" => index
                .hybrid_search(&search_query)
                .map_err(|e| AvaError::ToolError(format!("search failed: {e}")))?,
            other => {
                return Err(AvaError::ValidationError(format!(
                    "unsupported code_search mode: {other}"
                )))
            }
        };

        let results = hits
            .into_iter()
            .map(|hit| {
                json!({
                    "path": hit.path,
                    "score": hit.score,
                    "snippet": hit.snippet,
                })
            })
            .collect::<Vec<_>>();

        Ok(ToolResult {
            call_id: String::new(),
            content: serde_json::to_string_pretty(&json!({
                "query": query,
                "mode": mode,
                "result_count": results.len(),
                "results": results,
            }))
            .map_err(|e| AvaError::SerializationError(e.to_string()))?,
            is_error: false,
        })
    }
}

fn validate_root_path(root: PathBuf) -> ava_types::Result<PathBuf> {
    let cwd = std::env::current_dir().map_err(|e| AvaError::IoError(e.to_string()))?;
    let canonical_root = root
        .canonicalize()
        .map_err(|e| AvaError::ValidationError(format!("invalid root path: {e}")))?;
    let canonical_cwd = cwd
        .canonicalize()
        .map_err(|e| AvaError::IoError(e.to_string()))?;

    if !canonical_root.starts_with(&canonical_cwd) {
        return Err(AvaError::PermissionDenied(format!(
            "code_search root must stay within workspace: {}",
            canonical_cwd.display()
        )));
    }

    Ok(canonical_root)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn code_search_metadata_is_valid() {
        let tool = CodeSearchTool::new();
        assert_eq!(tool.name(), "code_search");
        assert!(tool.parameters()["properties"]["query"].is_object());
    }

    #[tokio::test]
    async fn code_search_requires_query() {
        let tool = CodeSearchTool::new();
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn code_search_finds_project_content() {
        let root = std::env::current_dir()
            .expect("cwd")
            .join(format!(".tmp-code-search-{}", uuid::Uuid::new_v4()));
        std::fs::create_dir_all(&root).expect("create root");
        let src = root.join("src");
        tokio::fs::create_dir_all(&src).await.expect("create src");
        tokio::fs::write(
            src.join("main.rs"),
            "fn auth_guard() { println!(\"token\"); }",
        )
        .await
        .expect("write file");

        let tool = CodeSearchTool::new();
        let result = tool
            .execute(json!({
                "query": "auth_guard",
                "root": root.to_string_lossy().to_string(),
                "mode": "lexical"
            }))
            .await
            .expect("search runs");

        assert!(result.content.contains("main.rs"));

        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn code_search_rejects_root_outside_workspace() {
        let result = validate_root_path(PathBuf::from("/"));
        assert!(result.is_err());
    }
}
