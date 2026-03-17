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
pub async fn list_providers(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<ProviderInfo>, String> {
    let names = bridge.stack.router.available_providers().await;
    Ok(names
        .into_iter()
        .map(|name| ProviderInfo { name })
        .collect())
}
