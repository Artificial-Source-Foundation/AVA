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
    pub reasoning: bool,
    pub capabilities: Vec<String>,
    pub context_window: usize,
    pub max_output: Option<usize>,
    pub cost_input: f64,
    pub cost_output: f64,
}

/// List all models from the repo-owned curated catalog.
pub(crate) async fn list_models() -> impl IntoResponse {
    let catalog = ava_config::fallback_catalog();
    let models: Vec<ModelInfo> = catalog
        .all_models()
        .iter()
        .map(|m| {
            let vision = m.id.contains("vision")
                || m.id.contains("gpt-4")
                || m.id.contains("claude")
                || m.provider_id == "google";
            let reasoning = matches!(
                m.ava_provider(),
                "anthropic" | "openai" | "gemini" | "zai" | "kimi" | "minimax"
            ) || m.id.contains("reason")
                || m.id.starts_with("o3")
                || m.id.starts_with("o4")
                || m.id.starts_with("gpt-5");

            ModelInfo {
                id: m.api_model_id(m.ava_provider()),
                provider: m.ava_provider().to_string(),
                name: m.name.clone(),
                tool_call: m.tool_call,
                vision,
                reasoning,
                capabilities: {
                    let mut caps = vec!["tools".to_string()];
                    if vision {
                        caps.push("vision".to_string());
                    }
                    if reasoning {
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
            }
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
    if state.has_active_runs().await {
        return Err(error_response(
            StatusCode::CONFLICT,
            "Cannot switch models while web runs are active.",
        ));
    }

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
// CLI Agents
// ============================================================================

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CLIAgentInfo {
    pub name: String,
    pub binary: String,
    pub version: String,
    pub installed: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub auth: Option<ava_acp::claude::ClaudeAuthStatus>,
}

/// List discovered CLI agents (Claude Code, Gemini CLI, etc.)
pub(crate) async fn list_cli_agents(State(state): State<WebState>) -> impl IntoResponse {
    let agents: Vec<CLIAgentInfo> = state
        .inner
        .stack
        .cli_agents()
        .iter()
        .map(|a| CLIAgentInfo {
            name: a.name.clone(),
            binary: a.binary.clone(),
            version: a.version.clone(),
            installed: true,
            auth: a.auth.clone(),
        })
        .collect();
    Json(agents)
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

#[derive(Deserialize)]
pub struct SetPrimaryAgentProfileRequest {
    pub prompt: Option<String>,
}

/// Apply the active startup primary-agent prompt suffix for future web runs.
pub(crate) async fn set_primary_agent_profile(
    State(state): State<WebState>,
    Json(req): Json<SetPrimaryAgentProfileRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    if state.has_active_runs().await {
        return Err(error_response(
            StatusCode::CONFLICT,
            "Cannot switch primary agents while web runs are active.",
        ));
    }

    state
        .inner
        .stack
        .set_startup_prompt_suffix(req.prompt)
        .await
        .map_err(|e| error_response(StatusCode::BAD_REQUEST, &e.to_string()))?;

    Ok(Json(serde_json::json!({ "ok": true })))
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
    pub can_toggle: bool,
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

fn map_mcp_status(
    status: &ava_agent_orchestration::stack::McpServerStatus,
) -> (&'static str, Option<&str>) {
    match status {
        ava_agent_orchestration::stack::McpServerStatus::Connected => ("connected", None),
        ava_agent_orchestration::stack::McpServerStatus::Disabled => ("disabled", None),
        ava_agent_orchestration::stack::McpServerStatus::Failed(error) => {
            ("failed", Some(error.as_str()))
        }
        ava_agent_orchestration::stack::McpServerStatus::Connecting => ("connecting", None),
    }
}

/// List all configured MCP servers with their connection status and tool count.
pub(crate) async fn list_mcp_servers(State(state): State<WebState>) -> impl IntoResponse {
    let servers = state.inner.stack.mcp_server_info().await;
    let response: Vec<McpServerResponse> = servers
        .into_iter()
        .map(|s| {
            let (status, error) = map_mcp_status(&s.status);
            McpServerResponse {
                name: s.name,
                tool_count: s.tool_count,
                scope: s.scope.to_string(),
                enabled: s.enabled,
                can_toggle: s.can_toggle,
                status: status.to_string(),
                error: error.map(str::to_string),
            }
        })
        .collect();
    Json(response)
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

/// Receive a frontend log entry and append it to the XDG state log dir.
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

    let log_path = ava_config::frontend_log_path()
        .unwrap_or_else(|_| std::path::PathBuf::from("frontend.log"));
    if let Some(log_dir) = log_path.parent() {
        let _ = std::fs::create_dir_all(log_dir);
    }

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
