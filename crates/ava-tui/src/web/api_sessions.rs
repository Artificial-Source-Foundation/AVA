//! Session CRUD and message HTTP API handlers.

use axum::extract::{Path, Query, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use chrono::Utc;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use tracing::{debug, warn};

use super::api::{error_response, ErrorResponse};
use super::state::WebState;

// ============================================================================
// Types
// ============================================================================

#[derive(Serialize)]
pub struct SessionSummary {
    pub id: String,
    pub title: String,
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub parent_session_id: Option<String>,
    pub message_count: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_preview: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Serialize)]
pub struct SessionDetail {
    pub id: String,
    pub title: String,
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub parent_session_id: Option<String>,
    pub message_count: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_preview: Option<String>,
    pub created_at: String,
    pub updated_at: String,
    pub messages: Vec<MessageSummary>,
}

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "lowercase")]
enum SessionCloneKind {
    #[default]
    Duplicate,
    Fork,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum PersistedSessionStatus {
    Active,
    Archived,
}

impl PersistedSessionStatus {
    fn as_str(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Archived => "archived",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum SessionListFilter {
    Active,
    Archived,
    All,
}

impl SessionListFilter {
    fn matches(self, status: PersistedSessionStatus) -> bool {
        match self {
            Self::Active => status == PersistedSessionStatus::Active,
            Self::Archived => status == PersistedSessionStatus::Archived,
            Self::All => true,
        }
    }
}

#[derive(Default, Deserialize)]
pub struct ListSessionsQuery {
    #[serde(default)]
    pub status: Option<String>,
}

fn session_status(session: &ava_types::Session) -> PersistedSessionStatus {
    match session.metadata.get("status").and_then(Value::as_str) {
        Some("archived") => PersistedSessionStatus::Archived,
        _ => PersistedSessionStatus::Active,
    }
}

fn set_session_status(session: &mut ava_types::Session, status: PersistedSessionStatus) {
    if let Some(metadata) = session.metadata.as_object_mut() {
        metadata.insert(
            "status".to_string(),
            Value::String(status.as_str().to_string()),
        );
    } else {
        let mut metadata = Map::new();
        metadata.insert(
            "status".to_string(),
            Value::String(status.as_str().to_string()),
        );
        session.metadata = Value::Object(metadata);
    }
}

fn parse_list_filter(
    status: Option<&str>,
) -> Result<SessionListFilter, (StatusCode, Json<ErrorResponse>)> {
    match status {
        None | Some("active") => Ok(SessionListFilter::Active),
        Some("archived") => Ok(SessionListFilter::Archived),
        Some("all") => Ok(SessionListFilter::All),
        Some(other) => Err(error_response(
            StatusCode::BAD_REQUEST,
            &format!("Invalid session status filter: {other}. Expected active, archived, or all."),
        )),
    }
}

fn session_title(session: &ava_types::Session) -> String {
    session
        .metadata
        .get("title")
        .and_then(|v| v.as_str())
        .map(String::from)
        .unwrap_or_else(|| {
            session
                .messages
                .first()
                .map(|message| {
                    let content = &message.content;
                    if content.len() > 80 {
                        format!("{}...", &content[..77])
                    } else {
                        content.clone()
                    }
                })
                .unwrap_or_else(|| "New session".to_string())
        })
}

fn session_parent_session_id(session: &ava_types::Session) -> Option<String> {
    session
        .metadata
        .get("parentSessionId")
        .or_else(|| session.metadata.get("parent_session_id"))
        .and_then(Value::as_str)
        .map(str::to_string)
}

fn set_session_parent_session_id(
    session: &mut ava_types::Session,
    parent_session_id: Option<String>,
) {
    let metadata = session
        .metadata
        .as_object_mut()
        .expect("session metadata object");
    match parent_session_id {
        Some(parent_id) => {
            metadata.insert("parentSessionId".to_string(), Value::String(parent_id));
        }
        None => {
            metadata.remove("parentSessionId");
            metadata.remove("parent_session_id");
        }
    }
}

fn session_last_preview(session: &ava_types::Session) -> Option<String> {
    session
        .messages
        .iter()
        .rev()
        .find(|message| message.user_visible)
        .map(|message| message.content.chars().take(100).collect())
}

fn visible_message_count(session: &ava_types::Session) -> usize {
    session
        .messages
        .iter()
        .filter(|message| message.user_visible)
        .count()
}

fn summarize_session(session: &ava_types::Session) -> SessionSummary {
    SessionSummary {
        id: session.id.to_string(),
        title: session_title(session),
        status: session_status(session).as_str().to_string(),
        parent_session_id: session_parent_session_id(session),
        message_count: visible_message_count(session),
        last_preview: session_last_preview(session),
        created_at: session.created_at.to_rfc3339(),
        updated_at: session.updated_at.to_rfc3339(),
    }
}

fn parse_session_uuid(id: &str) -> Result<uuid::Uuid, (StatusCode, Json<ErrorResponse>)> {
    uuid::Uuid::parse_str(id)
        .map_err(|e| error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}")))
}

fn load_session_by_uuid(
    state: &WebState,
    uuid: uuid::Uuid,
) -> Result<ava_types::Session, (StatusCode, Json<ErrorResponse>)> {
    state
        .inner
        .stack
        .session_manager
        .get(uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))
}

fn persist_session_status(
    state: &WebState,
    uuid: uuid::Uuid,
    status: PersistedSessionStatus,
) -> Result<(), (StatusCode, Json<ErrorResponse>)> {
    let mut session = load_session_by_uuid(state, uuid)?;
    set_session_status(&mut session, status);
    session.updated_at = Utc::now();
    state
        .inner
        .stack
        .session_manager
        .save(&session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))
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

fn tool_calls_value(message: &ava_types::Message) -> Option<Value> {
    if let Some(tool_calls) = message.metadata.get("toolCalls") {
        if tool_calls.is_array() {
            return Some(tool_calls.clone());
        }
    }

    if message.tool_calls.is_empty() {
        return None;
    }

    Some(Value::Array(
        message
            .tool_calls
            .iter()
            .map(|tool_call| {
                serde_json::json!({
                    "id": tool_call.id,
                    "name": tool_call.name,
                    "arguments": tool_call.arguments,
                    "args": tool_call.arguments,
                    "status": "success",
                    "startedAt": 0,
                })
            })
            .collect(),
    ))
}

fn merged_message_metadata(message: &ava_types::Message) -> Option<Value> {
    let mut metadata = message
        .metadata
        .as_object()
        .cloned()
        .unwrap_or_else(Map::new);

    if !metadata.contains_key("toolCalls") {
        if let Some(tool_calls) = tool_calls_value(message) {
            metadata.insert("toolCalls".to_string(), tool_calls);
        }
    }

    if metadata.is_empty() {
        None
    } else {
        Some(Value::Object(metadata))
    }
}

fn metadata_u32(metadata: Option<&Value>, key: &str) -> Option<u32> {
    metadata
        .and_then(Value::as_object)
        .and_then(|map| map.get(key))
        .and_then(Value::as_u64)
        .and_then(|value| u32::try_from(value).ok())
}

fn metadata_f64(metadata: Option<&Value>, key: &str) -> Option<f64> {
    metadata
        .and_then(Value::as_object)
        .and_then(|map| map.get(key))
        .and_then(Value::as_f64)
}

fn metadata_string(metadata: Option<&Value>, key: &str) -> Option<String> {
    metadata
        .and_then(Value::as_object)
        .and_then(|map| map.get(key))
        .and_then(Value::as_str)
        .map(str::to_string)
}

fn summarize_message(message: &ava_types::Message) -> MessageSummary {
    let metadata = merged_message_metadata(message);

    MessageSummary {
        id: message.id.to_string(),
        role: format!("{:?}", message.role).to_lowercase(),
        content: message.content.clone(),
        timestamp: message.timestamp.to_rfc3339(),
        tool_calls: tool_calls_value(message),
        tokens_used: metadata_u32(metadata.as_ref(), "tokens_used")
            .or_else(|| metadata_u32(metadata.as_ref(), "tokensUsed")),
        cost_usd: metadata_f64(metadata.as_ref(), "cost_usd")
            .or_else(|| metadata_f64(metadata.as_ref(), "costUSD")),
        model: metadata_string(metadata.as_ref(), "model"),
        metadata,
    }
}

fn message_tool_calls_from_metadata(metadata: &Value) -> Vec<ava_types::ToolCall> {
    metadata
        .as_object()
        .and_then(|map| map.get("toolCalls"))
        .and_then(Value::as_array)
        .map(|tool_calls| {
            tool_calls
                .iter()
                .filter_map(|tool_call| {
                    let record = tool_call.as_object()?;
                    let id = record.get("id")?.as_str()?.to_string();
                    let name = record.get("name")?.as_str()?.to_string();
                    let arguments = record
                        .get("arguments")
                        .or_else(|| record.get("args"))
                        .cloned()
                        .unwrap_or_else(|| Value::Object(Map::new()));

                    Some(ava_types::ToolCall {
                        id,
                        name,
                        arguments,
                    })
                })
                .collect()
        })
        .unwrap_or_default()
}

// ============================================================================
// List Sessions
// ============================================================================

/// List recent sessions.
pub(crate) async fn list_sessions(
    State(state): State<WebState>,
    Query(query): Query<ListSessionsQuery>,
) -> Result<Json<Vec<SessionSummary>>, (StatusCode, Json<ErrorResponse>)> {
    let filter = parse_list_filter(query.status.as_deref())?;
    let sessions = state
        .inner
        .stack
        .session_manager
        .list_recent(i64::MAX as usize)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    let summaries: Vec<SessionSummary> = sessions
        .iter()
        .filter(|session| filter.matches(session_status(session)))
        .take(50)
        .map(summarize_session)
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
        map.insert(
            "status".to_string(),
            serde_json::Value::String(PersistedSessionStatus::Active.as_str().to_string()),
        );
    }

