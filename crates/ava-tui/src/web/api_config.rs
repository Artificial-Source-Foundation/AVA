//! Config, model, provider, MCP, plugin, permission, and logging HTTP API handlers.

use axum::extract::{Path, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};

use super::api::{error_response, ErrorResponse};
use super::state::WebState;

// ============================================================================
// Models
// ============================================================================

#[derive(Serialize)]
pub struct ModelInfo {
    pub id: String,
    pub provider: String,
    pub name: String,
    pub tool_call: bool,
    pub vision: bool,
    pub context_window: usize,
    pub cost_input: f64,
    pub cost_output: f64,
}

/// List all models from the compiled-in registry.
pub(crate) async fn list_models() -> impl IntoResponse {
    let reg = ava_config::model_catalog::registry::registry();
    let models: Vec<ModelInfo> = reg
        .models
        .iter()
        .map(|m| ModelInfo {
            id: m.id.clone(),
            provider: m.provider.clone(),
            name: m.name.clone(),
            tool_call: m.capabilities.tool_call,
            vision: m.capabilities.vision,
            context_window: m.limits.context_window,
            cost_input: m.cost.input_per_million,
            cost_output: m.cost.output_per_million,
        })
        .collect();
    Json(models)
}

#[derive(Serialize)]
pub struct CurrentModel {
    pub provider: String,
    pub model: String,
}

/// Get the currently-active provider and model.
pub(crate) async fn get_current_model(State(state): State<WebState>) -> impl IntoResponse {
    let (provider, model) = state.inner.stack.current_model().await;
    Json(CurrentModel { provider, model })
}

#[derive(Deserialize)]
pub struct SwitchModelRequest {
    pub provider: String,
    pub model: String,
}

