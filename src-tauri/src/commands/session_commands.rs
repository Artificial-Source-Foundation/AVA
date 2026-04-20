//! Tauri commands for session CRUD + rename + search.
//!
//! Wraps `SessionManager` from `ava-session`.

use chrono::{TimeZone, Utc};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use tauri::State;
use tokio::task;
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

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ActiveSessionSyncResult {
    pub session_id: String,
    pub exists: bool,
    pub message_count: usize,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActiveSessionSyncMessage {
    pub id: String,
    pub role: String,
    #[serde(default)]
    pub content: String,
    pub created_at: i64,
    #[serde(default)]
    pub images: Vec<ActiveSessionSyncImage>,
    #[serde(default = "empty_json_object")]
    pub metadata: Value,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActiveSessionSyncImage {
    pub data: String,
    pub media_type: String,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActiveSessionSyncSnapshot {
    #[serde(default)]
    pub title: Option<String>,
    #[serde(default)]
    pub messages: Vec<ActiveSessionSyncMessage>,
}

fn empty_json_object() -> Value {
    Value::Object(Map::new())
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

async fn run_session_blocking<T, F>(bridge: &DesktopBridge, op: F) -> Result<T, String>
where
    T: Send + 'static,
    F: FnOnce(&ava_session::SessionManager) -> ava_types::Result<T> + Send + 'static,
{
    let session_manager = bridge.stack.session_manager.clone();
    task::spawn_blocking(move || op(&session_manager))
        .await
        .map_err(|e| format!("session task join error: {e}"))?
        .map_err(|e| e.to_string())
}

async fn set_active_session_inner(
    id: String,
    working_directory: Option<String>,
    snapshot: Option<ActiveSessionSyncSnapshot>,
    bridge: &DesktopBridge,
) -> Result<ActiveSessionSyncResult, String> {
    let uuid = Uuid::parse_str(&id).map_err(|e| format!("invalid session ID: {e}"))?;
    let session = run_session_blocking(bridge, move |sm| sm.get(uuid)).await?;

    let session = if let Some(session) = session {
        session
    } else if let Some(snapshot) = snapshot {
        let materialized = materialize_session_snapshot(uuid, snapshot)?;
        let persisted = materialized.clone();
        run_session_blocking(bridge, move |sm| {
            sm.save(&persisted)?;
            Ok(())
        })
        .await?;
        materialized
    } else {
        return Ok(ActiveSessionSyncResult {
            session_id: id,
            exists: false,
            message_count: 0,
        });
    };

    if let Some(working_directory) = working_directory.as_deref() {
        bridge
            .bind_session_working_directory(uuid, working_directory)
            .await;
    } else {
        bridge.clear_session_working_directory_unbound(uuid).await;
    }

    *bridge.last_session_id.write().await = Some(uuid);
    Ok(ActiveSessionSyncResult {
        session_id: id,
        exists: true,
        message_count: session.messages.len(),
    })
}

fn materialize_session_snapshot(
    session_id: Uuid,
    snapshot: ActiveSessionSyncSnapshot,
) -> Result<ava_types::Session, String> {
    let mut session = ava_types::Session::new().with_id(session_id);

    if let Some(title) = snapshot.title.map(|value| value.trim().to_string()) {
        if !title.is_empty() {
            if let Some(metadata) = session.metadata.as_object_mut() {
                metadata.insert("title".to_string(), Value::String(title));
            }
        }
    }

    let mut messages = Vec::with_capacity(snapshot.messages.len());
    for frontend_message in snapshot.messages {
        messages.push(materialize_session_message(frontend_message)?);
    }

    if let Some(first) = messages.first() {
        session.created_at = first.timestamp;
    }
    if let Some(last) = messages.last() {
        session.updated_at = last.timestamp;
    }
    session.messages = messages;

    Ok(session)
}

fn materialize_session_message(
    snapshot: ActiveSessionSyncMessage,
) -> Result<ava_types::Message, String> {
    let role = match snapshot.role.as_str() {
        "user" => ava_types::Role::User,
        "assistant" => ava_types::Role::Assistant,
        "system" => ava_types::Role::System,
        "tool" => ava_types::Role::Tool,
        other => return Err(format!("unsupported session message role '{other}'")),
    };

    let message_id =
        Uuid::parse_str(&snapshot.id).map_err(|e| format!("invalid session message ID: {e}"))?;
    let timestamp = Utc
        .timestamp_millis_opt(snapshot.created_at)
        .single()
        .unwrap_or_else(Utc::now);

    let mut message = ava_types::Message::new(role, snapshot.content);
    message.id = message_id;
    message.timestamp = timestamp;
    message.images = snapshot
        .images
        .into_iter()
        .map(materialize_session_image)
        .collect::<Result<Vec<_>, _>>()?;
    apply_materialized_session_message_metadata(&mut message, snapshot.metadata)?;
    Ok(message)
}

fn apply_materialized_session_message_metadata(
    message: &mut ava_types::Message,
    metadata: Value,
) -> Result<(), String> {
    let metadata = match metadata {
        Value::Object(map) => Value::Object(map),
        Value::Null => empty_json_object(),
        _ => empty_json_object(),
    };

    message.tool_calls = message_tool_calls_from_metadata(&metadata);
    message.tool_call_id = metadata
        .get("toolCallId")
        .or_else(|| metadata.get("tool_call_id"))
        .and_then(Value::as_str)
        .map(str::to_string);
    message.agent_visible = metadata
        .get("agentVisible")
        .or_else(|| metadata.get("agent_visible"))
        .and_then(Value::as_bool)
        .unwrap_or(message.agent_visible);
    message.user_visible = metadata
        .get("userVisible")
        .or_else(|| metadata.get("user_visible"))
        .and_then(Value::as_bool)
        .unwrap_or(message.user_visible);
    message.original_content = metadata
        .get("originalContent")
        .or_else(|| metadata.get("original_content"))
        .and_then(Value::as_str)
        .map(str::to_string);
    message.parent_id = metadata
        .get("parentId")
        .or_else(|| metadata.get("parent_id"))
        .and_then(Value::as_str)
        .map(|value| Uuid::parse_str(value).map_err(|e| format!("invalid parent message ID: {e}")))
        .transpose()?;
    message.structured_content = metadata
        .get("structuredContent")
        .or_else(|| metadata.get("structured_content"))
        .cloned()
        .map(serde_json::from_value)
        .transpose()
        .map_err(|e| format!("invalid structured session message content: {e}"))?
        .unwrap_or_default();
    message.metadata = metadata;
    Ok(())
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

fn materialize_session_image(
    snapshot: ActiveSessionSyncImage,
) -> Result<ava_types::ImageContent, String> {
    let media_type = match snapshot.media_type.as_str() {
        "image/png" => ava_types::ImageMediaType::Png,
        "image/jpeg" => ava_types::ImageMediaType::Jpeg,
        "image/gif" => ava_types::ImageMediaType::Gif,
        "image/webp" => ava_types::ImageMediaType::WebP,
        other => return Err(format!("unsupported session image media type '{other}'")),
    };

    Ok(ava_types::ImageContent::new(snapshot.data, media_type))
}

async fn create_session_inner(bridge: &DesktopBridge) -> Result<SessionSummary, String> {
    let session = run_session_blocking(bridge, |sm| {
        let session = sm.create()?;
        sm.save(&session)?;
        Ok(session)
    })
    .await?;
    *bridge.last_session_id.write().await = Some(session.id);
    Ok(session_to_summary(&session))
}

/// List recent sessions, most recent first.
#[tauri::command]
pub async fn list_sessions(
    limit: Option<usize>,
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<SessionSummary>, String> {
    let sessions =
        run_session_blocking(&bridge, move |sm| sm.list_recent(limit.unwrap_or(50))).await?;
    Ok(sessions.iter().map(session_to_summary).collect())
}

/// Load a full session by ID, including all messages.
#[tauri::command]
pub async fn load_session(
    id: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    let uuid = Uuid::parse_str(&id).map_err(|e| format!("invalid session ID: {e}"))?;
    let session = run_session_blocking(&bridge, move |sm| sm.get(uuid))
        .await?
        .ok_or_else(|| format!("session not found: {id}"))?;
    *bridge.last_session_id.write().await = Some(uuid);
    serde_json::to_value(&session).map_err(|e| e.to_string())
}

/// Create a new empty session.
#[tauri::command]
pub async fn create_session(bridge: State<'_, DesktopBridge>) -> Result<SessionSummary, String> {
    create_session_inner(&bridge).await
}

/// Mark the active desktop session so retry/regenerate/edit flows target the restored session.
#[tauri::command]
pub async fn set_active_session(
    id: String,
    working_directory: Option<String>,
    snapshot: Option<ActiveSessionSyncSnapshot>,
    bridge: State<'_, DesktopBridge>,
) -> Result<ActiveSessionSyncResult, String> {
    set_active_session_inner(id, working_directory, snapshot, &bridge).await
}

/// Delete a session by ID.
#[tauri::command]
pub async fn delete_session(id: String, bridge: State<'_, DesktopBridge>) -> Result<(), String> {
    let uuid = Uuid::parse_str(&id).map_err(|e| format!("invalid session ID: {e}"))?;
    run_session_blocking(&bridge, move |sm| sm.delete(uuid)).await
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
    let title = trimmed.to_string();
    run_session_blocking(&bridge, move |sm| sm.rename(uuid, &title)).await
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
    let query = trimmed.to_string();
    let sessions = run_session_blocking(&bridge, move |sm| sm.search(&query)).await?;
    Ok(sessions.iter().map(session_to_summary).collect())
}

#[cfg(test)]
mod tests {
    use ava_agent::control_plane::sessions::build_retry_replay_payload;
    use ava_types::{repair_conversation, Role, ToolCall};
    use serde_json::json;
    use serde_json::Value;
    use tempfile::tempdir;

    use crate::bridge::DesktopBridge;

    use super::{
        create_session_inner, empty_json_object, materialize_session_snapshot,
        run_session_blocking, set_active_session_inner, ActiveSessionSyncImage,
        ActiveSessionSyncMessage, ActiveSessionSyncSnapshot,
    };

    #[tokio::test]
    async fn create_session_command_persists_sessions_before_active_session_checks() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");

        let created = create_session_inner(&bridge)
            .await
            .expect("session should be created and persisted");

        let result = set_active_session_inner(created.id.clone(), None, None, &bridge)
            .await
            .expect("freshly created session should bind immediately");

        assert_eq!(result.session_id, created.id);
        assert!(result.exists);
        assert_eq!(result.message_count, 0);
    }

    #[tokio::test]
    async fn set_active_session_command_reports_existing_session_and_updates_last_session() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");

        let session = run_session_blocking(&bridge, |sm| {
            let session = sm.create()?;
            sm.save(&session)?;
            Ok(session)
        })
        .await
        .expect("session should be created and persisted");
        let session_id = session.id.to_string();

        let result = set_active_session_inner(session_id.clone(), None, None, &bridge)
            .await
            .expect("active session should sync");

        assert_eq!(result.session_id, session_id);
        assert!(result.exists);
        assert_eq!(result.message_count, 0);
        assert_eq!(*bridge.last_session_id.read().await, Some(session.id));
    }

    #[tokio::test]
    async fn set_active_session_command_reports_missing_sessions_without_throwing() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");
        let missing = uuid::Uuid::new_v4().to_string();

        let result = set_active_session_inner(missing.clone(), None, None, &bridge)
            .await
            .expect("missing session should be reported as a result");

        assert_eq!(result.session_id, missing);
        assert!(!result.exists);
        assert_eq!(result.message_count, 0);
        assert!(bridge.last_session_id.read().await.is_none());
    }

    #[tokio::test]
    async fn set_active_session_command_materializes_missing_sessions_from_frontend_snapshot() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");
        let missing = uuid::Uuid::new_v4().to_string();
        let user_id = uuid::Uuid::new_v4().to_string();
        let assistant_id = uuid::Uuid::new_v4().to_string();

        let result = set_active_session_inner(
            missing.clone(),
            None,
            Some(ActiveSessionSyncSnapshot {
                title: Some("Recovered desktop session".to_string()),
                messages: vec![
                    ActiveSessionSyncMessage {
                        id: user_id.clone(),
                        role: "user".to_string(),
                        content: "hello from desktop".to_string(),
                        created_at: 1_762_806_000_000,
                        images: vec![ActiveSessionSyncImage {
                            data: "base64-image".to_string(),
                            media_type: "image/png".to_string(),
                        }],
                        metadata: empty_json_object(),
                    },
                    ActiveSessionSyncMessage {
                        id: assistant_id.clone(),
                        role: "assistant".to_string(),
                        content: "hi from backend".to_string(),
                        created_at: 1_762_806_001_000,
                        images: vec![],
                        metadata: empty_json_object(),
                    },
                ],
            }),
            &bridge,
        )
        .await
        .expect("missing session snapshot should materialize backend session");

        assert_eq!(result.session_id, missing);
        assert!(result.exists);
        assert_eq!(result.message_count, 2);

        let uuid = uuid::Uuid::parse_str(&result.session_id).expect("session id should stay valid");
        let session = run_session_blocking(&bridge, move |sm| sm.get(uuid))
            .await
            .expect("session read should succeed")
            .expect("materialized session should exist");

        assert_eq!(
            session.metadata.get("title").and_then(Value::as_str),
            Some("Recovered desktop session")
        );
        assert_eq!(session.messages.len(), 2);
        assert_eq!(session.messages[0].id.to_string(), user_id);
        assert_eq!(session.messages[0].content, "hello from desktop");
        assert_eq!(
            session.messages[0].images,
            vec![ava_types::ImageContent::new(
                "base64-image",
                ava_types::ImageMediaType::Png,
            )]
        );
        assert_eq!(session.messages[1].id.to_string(), assistant_id);
        assert_eq!(session.messages[1].content, "hi from backend");
        assert_eq!(session.messages[1].role, ava_types::Role::Assistant);
    }

    #[test]
    fn materialized_session_snapshot_preserves_tool_replay_context() {
        let session_id = uuid::Uuid::new_v4();
        let session = materialize_session_snapshot(
            session_id,
            ActiveSessionSyncSnapshot {
                title: Some("Recovered tool session".to_string()),
                messages: vec![
                    ActiveSessionSyncMessage {
                        id: uuid::Uuid::new_v4().to_string(),
                        role: "user".to_string(),
                        content: "inspect the workspace".to_string(),
                        created_at: 1,
                        images: vec![],
                        metadata: empty_json_object(),
                    },
                    ActiveSessionSyncMessage {
                        id: uuid::Uuid::new_v4().to_string(),
                        role: "assistant".to_string(),
                        content: "".to_string(),
                        created_at: 2,
                        images: vec![],
                        metadata: json!({
                            "agentVisible": false,
                            "toolCalls": [
                                {
                                    "id": "tool-call-1",
                                    "name": "bash",
                                    "arguments": { "command": "pwd" },
                                    "status": "success"
                                }
                            ]
                        }),
                    },
                    ActiveSessionSyncMessage {
                        id: uuid::Uuid::new_v4().to_string(),
                        role: "tool".to_string(),
                        content: "/workspace".to_string(),
                        created_at: 3,
                        images: vec![],
                        metadata: json!({
                            "toolCallId": "tool-call-1",
                            "userVisible": false
                        }),
                    },
                    ActiveSessionSyncMessage {
                        id: uuid::Uuid::new_v4().to_string(),
                        role: "user".to_string(),
                        content: "continue".to_string(),
                        created_at: 4,
                        images: vec![],
                        metadata: empty_json_object(),
                    },
                ],
            },
        )
        .expect("snapshot should materialize");

        assert_eq!(session.messages[1].role, Role::Assistant);
        assert_eq!(session.messages[1].tool_calls.len(), 1);
        assert_eq!(
            session.messages[1].tool_calls,
            vec![ToolCall {
                id: "tool-call-1".to_string(),
                name: "bash".to_string(),
                arguments: json!({ "command": "pwd" }),
            }]
        );
        assert!(!session.messages[1].agent_visible);
        assert_eq!(session.messages[2].role, Role::Tool);
        assert_eq!(
            session.messages[2].tool_call_id.as_deref(),
            Some("tool-call-1")
        );
        assert!(!session.messages[2].user_visible);

        let replay = build_retry_replay_payload(&session).expect("retry replay should build");
        assert_eq!(replay.goal, "continue");
        assert_eq!(replay.history.len(), 3);
        assert_eq!(replay.history[1].tool_calls, session.messages[1].tool_calls);
        assert_eq!(replay.history[2].role, Role::Tool);
        assert_eq!(
            replay.history[2].tool_call_id.as_deref(),
            Some("tool-call-1")
        );

        let mut repaired_history = replay.history.clone();
        repair_conversation(&mut repaired_history);
        assert_eq!(repaired_history.len(), 3);
        assert_eq!(repaired_history[2].role, Role::Tool);
        assert_eq!(
            repaired_history[2].tool_call_id.as_deref(),
            Some("tool-call-1")
        );
    }

    #[tokio::test]
    async fn set_active_session_command_binds_forwarded_working_directory_to_session_context() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");
        let project_dir = dir.path().join("project-two");
        std::fs::create_dir_all(&project_dir).expect("project dir should exist");

        let session = run_session_blocking(&bridge, |sm| {
            let session = sm.create()?;
            sm.save(&session)?;
            Ok(session)
        })
        .await
        .expect("session should be created and persisted");

        let result = set_active_session_inner(
            session.id.to_string(),
            Some(project_dir.to_string_lossy().to_string()),
            None,
            &bridge,
        )
        .await
        .expect("active session should sync with cwd binding");

        assert!(result.exists);

        let run = bridge
            .register_run(
                "desktop-run-cwd".to_string(),
                session.id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("run should reuse bound session context");

        assert_eq!(
            run.permission_context.read().await.workspace_root,
            project_dir
        );
    }

    #[tokio::test]
    async fn set_active_session_command_marks_missing_working_directory_as_unbound() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");
        let project_dir = dir.path().join("project-two");
        std::fs::create_dir_all(&project_dir).expect("project dir should exist");

        let session = run_session_blocking(&bridge, |sm| {
            let session = sm.create()?;
            sm.save(&session)?;
            Ok(session)
        })
        .await
        .expect("session should be created and persisted");

        set_active_session_inner(
            session.id.to_string(),
            Some(project_dir.to_string_lossy().to_string()),
            None,
            &bridge,
        )
        .await
        .expect("active session should bind cwd before unbinding");

        let first_run = bridge
            .register_run(
                "desktop-run-bound".to_string(),
                session.id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("bound session should allow runs");

        assert_eq!(
            first_run.permission_context.read().await.workspace_root,
            project_dir
        );

        bridge.finish_run("desktop-run-bound").await;

        let result =
            set_active_session_inner(session.id.to_string(), Some(String::new()), None, &bridge)
                .await
                .expect("active session should still sync when cwd is unresolved");

        assert!(result.exists);
        assert!(
            bridge
                .session_permission_contexts
                .read()
                .await
                .get(&session.id)
                .is_some(),
            "unresolved-bound sessions should keep a reset cached context while failing closed"
        );

        let err = match bridge
            .register_run(
                "desktop-run-unbound".to_string(),
                session.id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
        {
            Ok(_) => panic!("explicitly unbound sessions should fail closed"),
            Err(err) => err,
        };

        assert!(err.contains("resolved working directory"));
    }

    #[tokio::test]
    async fn set_active_session_command_keeps_never_bound_sessions_runnable_after_recovery() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");

        let session = run_session_blocking(&bridge, |sm| {
            let session = sm.create()?;
            sm.save(&session)?;
            Ok(session)
        })
        .await
        .expect("session should be created and persisted");

        bridge
            .mark_session_working_directory_unbound(session.id)
            .await;

        let legacy_err = match bridge
            .register_run(
                "desktop-run-projectless-legacy".to_string(),
                session.id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
        {
            Ok(_) => {
                panic!("legacy incorrectly-unbound projectless session should fail before reopen")
            }
            Err(err) => err,
        };

        assert!(legacy_err.contains("resolved working directory"));

        let result = set_active_session_inner(session.id.to_string(), None, None, &bridge)
            .await
            .expect("projectless session should still sync");

        assert!(result.exists);
        assert!(
            bridge
                .session_permission_contexts
                .read()
                .await
                .get(&session.id)
                .is_some(),
            "projectless reopen should rebuild the permission context with a safe default root"
        );
        assert!(
            !bridge
                .explicitly_unbound_sessions
                .read()
                .await
                .contains(&session.id),
            "reopening a never-bound/projectless session should clear stale unbound state"
        );

        bridge
            .register_run(
                "desktop-run-projectless-recovered".to_string(),
                session.id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("reopened never-bound/projectless session should remain runnable");
    }

    #[tokio::test]
    async fn set_active_session_command_clears_stale_workspace_binding_when_reopening_projectless_session(
    ) {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");
        let project_dir = dir.path().join("project-three");
        std::fs::create_dir_all(&project_dir).expect("project dir should exist");

        let session = run_session_blocking(&bridge, |sm| {
            let session = sm.create()?;
            sm.save(&session)?;
            Ok(session)
        })
        .await
        .expect("session should be created and persisted");

        set_active_session_inner(
            session.id.to_string(),
            Some(project_dir.to_string_lossy().to_string()),
            None,
            &bridge,
        )
        .await
        .expect("active session should bind cwd before projectless reopen");

        let first_run = bridge
            .register_run(
                "desktop-run-project-bound".to_string(),
                session.id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("bound session should allow runs");

        assert_eq!(
            first_run.permission_context.read().await.workspace_root,
            project_dir
        );
        first_run
            .permission_context
            .write()
            .await
            .session_approved
            .insert("bash".to_string());

        bridge.finish_run("desktop-run-project-bound").await;

        let result = set_active_session_inner(session.id.to_string(), None, None, &bridge)
            .await
            .expect("projectless reopen should sync");

        assert!(result.exists);
        assert!(
            !bridge
                .explicitly_unbound_sessions
                .read()
                .await
                .contains(&session.id),
            "projectless reopen should not leave the session explicitly unbound"
        );

        let projectless_run = bridge
            .register_run(
                "desktop-run-projectless-reopen".to_string(),
                session.id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("projectless reopen should remain runnable");

        assert_ne!(
            projectless_run
                .permission_context
                .read()
                .await
                .workspace_root,
            project_dir,
            "projectless reopen should not inherit the old bound workspace root"
        );
        assert!(
            projectless_run
                .permission_context
                .read()
                .await
                .session_approved
                .contains("bash"),
            "projectless reopen should preserve session-scoped approvals while resetting the workspace root"
        );
    }
}
