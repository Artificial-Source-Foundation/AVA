//! Tauri commands for reading and writing configuration.

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

/// Update LLM settings (provider, model, temperature, max_tokens) in config.yaml.
///
/// Only provided (non-null) fields are updated; omitted fields are left unchanged.
#[tauri::command]
pub async fn update_llm_config(
    provider: Option<String>,
    model: Option<String>,
    temperature: Option<f32>,
    max_tokens: Option<usize>,
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    bridge
        .stack
        .config
        .update(|config| {
            if let Some(ref p) = provider {
                config.llm.provider = p.clone();
            }
            if let Some(ref m) = model {
                config.llm.model = m.clone();
            }
            if let Some(t) = temperature {
                config.llm.temperature = t;
            }
            if let Some(mt) = max_tokens {
                config.llm.max_tokens = mt;
            }
        })
        .await
        .map_err(|e| e.to_string())?;

    bridge.stack.config.save().await.map_err(|e| e.to_string())?;

    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg.llm).map_err(|e| e.to_string())
}

/// Update feature flags in config.yaml.
///
/// Only provided (non-null) fields are updated; omitted fields are left unchanged.
#[tauri::command]
pub async fn update_feature_flags(
    enable_git: Option<bool>,
    enable_lsp: Option<bool>,
    enable_mcp: Option<bool>,
    audit_logging: Option<bool>,
    session_logging: Option<bool>,
    auto_review: Option<bool>,
    enable_codebase_index: Option<bool>,
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    bridge
        .stack
        .config
        .update(|config| {
            if let Some(v) = enable_git {
                config.features.enable_git = v;
            }
            if let Some(v) = enable_lsp {
                config.features.enable_lsp = v;
            }
            if let Some(v) = enable_mcp {
                config.features.enable_mcp = v;
            }
            if let Some(v) = audit_logging {
                config.features.audit_logging = v;
            }
            if let Some(v) = session_logging {
                config.features.session_logging = v;
            }
            if let Some(v) = auto_review {
                config.features.auto_review = v;
            }
            if let Some(v) = enable_codebase_index {
                config.features.enable_codebase_index = v;
            }
        })
        .await
        .map_err(|e| e.to_string())?;

    bridge.stack.config.save().await.map_err(|e| e.to_string())?;

    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg.features).map_err(|e| e.to_string())
}

/// Read the current feature flags from config.yaml.
#[tauri::command]
pub async fn get_feature_flags(
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg.features).map_err(|e| e.to_string())
}
