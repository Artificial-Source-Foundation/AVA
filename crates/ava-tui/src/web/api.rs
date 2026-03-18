//! HTTP API handlers for the AVA web server.
//!
//! Each handler maps to a Tauri command equivalent, operating on the shared
//! `WebState` instead of `tauri::State<DesktopBridge>`.

use axum::extract::{Path, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tokio::sync::mpsc;
use tracing::info;

use super::state::WebState;

// ============================================================================
// Health
// ============================================================================

pub async fn health() -> impl IntoResponse {
    let cwd = std::env::current_dir()
        .ok()
        .and_then(|p| p.to_str().map(String::from))
        .unwrap_or_default();
    Json(serde_json::json!({ "status": "ok", "version": env!("CARGO_PKG_VERSION"), "cwd": cwd }))
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
    /// Optional session ID to continue an existing session.
    #[serde(default)]
    pub session_id: Option<String>,
}

#[derive(Serialize)]
pub struct SubmitGoalResponse {
    /// Matches the frontend's `SubmitGoalResult` interface.
    pub success: bool,
    pub turns: usize,
    #[serde(rename = "sessionId")]
    pub session_id: String,
}

/// Start the agent with a goal asynchronously.
///
/// Returns immediately with the session ID and `"running"` status.
/// Agent events stream over the WebSocket broadcast channel as the agent runs.
/// When the agent finishes, a `Complete` or `Error` event is sent.
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

    // If a session_id was provided, load that session's messages as history
    let (session_id_str, history) =
        if let Some(ref sid) = req.session_id {
            let uuid = uuid::Uuid::parse_str(sid).map_err(|e| {
                error_response(StatusCode::BAD_REQUEST, &format!("Invalid session_id: {e}"))
            })?;
            let session =
                state.inner.stack.session_manager.get(uuid).map_err(|e| {
                    error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string())
                })?;
            let msgs = session.map(|s| s.messages).unwrap_or_default();
            (sid.clone(), msgs)
        } else {
            (uuid::Uuid::new_v4().to_string(), vec![])
        };

    let cancel = state.new_cancel_token().await;
    let inner = state.inner.clone();
    let stack = inner.stack.clone();

    let max_turns = if req.max_turns > 0 { req.max_turns } else { 0 };
    let goal = req.goal.clone();

    info!(goal = %goal, max_turns, "Web: starting agent (async)");

    // Create message queue for mid-stream messaging (3-tier)
    let (msg_queue, msg_queue_tx) = stack.create_message_queue();
    *state.inner.message_queue.write().await = Some(msg_queue_tx);

    // Spawn the agent run in a background task
    tokio::spawn(async move {
        // Create an mpsc channel for the agent to send events into,
        // then forward those events to the broadcast channel.
        let (tx, mut rx) = mpsc::unbounded_channel();
        let event_broadcast = inner.event_tx.clone();

        let forwarder = tokio::spawn(async move {
            while let Some(event) = rx.recv().await {
                let _ = event_broadcast.send(event);
            }
        });

        let result = stack
            .run(
                &goal,
                max_turns,
                Some(tx),
                cancel,
                history,
                Some(msg_queue),
                vec![], // no images
            )
            .await;

        // Wait for the forwarder to drain
        let _ = forwarder.await;

        // Clear the message queue sender and mark as not running
        *inner.message_queue.write().await = None;
        *inner.running.write().await = false;

        match result {
            Ok(run_result) => {
                let _ = stack.session_manager.save(&run_result.session);
            }
            Err(e) => {
                tracing::error!("Agent run failed: {e}");
            }
        }
    });

    Ok(Json(SubmitGoalResponse {
        success: true,
        turns: 0,
        session_id: session_id_str,
    }))
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

// ── Session CRUD ─────────────────────────────────────────────────────────────

#[derive(Deserialize)]
pub struct CreateSessionRequest {
    #[serde(default = "default_session_name")]
    pub name: String,
    /// Optional client-generated session ID. If provided, the server uses this ID.
    #[serde(default)]
    pub id: Option<String>,
}

fn default_session_name() -> String {
    "New Session".to_string()
}

