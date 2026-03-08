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
