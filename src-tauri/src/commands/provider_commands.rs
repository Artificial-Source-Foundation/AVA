//! Tauri commands for provider listing.
//!
//! Uses `ModelRouter::available_providers` to return providers that have
//! credentials configured.

use serde::Serialize;
use tauri::State;

use crate::bridge::DesktopBridge;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ProviderInfo {
    pub name: String,
}

/// List providers that have credentials configured.
#[tauri::command]
pub async fn list_providers(bridge: State<'_, DesktopBridge>) -> Result<Vec<ProviderInfo>, String> {
    let names = bridge.stack.router.available_providers().await;
    Ok(names
        .into_iter()
        .map(|name| ProviderInfo { name })
        .collect())
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CLIAgentInfo {
    pub name: String,
    pub binary: String,
    pub version: String,
    pub installed: bool,
}

/// Discover installed CLI agents (Claude Code, Gemini CLI, etc.)
#[tauri::command]
pub async fn discover_cli_agents(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<CLIAgentInfo>, String> {
    Ok(bridge
        .stack
        .cli_agents()
        .iter()
        .map(|agent| CLIAgentInfo {
            name: agent.name.clone(),
            binary: agent.binary.clone(),
            version: agent.version.clone(),
            installed: true,
        })
        .collect())
}