/// Switch the active provider and model.
pub(crate) async fn switch_model(
    State(state): State<WebState>,
    Json(req): Json<SwitchModelRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    state
        .inner
        .stack
        .switch_model(&req.provider, &req.model)
        .await
        .map_err(|e| error_response(StatusCode::BAD_REQUEST, &e.to_string()))?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

// ============================================================================
// Providers
// ============================================================================

#[derive(Serialize)]
pub struct ProviderInfo {
    pub name: String,
}

/// List providers that have credentials configured.
pub(crate) async fn list_providers(State(state): State<WebState>) -> impl IntoResponse {
    let names = state.inner.stack.router.available_providers().await;
    let providers: Vec<ProviderInfo> = names
        .into_iter()
        .map(|name| ProviderInfo { name })
        .collect();
    Json(providers)
}

// ============================================================================
// Config
// ============================================================================

/// Get the full configuration as JSON.
pub(crate) async fn get_config(
    State(state): State<WebState>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let cfg = state.inner.stack.config.get().await;
    let value = serde_json::to_value(&cfg)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;
    Ok(Json(value))
}

// ============================================================================
// MCP Servers
// ============================================================================

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct McpServerResponse {
    pub name: String,
    pub tool_count: usize,
    pub scope: String,
    pub enabled: bool,
    pub status: String,
}

/// List all configured MCP servers with their connection status and tool count.
pub(crate) async fn list_mcp_servers(State(state): State<WebState>) -> impl IntoResponse {
    let servers = state.inner.stack.mcp_server_info().await;
    let response: Vec<McpServerResponse> = servers
        .into_iter()
        .map(|s| {
            let status = match &s.status {
                ava_agent::stack::McpServerStatus::Connected => "connected",
                ava_agent::stack::McpServerStatus::Disabled => "disabled",
                ava_agent::stack::McpServerStatus::Failed(_) => "failed",
                ava_agent::stack::McpServerStatus::Connecting => "connecting",
            }
            .to_string();
            McpServerResponse {
                name: s.name,
                tool_count: s.tool_count,
                scope: s.scope.to_string(),
                enabled: s.enabled,
                status,
            }
        })
        .collect();
    Json(response)
}

#[derive(Deserialize)]
pub struct McpServerNamePath {
    pub name: String,
}

/// Enable a previously disabled MCP server.
pub(crate) async fn enable_mcp_server(
    State(state): State<WebState>,
    Path(name): Path<String>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let changed = state.inner.stack.mcp_enable_server(&name).await;
    if changed {
        Ok(Json(serde_json::json!({ "ok": true, "name": name })))
    } else {
        Err(error_response(
            StatusCode::NOT_FOUND,
            &format!("MCP server '{name}' is not known or was not disabled"),
        ))
    }
}

/// Disable an MCP server for this session.
pub(crate) async fn disable_mcp_server(
    State(state): State<WebState>,
    Path(name): Path<String>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let changed = state.inner.stack.mcp_disable_server(&name).await;
    if changed {
        Ok(Json(serde_json::json!({ "ok": true, "name": name })))
    } else {
        Err(error_response(
            StatusCode::NOT_FOUND,
            &format!("MCP server '{name}' is not known"),
        ))
    }
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct McpReloadResponse {
    pub server_count: usize,
    pub tool_count: usize,
}

/// Reload MCP servers from config on disk.
pub(crate) async fn reload_mcp(
    State(state): State<WebState>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let (server_count, tool_count) = state
        .inner
        .stack
        .reload_mcp()
        .await
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;
    Ok(Json(McpReloadResponse {
        server_count,
        tool_count,
    }))
}

// ============================================================================
// Plugins
// ============================================================================

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginInfoResponse {
    pub name: String,
    pub version: String,
    pub status: String,
    pub hooks: Vec<String>,
}

/// List all loaded power plugins with their status and hook subscriptions.
pub(crate) async fn list_plugins(State(state): State<WebState>) -> impl IntoResponse {
    let mgr = state.inner.stack.plugin_manager.lock().await;
    let infos = mgr.list_plugins();
    let response: Vec<PluginInfoResponse> = infos
        .into_iter()
        .map(|p| {
            let status = match &p.status {
                ava_plugin::PluginStatus::Running => "running",
                ava_plugin::PluginStatus::Stopped => "stopped",
                ava_plugin::PluginStatus::Failed(_) => "failed",
            }
            .to_string();
            PluginInfoResponse {
                name: p.name,
                version: p.version,
                status,
                hooks: p.hooks,
            }
        })
        .collect();
    Json(response)
}

// ============================================================================
// Permission level
// ============================================================================

#[derive(Serialize)]
pub struct PermissionLevelInfo {
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
pub(crate) async fn get_permission_level(State(state): State<WebState>) -> impl IntoResponse {
    let auto = state.inner.stack.is_auto_approve().await;
    Json(PermissionLevelInfo {
        level: level_label(auto),
    })
}

#[derive(Deserialize)]
pub struct SetPermissionRequest {
    pub level: String,
}

/// Set the permission level.
pub(crate) async fn set_permission_level(
    State(state): State<WebState>,
    Json(req): Json<SetPermissionRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let auto = parse_level(&req.level).map_err(|e| error_response(StatusCode::BAD_REQUEST, &e))?;
    state.inner.stack.set_auto_approve(auto).await;
    Ok(Json(PermissionLevelInfo {
        level: level_label(auto),
    }))
}

/// Toggle between Standard and AutoApprove.
pub(crate) async fn toggle_permission_level(State(state): State<WebState>) -> impl IntoResponse {
    let current = state.inner.stack.is_auto_approve().await;
    let new_auto = !current;
    state.inner.stack.set_auto_approve(new_auto).await;
    Json(PermissionLevelInfo {
        level: level_label(new_auto),
    })
}

// ============================================================================
// Frontend Log Ingestion
// ============================================================================

#[derive(Deserialize)]
pub struct FrontendLogRequest {
    pub level: String,
    pub category: String,
    pub message: String,
    #[serde(default)]
    pub data: Option<serde_json::Value>,
    #[serde(default)]
    pub timestamp: Option<String>,
}

/// Receive a frontend log entry and append it to `~/.ava/logs/frontend.log`.
pub(crate) async fn ingest_frontend_log(Json(req): Json<FrontendLogRequest>) -> impl IntoResponse {
    let timestamp = req
        .timestamp
        .unwrap_or_else(|| chrono::Utc::now().to_rfc3339());
    let level = req.level.to_uppercase();
    let data_str = match &req.data {
        Some(v) if !v.is_null() => format!(" | {v}"),
        _ => String::new(),
    };
    let line = format!(
        "[{timestamp}] [{level}] [{}] {}{data_str}\n",
        req.category, req.message
    );

    let log_dir = dirs::home_dir()
        .unwrap_or_default()
        .join(".ava")
        .join("logs");
    let _ = std::fs::create_dir_all(&log_dir);
    let log_path = log_dir.join("frontend.log");

    // Size-based rotation: if over 1 MB, keep last half
    if let Ok(meta) = std::fs::metadata(&log_path) {
        if meta.len() > 1_024 * 1_024 {
            if let Ok(content) = std::fs::read_to_string(&log_path) {
                let half = content.len() / 2;
                let truncated = if let Some(nl) = content[half..].find('\n') {
                    format!(
                        "--- log truncated at {} ---\n{}",
                        chrono::Utc::now().to_rfc3339(),
                        &content[half + nl + 1..]
                    )
                } else {
                    content[half..].to_string()
                };
                let _ = std::fs::write(&log_path, truncated);
            }
        }
    }

    match std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)
    {
        Ok(mut file) => {
            use std::io::Write;
            let _ = file.write_all(line.as_bytes());
            StatusCode::OK.into_response()
        }
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}
