//! TodoWrite and TodoRead tools for agent progress tracking.
//!
//! These tools let the agent maintain a checklist of work items. The todo list
//! uses full-replace semantics: each `todo_write` call replaces the entire list.
//! State is shared via [`TodoState`] so the TUI can display progress.

use async_trait::async_trait;
use ava_types::{TodoItem, TodoPriority, TodoState, TodoStatus, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

/// Tool that writes/replaces the entire todo list.
pub struct TodoWriteTool {
    state: TodoState,
}

impl TodoWriteTool {
    pub fn new(state: TodoState) -> Self {
        Self { state }
    }
}

#[async_trait]
impl Tool for TodoWriteTool {
    fn name(&self) -> &str {
        "todo_write"
    }

    fn description(&self) -> &str {
        "Create or update the agent's todo/progress list. Each call replaces the entire list. \
         Use this to track what you're working on, what's done, and what remains."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["todos"],
            "properties": {
                "todos": {
                    "type": "array",
                    "description": "The complete todo list (replaces any existing list)",
                    "items": {
                        "type": "object",
                        "required": ["content", "status", "priority"],
                        "properties": {
                            "content": {
                                "type": "string",
                                "description": "Description of the task"
                            },
                            "status": {
                                "type": "string",
                                "enum": ["pending", "in_progress", "completed", "cancelled"],
                                "description": "Current status of the task"
                            },
                            "priority": {
                                "type": "string",
                                "enum": ["high", "medium", "low"],
                                "description": "Priority level"
                            }
                        }
                    }
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let todos_val = args.get("todos").ok_or_else(|| {
            ava_types::AvaError::ValidationError("missing required field: todos".into())
        })?;

        let todos_arr = todos_val
            .as_array()
            .ok_or_else(|| ava_types::AvaError::ValidationError("todos must be an array".into()))?;

        let mut items = Vec::with_capacity(todos_arr.len());
        for (i, entry) in todos_arr.iter().enumerate() {
            let content = entry
                .get("content")
                .and_then(Value::as_str)
                .ok_or_else(|| {
                    ava_types::AvaError::ValidationError(format!(
                        "todos[{i}]: missing required field: content"
                    ))
                })?
                .to_string();

            let status_str = entry.get("status").and_then(Value::as_str).ok_or_else(|| {
                ava_types::AvaError::ValidationError(format!(
                    "todos[{i}]: missing required field: status"
                ))
            })?;

            let status = match status_str {
                "pending" => TodoStatus::Pending,
                "in_progress" => TodoStatus::InProgress,
                "completed" => TodoStatus::Completed,
                "cancelled" => TodoStatus::Cancelled,
                other => {
                    return Err(ava_types::AvaError::ValidationError(format!(
                        "todos[{i}]: invalid status '{other}', expected one of: \
                         pending, in_progress, completed, cancelled"
                    )));
                }
            };

            let priority_str = entry
                .get("priority")
                .and_then(Value::as_str)
                .ok_or_else(|| {
                    ava_types::AvaError::ValidationError(format!(
                        "todos[{i}]: missing required field: priority"
                    ))
                })?;

            let priority = match priority_str {
                "high" => TodoPriority::High,
                "medium" => TodoPriority::Medium,
                "low" => TodoPriority::Low,
                other => {
                    return Err(ava_types::AvaError::ValidationError(format!(
                        "todos[{i}]: invalid priority '{other}', expected one of: \
                         high, medium, low"
                    )));
                }
            };

            items.push(TodoItem {
                content,
                status,
                priority,
            });
        }

        self.state.set(items.clone());

        let incomplete = items
            .iter()
            .filter(|t| !matches!(t.status, TodoStatus::Completed | TodoStatus::Cancelled))
            .count();

        let list_json = serde_json::to_string_pretty(&items).unwrap_or_else(|_| "[]".to_string());

        Ok(ToolResult {
            call_id: String::new(),
            content: format!(
                "Updated todo list ({} total, {} incomplete):\n{list_json}",
                items.len(),
                incomplete
            ),
            is_error: false,
        })
    }
}

/// Tool that reads the current todo list.
pub struct TodoReadTool {
    state: TodoState,
}

impl TodoReadTool {
    pub fn new(state: TodoState) -> Self {
        Self { state }
    }
}

#[async_trait]
impl Tool for TodoReadTool {
    fn name(&self) -> &str {
        "todo_read"
    }

    fn description(&self) -> &str {
        "Read the current todo/progress list to see what tasks are pending, in progress, \
         completed, or cancelled."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {}
        })
    }

    async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
        let items = self.state.get();

        if items.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: "No todos. Use todo_write to create a todo list.".to_string(),
                is_error: false,
            });
        }

        let incomplete = items
            .iter()
            .filter(|t| !matches!(t.status, TodoStatus::Completed | TodoStatus::Cancelled))
            .count();

        let list_json = serde_json::to_string_pretty(&items).unwrap_or_else(|_| "[]".to_string());

        Ok(ToolResult {
            call_id: String::new(),
            content: format!(
                "Todo list ({} total, {} incomplete):\n{list_json}",
                items.len(),
                incomplete
            ),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn test_state() -> TodoState {
        TodoState::new()
    }

    #[test]
    fn write_tool_metadata() {
        let state = test_state();
        let tool = TodoWriteTool::new(state);
        assert_eq!(tool.name(), "todo_write");
        assert!(!tool.description().is_empty());
        let params = tool.parameters();
        assert_eq!(params["required"], json!(["todos"]));
    }

    #[test]
    fn read_tool_metadata() {
        let state = test_state();
        let tool = TodoReadTool::new(state);
        assert_eq!(tool.name(), "todo_read");
        assert!(!tool.description().is_empty());
    }

    #[tokio::test]
    async fn write_and_read_roundtrip() {
        let state = test_state();
        let write_tool = TodoWriteTool::new(state.clone());
        let read_tool = TodoReadTool::new(state);

        // Write todos
        let result = write_tool
            .execute(json!({
                "todos": [
                    {"content": "Implement feature", "status": "in_progress", "priority": "high"},
                    {"content": "Write tests", "status": "pending", "priority": "medium"},
                    {"content": "Deploy", "status": "pending", "priority": "low"}
                ]
            }))
            .await
            .unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("3 total"));
        assert!(result.content.contains("3 incomplete"));

        // Read back
        let result = read_tool.execute(json!({})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("Implement feature"));
        assert!(result.content.contains("Write tests"));
    }

    #[tokio::test]
    async fn write_replaces_entire_list() {
        let state = test_state();
        let write_tool = TodoWriteTool::new(state.clone());

        write_tool
            .execute(json!({
                "todos": [
                    {"content": "First", "status": "pending", "priority": "high"}
                ]
            }))
            .await
            .unwrap();

        write_tool
            .execute(json!({
                "todos": [
                    {"content": "Second", "status": "completed", "priority": "low"}
                ]
            }))
            .await
            .unwrap();

        let items = state.get();
        assert_eq!(items.len(), 1);
        assert_eq!(items[0].content, "Second");
        assert_eq!(items[0].status, TodoStatus::Completed);
    }

    #[tokio::test]
    async fn read_empty_list() {
        let state = test_state();
        let read_tool = TodoReadTool::new(state);
        let result = read_tool.execute(json!({})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("No todos"));
    }

    #[tokio::test]
    async fn write_empty_list() {
        let state = test_state();
        let write_tool = TodoWriteTool::new(state.clone());
        let result = write_tool.execute(json!({"todos": []})).await.unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("0 total"));
    }

    #[tokio::test]
    async fn write_missing_todos_field() {
        let state = test_state();
        let tool = TodoWriteTool::new(state);
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn write_invalid_status() {
        let state = test_state();
        let tool = TodoWriteTool::new(state);
        let result = tool
            .execute(json!({
                "todos": [{"content": "x", "status": "unknown", "priority": "high"}]
            }))
            .await;
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("invalid status"));
    }

    #[tokio::test]
    async fn write_invalid_priority() {
        let state = test_state();
        let tool = TodoWriteTool::new(state);
        let result = tool
            .execute(json!({
                "todos": [{"content": "x", "status": "pending", "priority": "urgent"}]
            }))
            .await;
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("invalid priority"));
    }

    #[tokio::test]
    async fn write_counts_incomplete_correctly() {
        let state = test_state();
        let tool = TodoWriteTool::new(state);
        let result = tool
            .execute(json!({
                "todos": [
                    {"content": "Done", "status": "completed", "priority": "low"},
                    {"content": "Active", "status": "in_progress", "priority": "high"},
                    {"content": "Dropped", "status": "cancelled", "priority": "low"},
                    {"content": "Todo", "status": "pending", "priority": "medium"}
                ]
            }))
            .await
            .unwrap();

        assert!(result.content.contains("4 total"));
        assert!(result.content.contains("2 incomplete"));
    }
}
