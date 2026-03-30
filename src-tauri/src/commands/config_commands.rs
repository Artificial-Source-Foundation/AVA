//! Tauri commands for reading configuration.

use ava_config::HqAgentOverride;
use tauri::State;

use crate::bridge::DesktopBridge;

/// Get the full config as JSON.
#[tauri::command]
pub async fn get_config(bridge: State<'_, DesktopBridge>) -> Result<serde_json::Value, String> {
    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg).map_err(|e| e.to_string())
}

/// Sync HQ agent overrides from desktop settings into Rust config.
#[tauri::command]
pub async fn sync_hq_agent_overrides(
    overrides: Vec<HqAgentOverride>,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    bridge
        .stack
        .config
        .update(|config| {
            config.hq.agent_overrides = overrides.clone();
        })
        .await
        .map_err(|e| e.to_string())?;
    bridge.stack.config.save().await.map_err(|e| e.to_string())
}
