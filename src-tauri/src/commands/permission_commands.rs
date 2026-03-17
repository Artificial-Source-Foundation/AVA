//! Tauri commands for managing the agent's permission level.
//!
//! Mirrors the TUI's `/permissions` command, allowing the desktop frontend
//! to toggle between Standard and AutoApprove modes.

use serde::Serialize;
use tauri::State;

use crate::bridge::DesktopBridge;

/// The two permission levels, serialised for the frontend.
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PermissionLevelInfo {
    /// `"standard"` or `"autoApprove"`
    pub level: String,
}

fn level_label(auto_approve: bool) -> String {
    if auto_approve {
        "autoApprove".to_string()
    } else {
        "standard".to_string()
    }
}

fn parse_level(level: &str) -> Result<bool, String> {
    match level {
        "standard" => Ok(false),
        "autoApprove" | "auto_approve" | "auto-approve" => Ok(true),
        other => Err(format!(
            "Unknown permission level \"{other}\". Expected \"standard\" or \"autoApprove\"."
        )),
    }
}

/// Get the current permission level.
#[tauri::command]
pub async fn get_permission_level(
    bridge: State<'_, DesktopBridge>,
) -> Result<PermissionLevelInfo, String> {
    let auto = bridge.stack.is_auto_approve().await;
    Ok(PermissionLevelInfo {
        level: level_label(auto),
    })
}

/// Set the permission level to the given value.
#[tauri::command]
pub async fn set_permission_level(
    level: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<PermissionLevelInfo, String> {
    let auto = parse_level(&level)?;
    bridge.stack.set_auto_approve(auto).await;
    Ok(PermissionLevelInfo {
        level: level_label(auto),
    })
}

/// Toggle between Standard and AutoApprove, returning the new level.
#[tauri::command]
pub async fn toggle_permission_level(
    bridge: State<'_, DesktopBridge>,
) -> Result<PermissionLevelInfo, String> {
    let current = bridge.stack.is_auto_approve().await;
    let new_auto = !current;
    bridge.stack.set_auto_approve(new_auto).await;
    Ok(PermissionLevelInfo {
        level: level_label(new_auto),
    })
}
