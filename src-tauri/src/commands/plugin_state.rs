use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use tauri::Manager;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginStateEntry {
    pub installed: bool,
    pub enabled: bool,
}

type PluginStateMap = HashMap<String, PluginStateEntry>;

fn plugin_state_file(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    let base_dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("Failed to resolve app data dir: {e}"))?;

    let ava_dir = base_dir.join("ava");
    fs::create_dir_all(&ava_dir).map_err(|e| format!("Failed to create data directory: {e}"))?;

    Ok(ava_dir.join("plugins-state.json"))
}

#[tauri::command]
pub fn get_plugins_state(app: tauri::AppHandle) -> Result<PluginStateMap, String> {
    let file_path = plugin_state_file(&app)?;

    if !file_path.exists() {
        return Ok(HashMap::new());
    }

    let raw = fs::read_to_string(&file_path).map_err(|e| {
        format!(
            "Failed to read plugin state file '{}': {e}",
            file_path.display()
        )
    })?;

    serde_json::from_str::<PluginStateMap>(&raw).map_err(|e| {
        format!(
            "Failed to parse plugin state JSON '{}': {e}",
            file_path.display()
        )
    })
}

#[tauri::command]
pub fn set_plugins_state(app: tauri::AppHandle, state: PluginStateMap) -> Result<(), String> {
    let file_path = plugin_state_file(&app)?;
    let raw = serde_json::to_string_pretty(&state)
        .map_err(|e| format!("Failed to serialize plugin state JSON: {e}"))?;

    fs::write(&file_path, raw).map_err(|e| {
        format!(
            "Failed to write plugin state file '{}': {e}",
            file_path.display()
        )
    })
}
