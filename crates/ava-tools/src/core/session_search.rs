use std::sync::Arc;

use async_trait::async_trait;
use ava_session::SessionManager;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct SessionSearchTool {
    session_manager: Arc<SessionManager>,
}

impl SessionSearchTool {
    pub fn new(session_manager: Arc<SessionManager>) -> Self {
        Self { session_manager }
    }
}

#[async_trait]
impl Tool for SessionSearchTool {
    fn name(&self) -> &str {
        "session_search"
    }

    fn description(&self) -> &str {
        "Search past sessions by content using full-text search"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["query"],
            "properties": {
                "query": { "type": "string", "description": "Search query" },
                "limit": { "type": "integer", "minimum": 1, "description": "Max results (default 5)" }
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
            .unwrap_or(5) as usize;

        let sessions = self.session_manager.search(query)?;

        if sessions.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("No sessions matching: {query}"),
                is_error: false,
            });
        }

        let entries: Vec<String> = sessions
            .into_iter()
            .take(limit)
            .map(|s| {
                let snippet = s
                    .messages
                    .iter()
                    .find(|m| m.content.to_lowercase().contains(&query.to_lowercase()))
                    .map(|m| {
                        let truncated: String = m.content.chars().take(120).collect();
                        if m.content.len() > 120 {
                            format!("{truncated}...")
                        } else {
                            truncated
                        }
                    })
                    .unwrap_or_default();
                format!(
                    "- {} ({}msgs, {}): {}",
                    s.id,
                    s.messages.len(),
                    s.created_at.format("%Y-%m-%d"),
                    snippet
                )
            })
            .collect();

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Found {} sessions:\n{}", entries.len(), entries.join("\n")),
            is_error: false,
        })
    }
}