    state
        .inner
        .stack
        .session_manager
        .save(&session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    debug!(session_id = %session.id, title = %req.name, "session created");
    Ok(Json(SessionSummary {
        id: session.id.to_string(),
        title: req.name,
        status: PersistedSessionStatus::Active.as_str().to_string(),
        parent_session_id: None,
        message_count: 0,
        last_preview: None,
        created_at: session.created_at.to_rfc3339(),
        updated_at: session.updated_at.to_rfc3339(),
    }))
}

/// Get a session by ID, including its messages.
pub(crate) async fn get_session(
    State(state): State<WebState>,
    Path(id): Path<String>,
) -> Result<Json<SessionDetail>, (StatusCode, Json<ErrorResponse>)> {
    let uuid = parse_session_uuid(&id)?;

    let session = load_session_by_uuid(&state, uuid)?;

    let messages: Vec<MessageSummary> = session
        .messages
        .iter()
        .filter(|m| m.user_visible)
        .map(summarize_message)
        .collect();

    Ok(Json(SessionDetail {
        id: session.id.to_string(),
        title: session_title(&session),
        status: session_status(&session).as_str().to_string(),
        parent_session_id: session_parent_session_id(&session),
        message_count: visible_message_count(&session),
        last_preview: session_last_preview(&session),
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
    let uuid = parse_session_uuid(&id)?;

    let session = load_session_by_uuid(&state, uuid)?;

    let messages: Vec<MessageSummary> = session
        .messages
        .iter()
        .filter(|m| m.user_visible)
        .map(summarize_message)
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
    let uuid = parse_session_uuid(&id)?;

    state
        .inner
        .stack
        .session_manager
        .rename(uuid, &req.name)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

/// Archive a session by persisting archived status in metadata.
pub(crate) async fn archive_session(
    State(state): State<WebState>,
    Path(id): Path<String>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let uuid = parse_session_uuid(&id)?;
    persist_session_status(&state, uuid, PersistedSessionStatus::Archived)?;
    debug!(session_id = %uuid, "session archived");
    Ok(Json(serde_json::json!({ "ok": true })))
}

/// Restore an archived session to active status.
pub(crate) async fn unarchive_session(
    State(state): State<WebState>,
    Path(id): Path<String>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let uuid = parse_session_uuid(&id)?;
    persist_session_status(&state, uuid, PersistedSessionStatus::Active)?;
    debug!(session_id = %uuid, "session unarchived");
    Ok(Json(serde_json::json!({ "ok": true })))
}

/// Delete a session.
pub(crate) async fn delete_session(
    State(state): State<WebState>,
    Path(id): Path<String>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let uuid = parse_session_uuid(&id)?;

    state
        .inner
        .stack
        .session_manager
        .delete(uuid)
        .map_err(|e| {
            warn!(session_id = %uuid, error = %e, "failed to delete session");
            error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string())
        })?;

    debug!(session_id = %uuid, "session deleted");
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
    let uuid = parse_session_uuid(&req.id)?;

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
    let uuid = parse_session_uuid(&req.id)?;

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

    let summaries = sessions.iter().map(summarize_session).collect();

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
    let uuid = parse_session_uuid(&id)?;

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
    #[serde(rename = "tokens_used")]
    pub tokens_used: Option<u32>,
    /// USD cost for this message.
    #[serde(default)]
    #[serde(rename = "cost_usd")]
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

    let mut session = state
        .inner
        .stack
        .session_manager
        .get(session_uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    let message = session
        .messages
        .iter_mut()
        .find(|message| message.id == msg_uuid)
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Message not found"))?;

    if let Some(content) = req.content {
        message.content = content;
    }

    let mut metadata = message
        .metadata
        .as_object()
        .cloned()
        .unwrap_or_else(Map::new);

    if let Some(extra_metadata) = req.metadata {
        if let Some(extra_map) = extra_metadata.as_object() {
            metadata.extend(extra_map.clone());
        } else {
            metadata.insert("frontend_metadata".to_string(), extra_metadata);
        }
    }

    if let Some(tokens_used) = req.tokens_used {
        metadata.insert("tokens_used".to_string(), Value::from(tokens_used));
    }

    if let Some(cost_usd) = req.cost_usd {
        metadata.insert("cost_usd".to_string(), Value::from(cost_usd));
    }

    if let Some(model) = req.model {
        metadata.insert("model".to_string(), Value::from(model));
    }

    message.metadata = Value::Object(metadata.clone());
    let reconstructed_tool_calls = message_tool_calls_from_metadata(&message.metadata);
    if !reconstructed_tool_calls.is_empty() {
        message.tool_calls = reconstructed_tool_calls;
    }

    state
        .inner
        .stack
        .session_manager
        .save(&session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

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
    /// Name for the new session. Auto-generated from source if omitted.
    #[serde(default)]
    pub name: Option<String>,
    /// Optional client-provided ID for the new session.
    #[serde(default)]
    pub id: Option<String>,
    /// Explicit clone semantics so backend can own duplicate vs fork lineage.
    #[serde(default)]
    pub kind: SessionCloneKind,
}

/// Clone a session: duplicate creates a root-level copy, while fork persists parent linkage.
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

    // Set title — use provided name or auto-generate from source
    let title = req.name.clone().unwrap_or_else(|| {
        let source_title = source_session
            .metadata
            .get("title")
            .and_then(|v| v.as_str())
            .unwrap_or("Untitled");
        match req.kind {
            SessionCloneKind::Duplicate => format!("{source_title} (copy)"),
            SessionCloneKind::Fork => format!("{source_title} (fork)"),
        }
    });
    if let Some(map) = new_session.metadata.as_object_mut() {
        map.insert("title".to_string(), serde_json::Value::String(title));
        map.insert(
            "status".to_string(),
            serde_json::Value::String(PersistedSessionStatus::Active.as_str().to_string()),
        );
    }
    let parent_session_id = match req.kind {
        SessionCloneKind::Duplicate => None,
        SessionCloneKind::Fork => Some(source_session.id.to_string()),
    };
    set_session_parent_session_id(&mut new_session, parent_session_id.clone());

    // Save the new session first
    state
        .inner
        .stack
        .session_manager
        .save(&new_session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    // Copy all persisted messages from source to new session.
    // Summary fields below still use the visible subset, but the cloned session
    // must retain hidden/system/tool messages so web clone semantics match desktop.
    let message_count = visible_message_count(&source_session);
    for msg in &source_session.messages {
        let mut cloned = msg.clone();
        cloned.id = uuid::Uuid::new_v4();
        state
            .inner
            .stack
            .session_manager
            .add_message(new_session.id, &cloned)
            .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;
    }

    let persisted_session = load_session_by_uuid(&state, new_session.id)?;
    let final_title = persisted_session
        .metadata
        .get("title")
        .and_then(|v| v.as_str())
        .unwrap_or("Untitled")
        .to_string();
    let last_preview = session_last_preview(&persisted_session);
    Ok(Json(SessionSummary {
        id: new_session.id.to_string(),
        title: final_title,
        status: PersistedSessionStatus::Active.as_str().to_string(),
        parent_session_id,
        message_count,
        last_preview,
        created_at: persisted_session.created_at.to_rfc3339(),
        updated_at: persisted_session.updated_at.to_rfc3339(),
    }))
}
