//! Tauri commands for desktop app auto-updates.

use serde::Serialize;
use tauri_plugin_updater::UpdaterExt;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DesktopUpdateInfo {
    pub available: bool,
    pub current_version: String,
    pub latest_version: Option<String>,
    pub changelog: Option<String>,
}

/// Check if a desktop app update is available.
#[tauri::command]
pub async fn check_desktop_update(app: tauri::AppHandle) -> Result<DesktopUpdateInfo, String> {
    let current = app.package_info().version.to_string();

    match app
        .updater_builder()
        .build()
        .map_err(|e| e.to_string())?
        .check()
        .await
    {
        Ok(Some(update)) => Ok(DesktopUpdateInfo {
            available: true,
            current_version: current,
            latest_version: Some(update.version.clone()),
            changelog: update.body.clone(),
        }),
        Ok(None) => Ok(DesktopUpdateInfo {
            available: false,
            current_version: current,
            latest_version: None,
            changelog: None,
        }),
        Err(e) => {
            // Updater not configured yet (no pubkey) — return gracefully
            Ok(DesktopUpdateInfo {
                available: false,
                current_version: current,
                latest_version: None,
                changelog: Some(format!("Update check unavailable: {e}")),
            })
        }
    }
}

/// Download and install a pending desktop app update.
#[tauri::command]
pub async fn install_desktop_update(app: tauri::AppHandle) -> Result<(), String> {
    let update = app
        .updater_builder()
        .build()
        .map_err(|e| format!("Updater init failed: {e}"))?
        .check()
        .await
        .map_err(|e| format!("Update check failed: {e}"))?
        .ok_or_else(|| "No update available".to_string())?;

    update
        .download_and_install(|_, _| {}, || {})
        .await
        .map_err(|e| format!("Install failed: {e}"))
}
