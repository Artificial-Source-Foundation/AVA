//! F11 — ToolSearch: a meta-tool that lets the LLM discover deferred tools.
//!
//! When the system prompt only lists tool names (not full schemas), the LLM
//! can call `tool_search` with a keyword query to get complete schemas for
//! matching tools.

use std::sync::Arc;

use async_trait::async_trait;
use ava_types::ToolResult;
use serde_json::{json, Value};

use crate::registry::{Tool, ToolRegistry};

pub struct ToolSearchTool {
    registry: Arc<ToolRegistry>,
}

impl ToolSearchTool {
    pub fn new(registry: Arc<ToolRegistry>) -> Self {
        Self { registry }
    }
}

#[async_trait]
impl Tool for ToolSearchTool {
    fn name(&self) -> &str {
        "tool_search"
    }

    fn description(&self) -> &str {
        "Search for available tools by keyword. Returns full schemas for matching tools, \
         including deferred tools not shown in the main tool list."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["query"],
            "properties": {
                "query": {
                    "type": "string",
                    "description": "Keywords to search for (matches tool names and descriptions)"
                },
                "max_results": {
                    "type": "integer",
                    "description": "Maximum number of results to return (default: 5)",
                    "minimum": 1,
                    "maximum": 20
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let query = args
            .get("query")
            .and_then(Value::as_str)
            .unwrap_or_default();

        let max_results = args.get("max_results").and_then(Value::as_u64).unwrap_or(5) as usize;

        if query.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: "Please provide a search query.".to_string(),
                is_error: true,
            });
        }

        let mut matches = self.registry.search_tools(query);
        matches.truncate(max_results);

        tracing::info!(
            query,
            match_count = matches.len(),
            "F11: tool search executed"
        );

        if matches.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("No tools found matching '{query}'."),
                is_error: false,
            });
        }

        let result: Vec<Value> = matches
            .iter()
            .map(|t| {
                json!({
                    "name": t.name,
                    "description": t.description,
                    "parameters": t.parameters,
                })
            })
            .collect();

        Ok(ToolResult {
            call_id: String::new(),
            content: serde_json::to_string_pretty(&result).unwrap_or_default(),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::registry::ToolRegistry;

    struct DummyTool {
        tool_name: String,
        tool_desc: String,
    }

    #[async_trait]
    impl Tool for DummyTool {
        fn name(&self) -> &str {
            &self.tool_name
        }
        fn description(&self) -> &str {
            &self.tool_desc
        }
        fn parameters(&self) -> Value {
            json!({"type": "object", "properties": {"path": {"type": "string"}}})
        }
        async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
            Ok(ToolResult {
                call_id: String::new(),
                content: "ok".to_string(),
                is_error: false,
            })
        }
    }

    #[tokio::test]
    async fn search_finds_matching_tools() {
        let mut registry = ToolRegistry::new();
        registry.register(DummyTool {
            tool_name: "lint".to_string(),
            tool_desc: "Run linter on code".to_string(),
        });
        registry.register(DummyTool {
            tool_name: "test_runner".to_string(),
            tool_desc: "Run test suite".to_string(),
        });
        registry.register(DummyTool {
            tool_name: "read".to_string(),
            tool_desc: "Read a file".to_string(),
        });

        let tool = ToolSearchTool::new(Arc::new(registry));
        let result = tool.execute(json!({"query": "lint"})).await.unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("lint"));
        assert!(!result.content.contains("test_runner"));
    }

    #[tokio::test]
    async fn search_returns_empty_for_no_match() {
        let registry = ToolRegistry::new();
        let tool = ToolSearchTool::new(Arc::new(registry));
        let result = tool.execute(json!({"query": "nonexistent"})).await.unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("No tools found"));
    }
}
