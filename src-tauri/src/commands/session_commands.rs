//! Tauri commands for session CRUD + rename + search.
//!
//! Wraps `SessionManager` from `ava-session`.

use serde::Serialize;
use tauri::State;
use uuid::Uuid;

use crate::bridge::DesktopBridge;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionSummary {
    pub id: String,
    pub title: String,
    pub message_count: usize,
    pub created_at: String,
    pub updated_at: String,
}

fn session_to_summary(s: &ava_types::Session) -> SessionSummary {
    // Prefer an explicit title stored in metadata (set by rename_session),
    // falling back to auto-generating from the first message.
    let title = s
        .metadata
        .get("title")
        .and_then(|v| v.as_str())
        .map(String::from)
        .unwrap_or_else(|| {
            s.messages
                .first()
                .map(|m| ava_session::generate_title(&m.content))
                .unwrap_or_else(|| "New session".to_string())
        });
    SessionSummary {
        id: s.id.to_string(),
        title,
        message_count: s.messages.len(),
        created_at: s.created_at.to_rfc3339(),
        updated_at: s.updated_at.to_rfc3339(),
    }
}

/// List recent sessions, most recent first.
#[tauri::command]
pub async fn list_sessions(
    limit: Option<usize>,
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<SessionSummary>, String> {
    let sessions = bridge
        .stack
        .session_manager
        .list_recent(limit.unwrap_or(50))
        .map_err(|e| e.to_string())?;
    Ok(sessions.iter().map(session_to_summary).collect())
}

/// Load a full session by ID, including all messages.
#[tauri::command]
pub async fn load_session(
    id: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    let uuid = Uuid::parse_str(&id).map_err(|e| format!("invalid session ID: {e}"))?;
    let session = bridge
        .stack
        .session_manager
        .get(uuid)
        .map_err(|e| e.to_string())?
        .ok_or_else(|| format!("session not found: {id}"))?;
    serde_json::to_value(&session).map_err(|e| e.to_string())
}

/// Create a new empty session.
#[tauri::command]
pub async fn create_session(bridge: State<'_, DesktopBridge>) -> Result<SessionSummary, String> {
    let session = bridge
        .stack
        .session_manager
        .create()
        .map_err(|e| e.to_string())?;
    Ok(session_to_summary(&session))
}

/// Delete a session by ID.
#[tauri::command]
pub async fn delete_session(id: String, bridge: State<'_, DesktopBridge>) -> Result<(), String> {
    let uuid = Uuid::parse_str(&id).map_err(|e| format!("invalid session ID: {e}"))?;
    bridge
        .stack
        .session_manager
        .delete(uuid)
        .map_err(|e| e.to_string())
}

/// Rename a session by setting a custom title in its metadata.
#[tauri::command]
pub async fn rename_session(
    id: String,
    title: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    let trimmed = title.trim();
    if trimmed.is_empty() {
        return Err("session title cannot be empty".to_string());
    }
    let uuid = Uuid::parse_str(&id).map_err(|e| format!("invalid session ID: {e}"))?;
    bridge
        .stack
        .session_manager
        .rename(uuid, trimmed)
        .map_err(|e| e.to_string())
}

/// Search sessions using FTS5 full-text search over message content.
#[tauri::command]
pub async fn search_sessions(
    query: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<SessionSummary>, String> {
    let trimmed = query.trim();
    if trimmed.is_empty() {
        return Ok(Vec::new());
    }
    let sessions = bridge
        .stack
        .session_manager
        .search(trimmed)
        .map_err(|e| e.to_string())?;
    Ok(sessions.iter().map(session_to_summary).collect())
}
