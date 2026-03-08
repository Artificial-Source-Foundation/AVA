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
