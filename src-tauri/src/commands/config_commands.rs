//! Tauri commands for reading configuration.

use tauri::State;

use crate::bridge::DesktopBridge;

/// Get the full config as JSON.
#[tauri::command]
pub async fn get_config(
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg).map_err(|e| e.to_string())
}
