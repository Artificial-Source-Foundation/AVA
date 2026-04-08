use ava_plugin::{PluginAppEvent, PluginAppResponse, PluginMountRegistration};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tauri::{AppHandle, Emitter, State};

use crate::bridge::DesktopBridge;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct PluginFrontendEvent {
    plugin: String,
    event: String,
    payload: Value,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginHostInvokeArgs {
    pub plugin: String,
    pub command: String,
    #[serde(default)]
    pub payload: Value,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginHostInvokeResult {
    pub result: Value,
    pub emitted_events: Vec<PluginAppEvent>,
}

fn emit_plugin_events(
    app: &AppHandle,
    plugin: &str,
    events: &[PluginAppEvent],
) -> Result<(), String> {
    for event in events {
        app.emit(
            "plugin-event",
            PluginFrontendEvent {
                plugin: plugin.to_string(),
                event: event.event.clone(),
                payload: event.payload.clone(),
            },
        )
        .map_err(|error| format!("failed to emit plugin event: {error}"))?;
    }

    Ok(())
}

#[tauri::command]
pub async fn list_plugin_mounts(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<PluginMountRegistration>, String> {
    let manager = bridge.stack.plugin_manager.lock().await;
    Ok(manager.list_plugin_mounts())
}

#[tauri::command]
pub async fn plugin_host_invoke(
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
    args: PluginHostInvokeArgs,
) -> Result<PluginHostInvokeResult, String> {
    let PluginHostInvokeArgs {
        plugin,
        command,
        payload,
    } = args;

    let handle = {
        let manager = bridge.stack.plugin_manager.lock().await;
        manager.get_app_command_handle(&plugin, &command)
    }
    .map_err(|error| error.to_string())?;

    let response: PluginAppResponse = handle
        .invoke_command(&command, payload)
        .await
        .map_err(|error| error.to_string())?;

    emit_plugin_events(&app, &plugin, &response.emitted_events)?;

    Ok(PluginHostInvokeResult {
        result: response.result,
        emitted_events: response.emitted_events,
    })
}
