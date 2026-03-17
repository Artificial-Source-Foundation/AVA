//! Tauri commands for MCP server management.

use serde::Serialize;
use tauri::State;

use crate::bridge::DesktopBridge;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct McpServerInfo {
    pub name: String,
    pub tool_count: usize,
    pub scope: String,
    pub enabled: bool,
}

/// List all MCP servers (active and disabled).
#[tauri::command]
pub async fn list_mcp_servers(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<McpServerInfo>, String> {
    let servers = bridge.stack.mcp_server_info().await;
    Ok(servers
        .into_iter()
        .map(|s| McpServerInfo {
            name: s.name,
            tool_count: s.tool_count,
            scope: format!("{:?}", s.scope),
            enabled: s.enabled,
        })
        .collect())
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct McpReloadResult {
    pub server_count: usize,
    pub tool_count: usize,
}

/// Reload MCP servers (re-reads config and reconnects).
#[tauri::command]
pub async fn reload_mcp_servers(
    bridge: State<'_, DesktopBridge>,
) -> Result<McpReloadResult, String> {
    let (server_count, tool_count) = bridge
        .stack
        .reload_mcp()
        .await
        .map_err(|e| e.to_string())?;
    Ok(McpReloadResult {
        server_count,
        tool_count,
    })
}
