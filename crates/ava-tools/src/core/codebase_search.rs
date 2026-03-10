use std::sync::Arc;

use async_trait::async_trait;
use ava_codebase::{CodebaseIndex, SearchQuery};
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};
use tokio::sync::RwLock;

use crate::registry::Tool;

pub struct CodebaseSearchTool {
    index: Arc<RwLock<Option<Arc<CodebaseIndex>>>>,
}

impl CodebaseSearchTool {
    pub fn new(index: Arc<RwLock<Option<Arc<CodebaseIndex>>>>) -> Self {
        Self { index }
    }
}

#[async_trait]
impl Tool for CodebaseSearchTool {
    fn name(&self) -> &str {
        "codebase_search"
    }

    fn description(&self) -> &str {
        "Search the codebase index for files matching a query (BM25 ranked)"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["query"],
            "properties": {
                "query": { "type": "string", "description": "Search query" },
                "limit": { "type": "integer", "minimum": 1, "description": "Max results (default 10)" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let query = args
            .get("query")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: query".into()))?;
        let limit = args
            .get("limit")
            .and_then(Value::as_u64)
            .unwrap_or(10) as usize;

        let guard = self.index.read().await;
        let Some(index) = guard.as_ref() else {
            return Ok(ToolResult {
                call_id: String::new(),
                content: "Codebase index is not yet available (still building or not configured)."
                    .to_string(),
                is_error: false,
            });
        };

        let search_query = SearchQuery::new(query).with_max_results(limit);
        let hits = index
            .search
            .search(&search_query)
            .map_err(|e| AvaError::ToolError(e.to_string()))?;

        if hits.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("No results for: {query}"),
                is_error: false,
            });
        }

        let entries: Vec<String> = hits
            .iter()
            .map(|hit| format!("- {} (score: {:.3}): {}", hit.path, hit.score, hit.snippet))
            .collect();

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Found {} results:\n{}", entries.len(), entries.join("\n")),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn empty_index() -> Arc<RwLock<Option<Arc<CodebaseIndex>>>> {
        Arc::new(RwLock::new(None))
    }

    #[test]
    fn tool_metadata() {
        let tool = CodebaseSearchTool::new(empty_index());
        assert_eq!(tool.name(), "codebase_search");
        assert!(!tool.description().is_empty());
        let params = tool.parameters();
        assert_eq!(params["required"], json!(["query"]));
    }

    #[tokio::test]
    async fn returns_not_available_when_no_index() {
        let tool = CodebaseSearchTool::new(empty_index());
        let result = tool.execute(json!({"query": "hello"})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("not yet available"));
    }

    #[tokio::test]
    async fn missing_query_errors() {
        let tool = CodebaseSearchTool::new(empty_index());
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn with_index_empty_results() {
        // Build a real index on a temp dir with no source files
        let dir = tempfile::tempdir().unwrap();
        tokio::fs::write(dir.path().join("README.md"), "# Empty project")
            .await
            .unwrap();

        let index = ava_codebase::indexer::index_project(dir.path()).await.unwrap();
        let shared = Arc::new(RwLock::new(Some(Arc::new(index))));
        let tool = CodebaseSearchTool::new(shared);

        let result = tool
            .execute(json!({"query": "nonexistent_symbol_xyz123"}))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("No results"));
    }

    #[tokio::test]
    async fn with_index_finds_content() {
        let dir = tempfile::tempdir().unwrap();
        tokio::fs::write(
            dir.path().join("main.rs"),
            "fn calculate_fibonacci(n: u64) -> u64 { if n <= 1 { n } else { calculate_fibonacci(n-1) + calculate_fibonacci(n-2) } }",
        )
        .await
        .unwrap();

        let index = ava_codebase::indexer::index_project(dir.path()).await.unwrap();
        let shared = Arc::new(RwLock::new(Some(Arc::new(index))));
        let tool = CodebaseSearchTool::new(shared);

        let result = tool
            .execute(json!({"query": "fibonacci"}))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("Found"));
        assert!(result.content.contains("main.rs"));
    }
}
