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
    let tool_call_id = "agent-run-preview";
    emitter.emit_progress(&format!("Streaming with {}", state.database_status()))?;
    emitter.emit_tool_call(tool_call_id, "agent_run", json!({ "goal": goal }))?;
    if goal.contains("error") {
        emitter.emit_error("Full agent loop not yet wired - use CLI")?;
    } else {
        emitter.emit_token("Full agent loop not yet wired - use CLI")?;
    }
    emitter.emit_tool_result(
        tool_call_id,
        "Full agent loop not yet wired - use CLI",
        true,
    )?;
    emitter.emit_complete(json!({
        "completed": false,
        "message": "Full agent loop not yet wired - use CLI"
    }))?;
    Ok(())
}

#[tauri::command]
pub async fn list_tools(state: State<'_, AppState>) -> Result<Vec<ToolInfo>, String> {
    Ok(state.list_tools())
}
