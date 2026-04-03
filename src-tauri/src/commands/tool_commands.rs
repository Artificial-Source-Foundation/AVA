//! Tauri commands for querying registered tools.

use serde::Serialize;
use tauri::State;

use crate::bridge::DesktopBridge;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentToolInfo {
    pub name: String,
    pub description: String,
    pub source: String,
}

/// List all tools currently registered in the agent's tool registry.
#[tauri::command]
pub async fn list_agent_tools(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<AgentToolInfo>, String> {
    let registry = bridge.stack.tools.read().await;
    let tools = registry.list_tools_with_source();
    Ok(tools
        .into_iter()
        .map(|(def, source)| AgentToolInfo {
            name: def.name,
            description: def.description,
            source: format!("{:?}", source),
        })
        .collect())
}

#[tauri::command]
pub async fn get_lsp_status(bridge: State<'_, DesktopBridge>) -> Result<serde_json::Value, String> {
    let snapshot = bridge.stack.lsp_snapshot().await;
    serde_json::to_value(snapshot).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn install_lsp_profile(
    bridge: State<'_, DesktopBridge>,
    profile: String,
) -> Result<serde_json::Value, String> {
    let result = bridge.stack.install_lsp_profile(&profile).await?;
    serde_json::to_value(result).map_err(|e| e.to_string())
}