#[derive(Serialize)]
pub struct SessionDetail {
    pub id: String,
    pub title: String,
    pub message_count: usize,
    pub created_at: String,
    pub updated_at: String,
    pub messages: Vec<MessageSummary>,
}

#[derive(Serialize)]
pub struct MessageSummary {
    pub id: String,
    pub role: String,
    pub content: String,
    pub timestamp: String,
}

/// Create a new session.
pub async fn create_session(
    State(state): State<WebState>,
    Json(req): Json<CreateSessionRequest>,
) -> Result<Json<SessionSummary>, (StatusCode, Json<ErrorResponse>)> {
    let mut session = state
        .inner
        .stack
        .session_manager
        .create()
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    // If client provided an ID, use it (web mode sends the client-generated UUID)
    if let Some(ref id_str) = req.id {
        if let Ok(id) = uuid::Uuid::parse_str(id_str) {
            session.id = id;
        }
    }

    // Set the title in metadata
    if let Some(map) = session.metadata.as_object_mut() {
        map.insert(
            "title".to_string(),
            serde_json::Value::String(req.name.clone()),
        );
    }

    state
        .inner
        .stack
        .session_manager
        .save(&session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(SessionSummary {
        id: session.id.to_string(),
        title: req.name,
        message_count: 0,
        created_at: session.created_at.to_rfc3339(),
        updated_at: session.updated_at.to_rfc3339(),
    }))
}

