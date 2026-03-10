use std::sync::Arc;

use async_trait::async_trait;
use ava_memory::MemorySystem;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct RememberTool {
    memory: Arc<MemorySystem>,
}

impl RememberTool {
    pub fn new(memory: Arc<MemorySystem>) -> Self {
        Self { memory }
    }
}

#[async_trait]
impl Tool for RememberTool {
    fn name(&self) -> &str {
        "remember"
    }

    fn description(&self) -> &str {
        "Store a key-value pair in persistent memory"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["key", "value"],
            "properties": {
                "key": { "type": "string", "description": "Memory key identifier" },
                "value": { "type": "string", "description": "Value to remember" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let key = args
            .get("key")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: key".into()))?;
        let value = args
            .get("value")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: value".into()))?;

        let memory = self
            .memory
            .remember(key, value)
            .map_err(|e| AvaError::DatabaseError(e.to_string()))?;

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Remembered [{}]: {}", memory.key, memory.value),
            is_error: false,
        })
    }
}

pub struct RecallTool {
    memory: Arc<MemorySystem>,
}

impl RecallTool {
    pub fn new(memory: Arc<MemorySystem>) -> Self {
        Self { memory }
    }
}

#[async_trait]
impl Tool for RecallTool {
    fn name(&self) -> &str {
        "recall"
    }

    fn description(&self) -> &str {
        "Recall a value from persistent memory by key"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["key"],
            "properties": {
                "key": { "type": "string", "description": "Memory key to recall" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let key = args
            .get("key")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: key".into()))?;

        let result = self
            .memory
            .recall(key)
            .map_err(|e| AvaError::DatabaseError(e.to_string()))?;

        let content = match result {
            Some(memory) => format!("[{}]: {}", memory.key, memory.value),
            None => format!("No memory found for key: {key}"),
        };

        Ok(ToolResult {
            call_id: String::new(),
            content,
            is_error: false,
        })
    }
}

pub struct MemorySearchTool {
    memory: Arc<MemorySystem>,
}

impl MemorySearchTool {
    pub fn new(memory: Arc<MemorySystem>) -> Self {
        Self { memory }
    }
}

#[async_trait]
impl Tool for MemorySearchTool {
    fn name(&self) -> &str {
        "memory_search"
    }

    fn description(&self) -> &str {
        "Search persistent memory using full-text search"
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

        let results = self
            .memory
            .search(query)
            .map_err(|e| AvaError::DatabaseError(e.to_string()))?;

        if results.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("No memories matching: {query}"),
                is_error: false,
            });
        }

        let entries: Vec<String> = results
            .into_iter()
            .take(limit)
            .map(|m| format!("- [{}]: {}", m.key, m.value))
            .collect();

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Found {} memories:\n{}", entries.len(), entries.join("\n")),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    /// Returns (TempDir, Arc<MemorySystem>). The TempDir must be kept alive
    /// for the duration of the test to prevent the database from being deleted.
    fn test_memory() -> (tempfile::TempDir, Arc<MemorySystem>) {
        let dir = tempfile::tempdir().unwrap();
        let mem = Arc::new(MemorySystem::new(dir.path().join("test.db")).unwrap());
        (dir, mem)
    }

    #[test]
    fn remember_tool_metadata() {
        let (_dir, mem) = test_memory();
        let tool = RememberTool::new(mem);
        assert_eq!(tool.name(), "remember");
        assert!(!tool.description().is_empty());
        let params = tool.parameters();
        assert_eq!(params["required"], json!(["key", "value"]));
    }

    #[tokio::test]
    async fn remember_stores_and_returns() {
        let (_dir, mem) = test_memory();
        let tool = RememberTool::new(mem);
        let result = tool
            .execute(json!({"key": "project", "value": "AVA"}))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("project"));
        assert!(result.content.contains("AVA"));
    }

    #[tokio::test]
    async fn remember_missing_key_errors() {
        let (_dir, mem) = test_memory();
        let tool = RememberTool::new(mem);
        let result = tool.execute(json!({"value": "no key"})).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn recall_existing_key() {
        let (_dir, mem) = test_memory();
        mem.remember("lang", "Rust").unwrap();
        let tool = RecallTool::new(mem);
        let result = tool.execute(json!({"key": "lang"})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("Rust"));
    }

    #[tokio::test]
    async fn recall_missing_key() {
        let (_dir, mem) = test_memory();
        let tool = RecallTool::new(mem);
        let result = tool.execute(json!({"key": "nonexistent"})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("No memory found"));
    }

    #[tokio::test]
    async fn recall_missing_arg_errors() {
        let (_dir, mem) = test_memory();
        let tool = RecallTool::new(mem);
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn search_finds_matches() {
        let (_dir, mem) = test_memory();
        mem.remember("greeting", "hello world").unwrap();
        mem.remember("farewell", "goodbye world").unwrap();
        let tool = MemorySearchTool::new(mem);
        let result = tool.execute(json!({"query": "world"})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("Found"));
    }

    #[tokio::test]
    async fn search_no_matches() {
        let (_dir, mem) = test_memory();
        mem.remember("key", "value").unwrap();
        let tool = MemorySearchTool::new(mem);
        let result = tool
            .execute(json!({"query": "nonexistent_xyz"}))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("No memories matching"));
    }

    #[tokio::test]
    async fn search_respects_limit() {
        let (_dir, mem) = test_memory();
        for i in 0..5 {
            mem.remember(&format!("item{i}"), &format!("data {i}"))
                .unwrap();
        }
        let tool = MemorySearchTool::new(mem);
        let result = tool
            .execute(json!({"query": "data", "limit": 2}))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("Found 2"));
    }

    #[tokio::test]
    async fn search_missing_query_errors() {
        let (_dir, mem) = test_memory();
        let tool = MemorySearchTool::new(mem);
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }
}
