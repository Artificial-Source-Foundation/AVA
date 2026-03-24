//! Tauri commands for model listing and switching.
//!
//! Uses the compiled-in model registry from `ava-config` and the
//! `AgentStack::switch_model` / `current_model` methods.

use serde::Serialize;
use tauri::State;

use crate::bridge::DesktopBridge;
use ava_config::model_catalog::registry;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ModelInfo {
    pub id: String,
    pub provider: String,
    pub name: String,
    pub tool_call: bool,
    pub vision: bool,
    pub reasoning: bool,
    pub context_window: usize,
    pub cost_input: f64,
    pub cost_output: f64,
}

/// List all models from the compiled-in registry.
#[tauri::command]
pub async fn list_models() -> Result<Vec<ModelInfo>, String> {
    let reg = registry::registry();
    let models: Vec<ModelInfo> = reg
        .models
        .iter()
        .map(|m| ModelInfo {
            id: m.id.clone(),
            provider: m.provider.clone(),
            name: m.name.clone(),
            tool_call: m.capabilities.tool_call,
            vision: m.capabilities.vision,
            reasoning: m.capabilities.reasoning,
            context_window: m.limits.context_window,
            cost_input: m.cost.input_per_million,
            cost_output: m.cost.output_per_million,
        })
        .collect();
    Ok(models)
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CurrentModel {
    pub provider: String,
    pub model: String,
}

/// Get the currently-active provider and model.
#[tauri::command]
pub async fn get_current_model(bridge: State<'_, DesktopBridge>) -> Result<CurrentModel, String> {
    let (provider, model) = bridge.stack.current_model().await;
    Ok(CurrentModel { provider, model })
}

/// Switch the active provider and model.
#[tauri::command]
pub async fn switch_model(
    provider: String,
    model: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    bridge
        .stack
        .switch_model(&provider, &model)
        .await
        .map_err(|e| e.to_string())
}
