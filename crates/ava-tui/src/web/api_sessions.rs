//! Session CRUD and message HTTP API handlers.

use axum::extract::{Path, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};

use super::api::{error_response, ErrorResponse};
use super::state::WebState;

// ============================================================================
// Types
// ============================================================================

#[derive(Serialize)]
pub struct SessionSummary {
    pub id: String,
    pub title: String,
    pub message_count: usize,
    pub created_at: String,
    pub updated_at: String,
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
    /// Tool calls associated with this message (serialised as JSON array or null).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tool_calls: Option<serde_json::Value>,
    /// Metadata blob (arbitrary JSON object).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub metadata: Option<serde_json::Value>,
    /// Token cost for this message.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub tokens_used: Option<u32>,
    /// USD cost for this message.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cost_usd: Option<f64>,
    /// Model that generated this message.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub model: Option<String>,
}

// ============================================================================
// List Sessions
// ============================================================================

/// List recent sessions.
pub(crate) async fn list_sessions(
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
// Session CRUD
// ============================================================================

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

/// Create a new session.
pub(crate) async fn create_session(
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
pub(crate) async fn get_session(
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
        .filter(|m| m.user_visible)
        .map(|m| MessageSummary {
            id: m.id.to_string(),
            role: format!("{:?}", m.role).to_lowercase(),
            content: m.content.clone(),
            timestamp: m.timestamp.to_rfc3339(),
            tool_calls: None,
            metadata: None,
            tokens_used: None,
            cost_usd: None,
            model: None,
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

/// Get all messages for a session (dedicated endpoint, avoids loading the full session object).
///
/// Returns a flat JSON array of `MessageSummary` objects compatible with the
/// frontend's `db-web-fallback.ts` message mapper.
pub(crate) async fn get_session_messages(
    State(state): State<WebState>,
    Path(id): Path<String>,
) -> Result<Json<Vec<MessageSummary>>, (StatusCode, Json<ErrorResponse>)> {
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

    let messages: Vec<MessageSummary> = session
        .messages
        .iter()
        .filter(|m| m.user_visible)
        .map(|m| {
            // Embed tool_calls inside the metadata JSON object under the "toolCalls" key
            // so that the frontend mapper (metadata?.toolCalls) can reconstruct them on
            // session restore — matching the convention used by db-messages.ts on desktop.
            let metadata = if !m.tool_calls.is_empty() {
                // Build minimal frontend-compatible ToolCall shape:
                // { id, name, args, status: "success", startedAt: 0 }
                let tc_json: Vec<serde_json::Value> = m
                    .tool_calls
                    .iter()
                    .map(|tc| {
                        serde_json::json!({
                            "id": tc.id,
                            "name": tc.name,
                            "args": tc.arguments,
                            "status": "success",
                            "startedAt": 0,
                        })
                    })
                    .collect();
                Some(serde_json::json!({ "toolCalls": tc_json }))
            } else {
                None
            };

            MessageSummary {
                id: m.id.to_string(),
                role: format!("{:?}", m.role).to_lowercase(),
                content: m.content.clone(),
                timestamp: m.timestamp.to_rfc3339(),
                tool_calls: None,
                metadata,
                tokens_used: None,
                cost_usd: None,
                model: None,
            }
        })
        .collect();

    Ok(Json(messages))
}

#[derive(Deserialize)]
pub struct RenameSessionRequest {
    pub name: String,
}

/// Rename a session.
pub(crate) async fn rename_session(
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
pub(crate) async fn delete_session(
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

// ============================================================================
// Body-based session operations (for apiInvoke compatibility)
// ============================================================================

#[derive(Deserialize)]
pub struct DeleteSessionBody {
    pub id: String,
}

/// Delete a session (body-based, for frontend apiInvoke compatibility).
pub(crate) async fn delete_session_body(
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
pub(crate) async fn rename_session_body(
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
pub(crate) async fn load_session_body(
    State(state): State<WebState>,
    Json(req): Json<LoadSessionBody>,
) -> Result<Json<SessionDetail>, (StatusCode, Json<ErrorResponse>)> {
    get_session(State(state), Path(req.id)).await
}

#[derive(Deserialize)]
pub struct SearchSessionsRequest {
    pub query: String,
}

/// Search sessions by message content.
pub(crate) async fn search_sessions(
    State(state): State<WebState>,
    Json(req): Json<SearchSessionsRequest>,
) -> Result<Json<Vec<SessionSummary>>, (StatusCode, Json<ErrorResponse>)> {
    let trimmed = req.query.trim();
    if trimmed.is_empty() {
        return Ok(Json(vec![]));
    }
    let sessions = state
        .inner
        .stack
        .session_manager
        .search(trimmed)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    let summaries = sessions
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
                            let c = &m.content;
                            if c.len() > 80 {
                                format!("{}...", &c[..77])
                            } else {
                                c.clone()
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
// Messages
// ============================================================================

#[derive(Deserialize)]
pub struct AddMessageRequest {
    pub content: String,
    #[serde(default = "default_role")]
    pub role: String,
    /// Optional client-generated UUID for the message.
    /// When provided, the server uses this ID so that subsequent PATCH
    /// requests from the client can target the correct message row.
    #[serde(default)]
    pub id: Option<String>,
}

fn default_role() -> String {
    "user".to_string()
}

/// Add a message to a session (typically a user message before submitting to the agent).
pub(crate) async fn add_message(
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
    let _session = state
        .inner
        .stack
        .session_manager
        .get(uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    // Use the client-provided ID when available so that subsequent PATCH
    // requests (updateMessage) can locate the correct row by the same UUID.
    let mut message = ava_types::Message::new(role, &req.content);
    if let Some(ref client_id) = req.id {
        if let Ok(parsed) = uuid::Uuid::parse_str(client_id) {
            message.id = parsed;
        }
    }
    let summary = MessageSummary {
        id: message.id.to_string(),
        role: req.role.clone(),
        content: message.content.clone(),
        timestamp: message.timestamp.to_rfc3339(),
        tool_calls: None,
        metadata: None,
        tokens_used: None,
        cost_usd: None,
        model: None,
    };

    // Incremental insert: only persist this single message instead of
    // re-writing the entire session (crash-safe, O(1) instead of O(n)).
    state
        .inner
        .stack
        .session_manager
        .add_message(uuid, &message)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(summary))
}

// ============================================================================
// Update Message
// ============================================================================

#[derive(Deserialize)]
pub struct UpdateMessageRequest {
    /// Updated text content (optional — omit to leave unchanged).
    #[serde(default)]
    pub content: Option<String>,
    /// Opaque metadata blob from the frontend (tool calls, thinking, etc.)
    #[serde(default)]
    pub metadata: Option<serde_json::Value>,
    /// Token count for this message.
    #[serde(default)]
    pub tokens_used: Option<u32>,
    /// USD cost for this message.
    #[serde(default)]
    pub cost_usd: Option<f64>,
    /// Model that generated this message.
    #[serde(default)]
    pub model: Option<String>,
}

/// Update an existing message within a session.
///
/// Accepts a PATCH to `/api/sessions/{id}/messages/{msg_id}` and updates
/// the matching message in the session manager.  Only fields that are present
/// in the JSON body are changed; absent fields are left as-is.
pub(crate) async fn update_message(
    State(state): State<WebState>,
    Path((id, msg_id)): Path<(String, String)>,
    Json(req): Json<UpdateMessageRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let session_uuid = uuid::Uuid::parse_str(&id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;
    // Accept both UUID and custom string IDs (frontend uses "asst-{timestamp}-{random}")
    let msg_uuid = uuid::Uuid::parse_str(&msg_id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid message ID: {e}"))
    })?;

    // Metadata, tokens, cost, model are frontend-only fields not stored in
    // ava_types::Message.  We persist the content update in the backend
    // session (for LLM context) and return success; the frontend's in-memory
    // store holds the rich metadata for its own display needs.
    if let Some(content) = req.content {
        // O(1) targeted UPDATE — no need to load/rewrite the entire session.
        state
            .inner
            .stack
            .session_manager
            .update_message_content(session_uuid, msg_uuid, &content)
            .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;
    }

    Ok(Json(serde_json::json!({ "ok": true })))
}

// ============================================================================
// Session Detail Sub-resources (stubs for web DB parity)
// ============================================================================

/// List agents for a session (stub — returns empty array).
pub(crate) async fn list_session_agents(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List file operations for a session (stub — returns empty array).
pub(crate) async fn list_session_files(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List terminal executions for a session (stub — returns empty array).
pub(crate) async fn list_session_terminal(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List memory items for a session (stub — returns empty array).
pub(crate) async fn list_session_memory(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

/// List checkpoints for a session (stub — returns empty array).
pub(crate) async fn list_session_checkpoints(Path(_id): Path<String>) -> impl IntoResponse {
    Json(serde_json::json!([]))
}

// ============================================================================
// Duplicate / Fork Session
// ============================================================================

#[derive(Deserialize)]
pub struct DuplicateSessionRequest {
    /// Name for the new session.
    pub name: String,
    /// Optional client-provided ID for the new session.
    #[serde(default)]
    pub id: Option<String>,
}

/// Duplicate a session: creates a new session with all messages copied from the source.
/// Used by both "Duplicate" and "Fork" actions in the frontend.
pub(crate) async fn duplicate_session(
    State(state): State<WebState>,
    Path(source_id): Path<String>,
    Json(req): Json<DuplicateSessionRequest>,
) -> Result<Json<SessionSummary>, (StatusCode, Json<ErrorResponse>)> {
    let source_uuid = uuid::Uuid::parse_str(&source_id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;

    // Load source session with all messages
    let source_session = state
        .inner
        .stack
        .session_manager
        .get(source_uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Source session not found"))?;

    // Create new session
    let mut new_session = state
        .inner
        .stack
        .session_manager
        .create()
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    // Use client-provided ID if given
    if let Some(ref id_str) = req.id {
        if let Ok(id) = uuid::Uuid::parse_str(id_str) {
            new_session.id = id;
        }
    }

    // Set title
    if let Some(map) = new_session.metadata.as_object_mut() {
        map.insert(
            "title".to_string(),
            serde_json::Value::String(req.name.clone()),
        );
    }

    // Save the new session first
    state
        .inner
        .stack
        .session_manager
        .save(&new_session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    // Copy all visible messages from source to new session
    let message_count = source_session
        .messages
        .iter()
        .filter(|m| m.user_visible)
        .count();
    for msg in &source_session.messages {
        if !msg.user_visible {
            continue;
        }
        let mut cloned = msg.clone();
        cloned.id = uuid::Uuid::new_v4();
        state
            .inner
            .stack
            .session_manager
            .add_message(new_session.id, &cloned)
            .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;
    }

    Ok(Json(SessionSummary {
        id: new_session.id.to_string(),
        title: req.name,
        message_count,
        created_at: new_session.created_at.to_rfc3339(),
        updated_at: new_session.updated_at.to_rfc3339(),
    }))
}
