//! Tauri commands for model listing and switching.
//!
//! Uses the compiled-in model registry from `ava-config` and the
//! `AgentStack::switch_model` / `current_model` methods.

use serde::Serialize;
use tauri::State;

use crate::bridge::DesktopBridge;
use ava_config::fallback_catalog;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ModelInfo {
    pub id: String,
    pub provider: String,
    pub name: String,
    pub tool_call: bool,
    pub vision: bool,
    pub reasoning: bool,
    pub capabilities: Vec<String>,
    pub context_window: usize,
    pub max_output: Option<usize>,
    pub cost_input: f64,
    pub cost_output: f64,
}

/// List all models from the repo-owned curated catalog.
#[tauri::command]
pub async fn list_models() -> Result<Vec<ModelInfo>, String> {
    let catalog = fallback_catalog();
    let models: Vec<ModelInfo> = catalog
        .all_models()
        .iter()
        .map(|m| ModelInfo {
            id: m.api_model_id(m.ava_provider()),
            provider: m.ava_provider().to_string(),
            name: m.name.clone(),
            tool_call: m.tool_call,
            vision: m.id.contains("vision")
                || m.id.contains("gpt-4")
                || m.id.contains("claude")
                || m.provider_id == "google",
            reasoning: matches!(
                m.ava_provider(),
                "anthropic" | "openai" | "gemini" | "zai" | "kimi" | "minimax"
            ) || m.id.contains("reason")
                || m.id.starts_with("o3")
                || m.id.starts_with("o4")
                || m.id.starts_with("gpt-5"),
            capabilities: {
                let mut caps = vec!["tools".to_string()];
                if m.id.contains("vision")
                    || m.id.contains("gpt-4")
                    || m.id.contains("claude")
                    || m.provider_id == "google"
                {
                    caps.push("vision".to_string());
                }
                if matches!(
                    m.ava_provider(),
                    "anthropic" | "openai" | "gemini" | "zai" | "kimi" | "minimax"
                ) || m.id.contains("reason")
                    || m.id.starts_with("o3")
                    || m.id.starts_with("o4")
                    || m.id.starts_with("gpt-5")
                {
                    caps.push("reasoning".to_string());
                }
                if matches!((m.cost_input, m.cost_output), (Some(0.0), Some(0.0))) {
                    caps.push("free".to_string());
                }
                caps
            },
            context_window: m.context_window.unwrap_or(4096) as usize,
            max_output: m.max_output.map(|v| v as usize),
            cost_input: m.cost_input.unwrap_or(0.0),
            cost_output: m.cost_output.unwrap_or(0.0),
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
