use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_codebase::{index_project, CodebaseIndex, SearchQuery};
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};
use tokio::sync::RwLock;

use crate::registry::Tool;

/// Shared codebase index type — the same `Arc<RwLock<Option<Arc<CodebaseIndex>>>>` that
/// `AgentStack` holds and populates in a background task at startup.
pub type SharedCodebaseIndex = Arc<RwLock<Option<Arc<CodebaseIndex>>>>;

pub struct CodeSearchTool {
    /// When set, the tool will reuse this pre-built index instead of rebuilding
    /// one on every invocation.  The `Option<Arc<CodebaseIndex>>` inside the
    /// lock starts as `None` while the background indexing task is still
    /// running; once the task completes it becomes `Some(...)`.
    shared_index: Option<SharedCodebaseIndex>,
}

impl Default for CodeSearchTool {
    fn default() -> Self {
        Self::new()
    }
}

impl CodeSearchTool {
    /// Create a standalone tool with no shared index (falls back to building
    /// a fresh index on every invocation — the original behaviour).
    pub fn new() -> Self {
        Self { shared_index: None }
    }

    /// Create a tool that will reuse `index` when it is populated, avoiding
    /// a full rebuild on every invocation.
    pub fn with_shared_index(index: SharedCodebaseIndex) -> Self {
        Self {
            shared_index: Some(index),
        }
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

        tracing::debug!(tool = "code_search", %query, %mode, "executing code_search tool");

        // Attempt to reuse the shared index supplied at construction time.
        // If the shared index is not yet populated (background task still
        // running), or no shared index was provided, fall back to building
        // a fresh one.
        let owned_index;
        let index: &CodebaseIndex =
            if let Some(shared) = &self.shared_index {
                let guard = shared.read().await;
                if let Some(idx) = guard.as_ref() {
                    // The shared index is ready — clone the Arc so we can drop the
                    // read-lock before performing the (potentially slow) search.
                    owned_index = Arc::clone(idx);
                    drop(guard);
                    &owned_index
                } else {
                    // Index not yet ready; build a fresh one for this call.
                    drop(guard);
                    tracing::debug!(
                        tool = "code_search",
                        "shared index not yet populated, building fresh index"
                    );
                    owned_index = Arc::new(index_project(&root).await.map_err(|e| {
                        AvaError::ToolError(format!("failed to index project: {e}"))
                    })?);
                    &owned_index
                }
            } else {
                owned_index =
                    Arc::new(index_project(&root).await.map_err(|e| {
                        AvaError::ToolError(format!("failed to index project: {e}"))
                    })?);
                &owned_index
            };

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