/// Get a session by ID, including its messages.
pub async fn get_session(
    State(state): State<WebState>,
    Path(id): Path<String>,
) -> Result<Json<SessionDetail>, (StatusCode, Json<ErrorResponse>)> {
    let uuid = uuid::Uuid::parse_str(&id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;

    let session = state
        .inner
        .stack
        .session_manager
        .get(uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    let title = session
        .metadata
        .get("title")
        .and_then(|v| v.as_str())
        .map(String::from)
        .unwrap_or_else(|| {
            session
                .messages
                .first()
                .map(|m| {
                    let c = &m.content;
                    if c.len() > 80 {
                        format!("{}...", &c[..77])
                    } else {
                        c.clone()
                    }
                })
                .unwrap_or_else(|| "New session".to_string())
        });

    let messages: Vec<MessageSummary> = session
        .messages
        .iter()
        .map(|m| MessageSummary {
            id: m.id.to_string(),
            role: format!("{:?}", m.role).to_lowercase(),
            content: m.content.clone(),
            timestamp: m.timestamp.to_rfc3339(),
        })
        .collect();

    Ok(Json(SessionDetail {
        id: session.id.to_string(),
        title,
        message_count: session.messages.len(),
        created_at: session.created_at.to_rfc3339(),
        updated_at: session.updated_at.to_rfc3339(),
        messages,
    }))
}

#[derive(Deserialize)]
pub struct RenameSessionRequest {
    pub name: String,
}

/// Rename a session.
pub async fn rename_session(
    State(state): State<WebState>,
    Path(id): Path<String>,
    Json(req): Json<RenameSessionRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let uuid = uuid::Uuid::parse_str(&id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;

    state
        .inner
        .stack
        .session_manager
        .rename(uuid, &req.name)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

/// Delete a session.
pub async fn delete_session(
    State(state): State<WebState>,
    Path(id): Path<String>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let uuid = uuid::Uuid::parse_str(&id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;

    state
        .inner
        .stack
        .session_manager
        .delete(uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(StatusCode::NO_CONTENT)
}

// ── Body-based session operations (for apiInvoke compatibility) ──────────────

#[derive(Deserialize)]
pub struct DeleteSessionBody {
    pub id: String,
}

/// Delete a session (body-based, for frontend apiInvoke compatibility).
pub async fn delete_session_body(
    State(state): State<WebState>,
    Json(req): Json<DeleteSessionBody>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let uuid = uuid::Uuid::parse_str(&req.id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;

    state
        .inner
        .stack
        .session_manager
        .delete(uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct RenameSessionBody {
    pub id: String,
    pub title: String,
}

/// Rename a session (body-based, for frontend apiInvoke compatibility).
pub async fn rename_session_body(
    State(state): State<WebState>,
    Json(req): Json<RenameSessionBody>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let uuid = uuid::Uuid::parse_str(&req.id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;

    state
        .inner
        .stack
        .session_manager
        .rename(uuid, &req.title)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct LoadSessionBody {
    pub id: String,
}

/// Load a session by ID (body-based, for frontend apiInvoke compatibility).
pub async fn load_session_body(
    State(state): State<WebState>,
    Json(req): Json<LoadSessionBody>,
) -> Result<Json<SessionDetail>, (StatusCode, Json<ErrorResponse>)> {
    get_session(State(state), Path(req.id)).await
}

// ── Messages ─────────────────────────────────────────────────────────────────

#[derive(Deserialize)]
pub struct AddMessageRequest {
    pub content: String,
    #[serde(default = "default_role")]
    pub role: String,
}

fn default_role() -> String {
    "user".to_string()
}

/// Add a message to a session (typically a user message before submitting to the agent).
pub async fn add_message(
    State(state): State<WebState>,
    Path(id): Path<String>,
    Json(req): Json<AddMessageRequest>,
) -> Result<Json<MessageSummary>, (StatusCode, Json<ErrorResponse>)> {
    let uuid = uuid::Uuid::parse_str(&id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;

    let role = match req.role.as_str() {
        "user" => ava_types::Role::User,
        "assistant" => ava_types::Role::Assistant,
        "system" => ava_types::Role::System,
        other => {
            return Err(error_response(
                StatusCode::BAD_REQUEST,
                &format!("Invalid role: {other}. Expected user, assistant, or system."),
            ));
        }
    };

    // Load the session, append the message, and save
    let mut session = state
        .inner
        .stack
        .session_manager
        .get(uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    let message = ava_types::Message::new(role, &req.content);
    let summary = MessageSummary {
        id: message.id.to_string(),
        role: req.role.clone(),
        content: message.content.clone(),
        timestamp: message.timestamp.to_rfc3339(),
    };

    session.messages.push(message);
    session.updated_at = chrono::Utc::now();

    state
        .inner
        .stack
        .session_manager
        .save(&session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(summary))
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
// Mid-stream Messaging (3-tier)
// ============================================================================

#[derive(Deserialize)]
pub struct SteerRequest {
    pub message: String,
}

/// Inject a steering message (Tier 1) into the running agent.
pub async fn steer_agent(
    State(state): State<WebState>,
    Json(req): Json<SteerRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let running = *state.inner.running.read().await;
    if !running {
        return Err(error_response(StatusCode::CONFLICT, "Agent is not running"));
    }
    if let Some(ref tx) = *state.inner.message_queue.read().await {
        let _ = tx.send(ava_types::QueuedMessage {
            text: req.message,
            tier: ava_types::MessageTier::Steering,
        });
    }
    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct FollowUpRequest {
    pub message: String,
}

/// Queue a follow-up message (Tier 2) for after the current task.
pub async fn follow_up_agent(
    State(state): State<WebState>,
    Json(req): Json<FollowUpRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let running = *state.inner.running.read().await;
    if !running {
        return Err(error_response(StatusCode::CONFLICT, "Agent is not running"));
    }
    if let Some(ref tx) = *state.inner.message_queue.read().await {
        let _ = tx.send(ava_types::QueuedMessage {
            text: req.message,
            tier: ava_types::MessageTier::FollowUp,
        });
    }
    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct PostCompleteRequest {
    pub message: String,
    #[serde(default = "default_group")]
    pub group: u32,
}

fn default_group() -> u32 {
    1
}

/// Queue a post-complete message (Tier 3) for after the agent stops.
pub async fn post_complete_agent(
    State(state): State<WebState>,
    Json(req): Json<PostCompleteRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let running = *state.inner.running.read().await;
    if !running {
        return Err(error_response(StatusCode::CONFLICT, "Agent is not running"));
    }
    if let Some(ref tx) = *state.inner.message_queue.read().await {
        let _ = tx.send(ava_types::QueuedMessage {
            text: req.message,
            tier: ava_types::MessageTier::PostComplete { group: req.group },
        });
    }
    Ok(Json(serde_json::json!({ "ok": true })))
}

/// Get current message queue state.
pub async fn get_message_queue(State(state): State<WebState>) -> impl IntoResponse {
    let has_queue = state.inner.message_queue.read().await.is_some();
    let running = *state.inner.running.read().await;
    Json(serde_json::json!({ "active": running && has_queue }))
}

/// Clear the message queue.
pub async fn clear_message_queue(State(_state): State<WebState>) -> impl IntoResponse {
    // The message queue is internal to the agent loop; clearing it from the
    // outside requires draining the mpsc channel, which isn't safe.
    // For now, return OK — the queue will be dropped when the agent finishes.
    Json(serde_json::json!({ "ok": true }))
}

// ============================================================================
// Session Detail Sub-resources (stubs for web DB parity)
// ============================================================================

/// List agents for a session (stub — returns empty array).
pub async fn list_session_agents(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List file operations for a session (stub — returns empty array).
pub async fn list_session_files(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List terminal executions for a session (stub — returns empty array).
pub async fn list_session_terminal(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List memory items for a session (stub — returns empty array).
pub async fn list_session_memory(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List checkpoints for a session (stub — returns empty array).
pub async fn list_session_checkpoints(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

// ============================================================================
// WebAgentEvent — frontend-compatible serialization
// ============================================================================

/// Agent events serialized in the format the SolidJS frontend expects.
///
/// The frontend expects `{ "type": "token", "content": "..." }` (tagged enum),
/// while the backend `ava_agent::AgentEvent` serializes as Rust default
/// `{ "Token": "hello" }`. This type mirrors `src-tauri/src/events.rs`.
#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum WebAgentEvent {
    #[serde(rename = "token")]
    Token { content: String },
    #[serde(rename = "thinking")]
    Thinking { content: String },
    #[serde(rename = "tool_call")]
    ToolCall { name: String, args: Value },
    #[serde(rename = "tool_result")]
    ToolResult { content: String, is_error: bool },
    #[serde(rename = "progress")]
    Progress { message: String },
    #[serde(rename = "complete")]
    Complete { session: Value },
    #[serde(rename = "error")]
    Error { message: String },
    #[serde(rename = "token_usage")]
    TokenUsage {
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
    },
    #[serde(rename = "budget_warning")]
    BudgetWarning {
        threshold_percent: u8,
        current_cost_usd: f64,
        max_budget_usd: f64,
    },
    #[serde(rename = "approval_request")]
    ApprovalRequest {
        id: String,
        tool_name: String,
        args: Value,
        risk_level: String,
        reason: String,
        warnings: Vec<String>,
    },
}

/// Convert a backend `AgentEvent` to a frontend-compatible `WebAgentEvent`.
/// Returns `None` for events that have no direct frontend representation.
pub fn convert_agent_event(event: &ava_agent::agent_loop::AgentEvent) -> Option<WebAgentEvent> {
    use ava_agent::agent_loop::AgentEvent as BE;
    match event {
        BE::Token(content) => Some(WebAgentEvent::Token {
            content: content.clone(),
        }),
        BE::Thinking(content) => Some(WebAgentEvent::Thinking {
            content: content.clone(),
        }),
        BE::ToolCall(tc) => Some(WebAgentEvent::ToolCall {
            name: tc.name.clone(),
            args: tc.arguments.clone(),
        }),
        BE::ToolResult(tr) => Some(WebAgentEvent::ToolResult {
            content: tr.content.clone(),
            is_error: tr.is_error,
        }),
        BE::Progress(msg) => Some(WebAgentEvent::Progress {
            message: msg.clone(),
        }),
        BE::Complete(session) => {
            let session_json = serde_json::to_value(session).unwrap_or_default();
            Some(WebAgentEvent::Complete {
                session: session_json,
            })
        }
        BE::Error(msg) => Some(WebAgentEvent::Error {
            message: msg.clone(),
        }),
        BE::TokenUsage {
            input_tokens,
            output_tokens,
            cost_usd,
        } => Some(WebAgentEvent::TokenUsage {
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
        }),
        BE::BudgetWarning {
            threshold_percent,
            current_cost_usd,
            max_budget_usd,
        } => Some(WebAgentEvent::BudgetWarning {
            threshold_percent: *threshold_percent,
            current_cost_usd: *current_cost_usd,
            max_budget_usd: *max_budget_usd,
        }),
        // ToolStats and SubAgentComplete have no direct frontend representation.
        _ => None,
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
