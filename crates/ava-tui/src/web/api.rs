//! HTTP API handlers for the AVA web server.
//!
//! Each handler maps to a Tauri command equivalent, operating on the shared
//! `WebState` instead of `tauri::State<DesktopBridge>`.

use axum::extract::State;
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tracing::info;

use super::state::WebState;

// ============================================================================
// Health
// ============================================================================

pub async fn health() -> impl IntoResponse {
    Json(serde_json::json!({ "status": "ok", "version": env!("CARGO_PKG_VERSION") }))
}

// ============================================================================
// Agent
// ============================================================================

#[derive(Deserialize)]
pub struct SubmitGoalRequest {
    pub goal: String,
    #[serde(default)]
    pub max_turns: usize,
    #[serde(default)]
    pub provider: Option<String>,
    #[serde(default)]
    pub model: Option<String>,
}

#[derive(Serialize)]
pub struct SubmitGoalResponse {
    pub success: bool,
    pub turns: usize,
    pub session_id: String,
}

/// Start the agent with a goal. Events stream over the WebSocket channel.
pub async fn submit_goal(
    State(state): State<WebState>,
    Json(req): Json<SubmitGoalRequest>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    // Prevent concurrent runs
    {
        let running = state.inner.running.read().await;
        if *running {
            return Err(error_response(
                StatusCode::CONFLICT,
                "Agent is already running. Cancel first.",
            ));
        }
    }
    *state.inner.running.write().await = true;

    // Apply model override if requested
    if let (Some(ref provider), Some(ref model)) = (&req.provider, &req.model) {
        state
            .inner
            .stack
            .switch_model(provider, model)
            .await
            .map_err(|e| error_response(StatusCode::BAD_REQUEST, &e.to_string()))?;
    }

    let cancel = state.new_cancel_token().await;
    let event_broadcast = state.inner.event_tx.clone();

    // Create an mpsc channel for the agent to send events into,
    // then forward those events to the broadcast channel.
    let (tx, mut rx) = mpsc::unbounded_channel();

    let forwarder = tokio::spawn(async move {
        while let Some(event) = rx.recv().await {
            // Best-effort broadcast; if no subscribers are listening, that is fine.
            let _ = event_broadcast.send(event);
        }
    });

    let max_turns = if req.max_turns > 0 { req.max_turns } else { 0 };

    info!(goal = %req.goal, max_turns, "Web: starting agent");

    let result = state
        .inner
        .stack
        .run(
            &req.goal,
            max_turns,
            Some(tx),
            cancel,
            vec![], // no history for now
            None,   // no message queue for now
            vec![], // no images
        )
        .await;

    // Wait for the forwarder to drain
    let _ = forwarder.await;

    *state.inner.running.write().await = false;

    match result {
        Ok(run_result) => {
            let _ = state.inner.stack.session_manager.save(&run_result.session);
            Ok(Json(SubmitGoalResponse {
                success: run_result.success,
                turns: run_result.turns,
                session_id: run_result.session.id.to_string(),
            }))
        }
        Err(e) => Err(error_response(
            StatusCode::INTERNAL_SERVER_ERROR,
            &e.to_string(),
        )),
    }
}

/// Cancel the currently-running agent.
pub async fn cancel_agent(State(state): State<WebState>) -> impl IntoResponse {
    state.cancel().await;
    Json(serde_json::json!({ "cancelled": true }))
}

#[derive(Serialize)]
pub struct AgentStatusResponse {
    pub running: bool,
    pub provider: String,
    pub model: String,
}

/// Get current agent status.
pub async fn agent_status(State(state): State<WebState>) -> impl IntoResponse {
    let running = *state.inner.running.read().await;
    let (provider, model) = state.inner.stack.current_model().await;
    Json(AgentStatusResponse {
        running,
        provider,
        model,
    })
}

// ============================================================================
// Sessions
// ============================================================================

#[derive(Serialize)]
pub struct SessionSummary {
    pub id: String,
    pub title: String,
    pub message_count: usize,
    pub created_at: String,
    pub updated_at: String,
}

/// List recent sessions.
pub async fn list_sessions(
    State(state): State<WebState>,
) -> Result<Json<Vec<SessionSummary>>, (StatusCode, Json<ErrorResponse>)> {
    let sessions = state
        .inner
        .stack
        .session_manager
        .list_recent(50)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    let summaries: Vec<SessionSummary> = sessions
        .iter()
        .map(|s| {
            let title = s
                .metadata
                .get("title")
                .and_then(|v| v.as_str())
                .map(String::from)
                .unwrap_or_else(|| {
                    s.messages
                        .first()
                        .map(|m| {
                            let content = &m.content;
                            if content.len() > 80 {
                                format!("{}...", &content[..77])
                            } else {
                                content.clone()
                            }
                        })
                        .unwrap_or_else(|| "New session".to_string())
                });
            SessionSummary {
                id: s.id.to_string(),
                title,
                message_count: s.messages.len(),
                created_at: s.created_at.to_rfc3339(),
                updated_at: s.updated_at.to_rfc3339(),
            }
        })
        .collect();

    Ok(Json(summaries))
}

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
pub async fn list_models() -> impl IntoResponse {
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

// ============================================================================
// Providers
// ============================================================================

#[derive(Serialize)]
pub struct ProviderInfo {
    pub name: String,
}

/// List providers that have credentials configured.
pub async fn list_providers(State(state): State<WebState>) -> impl IntoResponse {
    let names = state.inner.stack.router.available_providers().await;
    let providers: Vec<ProviderInfo> = names
        .into_iter()
        .map(|name| ProviderInfo { name })
        .collect();
    Json(providers)
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
pub async fn ingest_frontend_log(Json(req): Json<FrontendLogRequest>) -> impl IntoResponse {
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

// ============================================================================
// Error helpers
// ============================================================================

#[derive(Serialize)]
pub struct ErrorResponse {
    pub error: String,
}

fn error_response(status: StatusCode, message: &str) -> (StatusCode, Json<ErrorResponse>) {
    (
        status,
        Json(ErrorResponse {
            error: message.to_string(),
        }),
    )
}
