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

fn is_valid_plugin_id(plugin_id: &str) -> bool {
    !plugin_id.trim().is_empty()
        && plugin_id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
}

fn plugin_state_file(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    let base_dir = app
        .path()
        .app_data_dir()
        .map_err(|e| format!("Failed to resolve app data dir: {e}"))?;

    let ava_dir = base_dir.join("ava");
    fs::create_dir_all(&ava_dir).map_err(|e| format!("Failed to create data directory: {e}"))?;

    Ok(ava_dir.join("plugins-state.json"))
}

fn read_plugin_state(file_path: &PathBuf) -> Result<PluginStateMap, String> {
    if !file_path.exists() {
        return Ok(HashMap::new());
    }

    let raw = fs::read_to_string(file_path).map_err(|e| {
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

fn write_plugin_state(file_path: &PathBuf, state: &PluginStateMap) -> Result<(), String> {
    let raw = serde_json::to_string_pretty(state)
        .map_err(|e| format!("Failed to serialize plugin state JSON: {e}"))?;

    fs::write(file_path, raw).map_err(|e| {
        format!(
            "Failed to write plugin state file '{}': {e}",
            file_path.display()
        )
    })
}

#[tauri::command]
pub fn get_plugins_state(app: tauri::AppHandle) -> Result<PluginStateMap, String> {
    let file_path = plugin_state_file(&app)?;
    read_plugin_state(&file_path)
}

#[tauri::command]
pub fn set_plugins_state(app: tauri::AppHandle, state: PluginStateMap) -> Result<(), String> {
    let file_path = plugin_state_file(&app)?;
    write_plugin_state(&file_path, &state)
}

#[tauri::command]
pub fn install_plugin(
    app: tauri::AppHandle,
    plugin_id: String,
) -> Result<PluginStateEntry, String> {
    if !is_valid_plugin_id(&plugin_id) {
        return Err(format!("Invalid plugin id: '{plugin_id}'"));
    }

    let file_path = plugin_state_file(&app)?;
    let mut state = read_plugin_state(&file_path)?;
    let next = PluginStateEntry {
        installed: true,
        enabled: true,
    };

    state.insert(plugin_id, next.clone());
    write_plugin_state(&file_path, &state)?;

    Ok(next)
}

#[tauri::command]
pub fn uninstall_plugin(
    app: tauri::AppHandle,
    plugin_id: String,
) -> Result<PluginStateEntry, String> {
    if !is_valid_plugin_id(&plugin_id) {
        return Err(format!("Invalid plugin id: '{plugin_id}'"));
    }

    let file_path = plugin_state_file(&app)?;
    let mut state = read_plugin_state(&file_path)?;

    if !state
        .get(&plugin_id)
        .map(|entry| entry.installed)
        .unwrap_or(false)
    {
        return Err(format!("Plugin '{plugin_id}' is not installed"));
    }

    let next = PluginStateEntry {
        installed: false,
        enabled: false,
    };

    state.remove(&plugin_id);
    write_plugin_state(&file_path, &state)?;

    Ok(next)
}

#[tauri::command]
pub fn set_plugin_enabled(
    app: tauri::AppHandle,
    plugin_id: String,
    enabled: bool,
) -> Result<PluginStateEntry, String> {
    if !is_valid_plugin_id(&plugin_id) {
        return Err(format!("Invalid plugin id: '{plugin_id}'"));
    }

    let file_path = plugin_state_file(&app)?;
    let mut state = read_plugin_state(&file_path)?;

    let current = state.get(&plugin_id).cloned().unwrap_or(PluginStateEntry {
        installed: false,
        enabled: false,
    });

    if !current.installed {
        return Err(format!(
            "Plugin '{plugin_id}' must be installed before enable/disable"
        ));
    }

    let next = PluginStateEntry {
        installed: true,
        enabled,
    };

    state.insert(plugin_id, next.clone());
    write_plugin_state(&file_path, &state)?;

    Ok(next)
}
