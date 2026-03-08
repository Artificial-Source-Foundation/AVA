use std::sync::Arc;

use async_trait::async_trait;
use ava_session::SessionManager;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct SessionListTool {
    session_manager: Arc<SessionManager>,
}

impl SessionListTool {
    pub fn new(session_manager: Arc<SessionManager>) -> Self {
        Self { session_manager }
    }
}

#[async_trait]
impl Tool for SessionListTool {
    fn name(&self) -> &str {
        "session_list"
    }

    fn description(&self) -> &str {
        "List recent sessions"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {
                "limit": { "type": "integer", "minimum": 1, "description": "Max sessions to list (default 10)" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let limit = args
            .get("limit")
            .and_then(Value::as_u64)
            .unwrap_or(10) as usize;

        let sessions = self.session_manager.list_recent(limit)?;

        if sessions.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: "No sessions found.".to_string(),
                is_error: false,
            });
        }

        let entries: Vec<String> = sessions
            .iter()
            .map(|s| {
                format!(
                    "- {} ({}msgs, {})",
                    s.id,
                    s.messages.len(),
                    s.updated_at.format("%Y-%m-%d %H:%M")
                )
            })
            .collect();

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("{} sessions:\n{}", entries.len(), entries.join("\n")),
            is_error: false,
        })
    }
}

pub struct SessionLoadTool {
    session_manager: Arc<SessionManager>,
}

impl SessionLoadTool {
    pub fn new(session_manager: Arc<SessionManager>) -> Self {
        Self { session_manager }
    }
}

#[async_trait]
impl Tool for SessionLoadTool {
    fn name(&self) -> &str {
        "session_load"
    }

    fn description(&self) -> &str {
        "Load a past session by ID"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["id"],
            "properties": {
                "id": { "type": "string", "description": "Session UUID" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let id_str = args
            .get("id")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: id".into()))?;

        let uuid = uuid::Uuid::parse_str(id_str)
            .map_err(|e| AvaError::ValidationError(format!("invalid UUID: {e}")))?;

        let session = self.session_manager.get(uuid)?;

        match session {
            Some(s) => {
                let messages: Vec<String> = s
                    .messages
                    .iter()
                    .map(|m| {
                        let content: String = m.content.chars().take(200).collect();
                        let truncated = if m.content.len() > 200 {
                            format!("{content}...")
                        } else {
                            content
                        };
                        format!("[{:?}]: {}", m.role, truncated)
                    })
                    .collect();

                Ok(ToolResult {
                    call_id: String::new(),
                    content: format!(
                        "Session {} ({} messages, created {}):\n{}",
                        s.id,
                        s.messages.len(),
                        s.created_at.format("%Y-%m-%d %H:%M"),
                        messages.join("\n")
                    ),
                    is_error: false,
                })
            }
            None => Ok(ToolResult {
                call_id: String::new(),
                content: format!("Session not found: {id_str}"),
                is_error: false,
            }),
        }
    }
}
