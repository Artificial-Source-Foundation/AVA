use serde::Serialize;
use serde_json::{json, Value};
use tauri::{State, Window};
use tracing::info;

use crate::app_state::AppState;
use crate::events::EventEmitter;

#[derive(Clone, Serialize)]
pub struct ToolInfo {
    pub name: String,
    pub description: String,
}

pub struct MockToolRegistry;

impl MockToolRegistry {
    pub fn new() -> Self {
        Self
    }

    pub async fn execute_tool(&self, tool: &str, args: Value) -> Value {
        json!({
            "content": format!("Mock result for {tool} with args {args}"),
            "is_error": false
        })
    }

    pub fn list_tools(&self) -> Vec<ToolInfo> {
        vec![
            ToolInfo {
                name: "read_file".to_string(),
                description: "Read a file".to_string(),
            },
            ToolInfo {
                name: "write_file".to_string(),
                description: "Write a file".to_string(),
            },
            ToolInfo {
                name: "edit".to_string(),
                description: "Edit file content".to_string(),
            },
            ToolInfo {
                name: "bash".to_string(),
                description: "Execute shell commands".to_string(),
            },
        ]
    }
}

pub struct MockAgent {
    runs: usize,
}

impl MockAgent {
    pub fn new() -> Self {
        Self { runs: 0 }
    }

    pub async fn run(&mut self, goal: &str) -> Value {
        self.runs += 1;
        json!({
            "id": format!("mock-session-{}", self.runs),
            "goal": goal,
            "messages": [],
            "completed": true
        })
    }
}

#[tauri::command]
pub async fn execute_tool(
    tool: String,
    args: Value,
    state: State<'_, AppState>,
) -> Result<Value, String> {
    info!(tool = %tool, args = ?args, "execute_tool called");
    Ok(state.execute_tool(&tool, args).await)
}

#[tauri::command]
pub async fn agent_run(goal: String, state: State<'_, AppState>) -> Result<Value, String> {
    info!(goal = %goal, "agent_run called");
    Ok(state.agent_run(&goal).await)
}

#[tauri::command]
pub async fn agent_stream(
    goal: String,
    window: Window,
    state: State<'_, AppState>,
) -> Result<(), String> {
    info!(goal = %goal, "agent_stream called");

    let emitter = EventEmitter::new(window);
    emitter.emit_progress(&format!("Starting stream with {}", state.database_status()))?;
    emitter.emit_tool_call("mock_planner", json!({ "goal": goal }))?;

    if goal.contains("error") {
        emitter.emit_error("Mock streaming error requested by goal")?;
        return Ok(());
    }

    for i in 0..10 {
        emitter.emit_token(&format!("Token {i} "))?;
        tokio::time::sleep(tokio::time::Duration::from_millis(100)).await;
    }

    emitter.emit_tool_result("Mock planner completed", false)?;
    emitter.emit_complete(json!({ "id": "mock-session" }))?;
    Ok(())
}

#[tauri::command]
pub async fn list_tools(state: State<'_, AppState>) -> Result<Vec<ToolInfo>, String> {
    Ok(state.list_tools())
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::{MockAgent, MockToolRegistry};

    #[tokio::test]
    async fn mock_tool_registry_executes_and_lists_tools() {
        let registry = MockToolRegistry::new();
        let result = registry
            .execute_tool("read_file", json!({ "path": "README.md" }))
            .await;

        assert_eq!(result["is_error"], false);
        assert!(result["content"].as_str().is_some());
        assert!(!registry.list_tools().is_empty());
    }

    #[tokio::test]
    async fn mock_agent_returns_incrementing_session_ids() {
        let mut agent = MockAgent::new();
        let first = agent.run("goal 1").await;
        let second = agent.run("goal 2").await;

        assert_eq!(first["id"], "mock-session-1");
        assert_eq!(second["id"], "mock-session-2");
        assert_eq!(second["completed"], true);
    }
}
