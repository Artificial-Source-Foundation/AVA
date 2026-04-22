//! Tauri commands for MCP server management.

use ava_agent_orchestration::stack::McpServerStatus;
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
    pub can_toggle: bool,
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

fn map_mcp_status(status: &McpServerStatus) -> (&'static str, Option<&str>) {
    match status {
        McpServerStatus::Connected => ("connected", None),
        McpServerStatus::Disabled => ("disabled", None),
        McpServerStatus::Failed(error) => ("failed", Some(error.as_str())),
        McpServerStatus::Connecting => ("connecting", None),
    }
}

/// List all MCP servers (active and disabled).
#[tauri::command]
pub async fn list_mcp_servers(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<McpServerInfo>, String> {
    let servers = bridge.stack.mcp_server_info().await;
    Ok(servers
        .into_iter()
        .map(|s| {
            let (status, error) = map_mcp_status(&s.status);
            McpServerInfo {
                name: s.name,
                tool_count: s.tool_count,
                scope: s.scope.to_string(),
                enabled: s.enabled,
                can_toggle: s.can_toggle,
                status: status.to_string(),
                error: error.map(str::to_string),
            }
        })
        .collect())
}

/// Enable a previously disabled MCP server.
#[tauri::command]
pub async fn enable_mcp_server(
    name: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if bridge.stack.mcp_enable_server(&name).await {
        Ok(())
    } else {
        Err(format!(
            "MCP server '{name}' is not known or was not disabled"
        ))
    }
}

/// Disable an MCP server for this session.
#[tauri::command]
pub async fn disable_mcp_server(
    name: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if bridge.stack.mcp_disable_server(&name).await {
        Ok(())
    } else {
        Err(format!("MCP server '{name}' is not known"))
    }
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
    let (server_count, tool_count) = bridge.stack.reload_mcp().await.map_err(|e| e.to_string())?;
    Ok(McpReloadResult {
        server_count,
        tool_count,
    })
}

#[cfg(test)]
mod tests {
    use super::map_mcp_status;
    use ava_agent_orchestration::stack::McpServerStatus;

    #[test]
    fn maps_runtime_statuses_to_frontend_strings() {
        assert_eq!(
            map_mcp_status(&McpServerStatus::Connected),
            ("connected", None)
        );
        assert_eq!(
            map_mcp_status(&McpServerStatus::Disabled),
            ("disabled", None)
        );
        assert_eq!(
            map_mcp_status(&McpServerStatus::Connecting),
            ("connecting", None)
        );
        assert_eq!(
            map_mcp_status(&McpServerStatus::Failed("boom".to_string())),
            ("failed", Some("boom"))
        );
    }
}
