//! Tauri commands for running the agent loop, mid-stream messaging,
//! approval/question resolution, and retry/regenerate/undo actions.
//!
//! The approval and question flows work as follows:
//! 1. The agent's permission middleware sends an `ApprovalRequest` through the bridge
//! 2. A spawned forwarder task picks it up and emits an `approval_request` event to the frontend
//! 3. The frontend shows the ApprovalDock and calls `resolve_approval` when the user decides
//! 4. The resolve command sends the response through the stored oneshot channel
//! 5. The permission middleware receives it and continues or blocks the tool

use ava_agent::control_plane::commands::{queue_message_tier, ControlPlaneCommand};
use ava_agent::control_plane::interactive::{
    InteractiveRequestKind, InteractiveRequestStore, ResolveInteractiveRequestError,
    TerminalInteractiveRequest,
};
use ava_agent::control_plane::orchestration::{
    clear_preserved_deferred, is_inactive_scoped_status_lookup, restore_in_flight_deferred,
    sync_deferred_queues_for_progress,
};
use ava_agent::control_plane::queue::{
    clear_queue_semantics, ClearQueueTarget, QueueClearSemantics, UNSUPPORTED_QUEUE_CLEAR_ERROR,
};
use ava_agent::control_plane::sessions::{
    build_edit_replay_payload, build_regenerate_replay_payload, build_retry_replay_payload,
    resolve_session_precedence, run_context_from_session as shared_run_context_from_session,
    SessionPromptContext, SessionSelectionSource,
};
use ava_agent::stack::AgentRunContext;
use ava_tools::permission_middleware::ToolApproval;
use serde::{Deserialize, Serialize};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Emitter, State};
use tokio::sync::mpsc;
use tokio::task;
use tracing::info;
use uuid::Uuid;

use crate::bridge::DesktopBridge;
use crate::events::{emit_backend_event, AgentEvent};

fn ensure_desktop_run_id(run_id: Option<String>) -> String {
    run_id.unwrap_or_else(|| format!("desktop-run-{}", Uuid::new_v4()))
}

async fn cancel_desktop_run(app: &AppHandle, bridge: &DesktopBridge, run_id: &str) {
    let _interactive_guard = bridge.interactive_lifecycle_lock.lock().await;
    let Ok(run) = bridge.resolve_run(Some(run_id), None).await else {
        return;
    };
    run.interactive_revoked.store(true, Ordering::SeqCst);
    bridge.revoke_queue_dispatch(run_id, true).await;
    run.cancel.cancel();
    loop {
        let previous_global_request_id = bridge.pending_approval_reply.current_request_id().await;
        let Some(pending) = bridge
            .pending_approval_reply
            .cancel_pending_for_run(run_id)
            .await
        else {
            break;
        };
        let request_id = pending.handle.request_id.clone();
        bridge
            .discard_deferred_interactive_request_event(&request_id)
            .await;
        let _ = pending.reply.send(ToolApproval::Rejected(Some(
            "Agent run cancelled from desktop UI".to_string(),
        )));
        emit_interactive_request_cleared(
            app,
            &request_id,
            "approval",
            false,
            pending.handle.run_id.as_deref(),
        );
        emit_promoted_desktop_interactive_request_if_current_changed(
            app,
            bridge,
            pending.handle.kind,
            &request_id,
            previous_global_request_id.as_deref(),
        )
        .await;
    }
    loop {
        let previous_global_request_id = bridge.pending_question_reply.current_request_id().await;
        let Some(pending) = bridge
            .pending_question_reply
            .cancel_pending_for_run(run_id)
            .await
        else {
            break;
        };
        let request_id = pending.handle.request_id.clone();
        bridge
            .discard_deferred_interactive_request_event(&request_id)
            .await;
        let _ = pending.reply.send(String::new());
        emit_interactive_request_cleared(
            app,
            &request_id,
            "question",
            false,
            pending.handle.run_id.as_deref(),
        );
        emit_promoted_desktop_interactive_request_if_current_changed(
            app,
            bridge,
            pending.handle.kind,
            &request_id,
            previous_global_request_id.as_deref(),
        )
        .await;
    }
    loop {
        let previous_global_request_id = bridge.pending_plan_reply.current_request_id().await;
        let Some(pending) = bridge
            .pending_plan_reply
            .cancel_pending_for_run(run_id)
            .await
        else {
            break;
        };
        let request_id = pending.handle.request_id.clone();
        bridge
            .discard_deferred_interactive_request_event(&request_id)
            .await;
        let _ = pending.reply.send(ava_types::PlanDecision::Rejected {
            feedback: "Agent run cancelled from desktop UI".to_string(),
        });
        emit_interactive_request_cleared(
            app,
            &request_id,
            "plan",
            false,
            pending.handle.run_id.as_deref(),
        );
        emit_promoted_desktop_interactive_request_if_current_changed(
            app,
            bridge,
            pending.handle.kind,
            &request_id,
            previous_global_request_id.as_deref(),
        )
        .await;
    }
}

async fn save_session_checkpoint(
    session_manager: std::sync::Arc<ava_session::SessionManager>,
    session: ava_types::Session,
) -> Result<(), String> {
    task::spawn_blocking(move || session_manager.save(&session))
        .await
        .map_err(|e| format!("session checkpoint join error: {e}"))?
        .map_err(|e| e.to_string())
}

fn emit_interactive_request_cleared<R: tauri::Runtime>(
    app: &AppHandle<R>,
    request_id: &str,
    request_kind: &str,
    timed_out: bool,
    run_id: Option<&str>,
) {
    let payload = interactive_request_cleared_event(request_id, request_kind, timed_out, run_id);
    if let Err(error) = app.emit("agent-event", payload) {
        tracing::error!("Failed to emit interactive_request_cleared event to frontend: {error}");
    }
}

async fn emit_promoted_desktop_interactive_request<R: tauri::Runtime>(
    app: &AppHandle<R>,
    bridge: &DesktopBridge,
    kind: ava_agent::control_plane::interactive::InteractiveRequestKind,
    run_id: Option<&str>,
) {
    let Some(event) = bridge
        .take_promoted_interactive_request_event(kind, run_id)
        .await
    else {
        return;
    };
    if let Err(error) = app.emit("agent-event", event) {
        tracing::error!("Failed to emit promoted interactive request event to frontend: {error}");
    }
}

async fn emit_promoted_desktop_interactive_request_if_current_changed<R: tauri::Runtime>(
    app: &AppHandle<R>,
    bridge: &DesktopBridge,
    kind: ava_agent::control_plane::interactive::InteractiveRequestKind,
    removed_request_id: &str,
    previous_global_request_id: Option<&str>,
) {
    let Some(event) = bridge
        .promoted_interactive_request_event_after_current_change(
            kind,
            removed_request_id,
            previous_global_request_id,
        )
        .await
    else {
        return;
    };
    if let Err(error) = app.emit("agent-event", event) {
        tracing::error!("Failed to emit promoted interactive request event to frontend: {error}");
    }
}

fn interactive_request_cleared_event(
    request_id: &str,
    request_kind: &str,
    timed_out: bool,
    run_id: Option<&str>,
) -> AgentEvent {
    AgentEvent::InteractiveRequestCleared {
        request_id: request_id.to_string(),
        request_kind: request_kind.to_string(),
        timed_out,
        run_id: run_id.map(str::to_string),
    }
}

fn desktop_interactive_resolve_error(kind: &str, error: ResolveInteractiveRequestError) -> String {
    match error {
        ResolveInteractiveRequestError::MissingPendingRequest { .. } => {
            format!("No pending {kind} request to resolve")
        }
        ResolveInteractiveRequestError::StaleRequestId { .. } => {
            format!("No matching pending {kind} request to resolve")
        }
    }
}

fn missing_request_id_error(kind: &str) -> String {
    format!("request_id is required to resolve pending {kind} request")
}

async fn take_resolved_desktop_interactive_request<T>(
    store: &InteractiveRequestStore<T>,
    kind: &str,
    request_id: Option<&str>,
) -> Result<TerminalInteractiveRequest<T>, String> {
    let request_id = request_id.ok_or_else(|| missing_request_id_error(kind))?;

    store
        .resolve(Some(request_id))
        .await
        .map_err(|err| desktop_interactive_resolve_error(kind, err))
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SubmitGoalArgs {
    pub goal: String,
    #[serde(default)]
    pub max_turns: usize,
    #[serde(default)]
    pub provider: Option<String>,
    #[serde(default)]
    pub model: Option<String>,
    /// Thinking/reasoning level: "off", "low", "medium", "high", "xhigh"
    #[serde(default)]
    pub thinking_level: Option<String>,
    #[serde(default)]
    pub session_id: Option<String>,
    #[serde(default)]
    pub images: Vec<SubmitGoalImageInput>,
    #[serde(default)]
    pub auto_compact: Option<bool>,
    #[serde(default)]
    pub compaction_threshold: Option<u8>,
    #[serde(default)]
    pub compaction_provider: Option<String>,
    #[serde(default)]
    pub compaction_model: Option<String>,
    #[serde(default)]
    pub run_id: Option<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SubmitGoalImageInput {
    pub data: String,
    pub media_type: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SubmitGoalResult {
    pub success: bool,
    pub turns: usize,
    pub session_id: String,
}

/// Internal helper that runs the agent, streams events, tracks edits for undo,
/// handles approval/question forwarding, and manages the message queue.
fn parse_thinking_level(level_str: &str) -> ava_types::ThinkingLevel {
    match level_str {
        "off" => ava_types::ThinkingLevel::Off,
        "low" => ava_types::ThinkingLevel::Low,
        "medium" => ava_types::ThinkingLevel::Medium,
        "high" => ava_types::ThinkingLevel::High,
        "max" | "xhigh" => ava_types::ThinkingLevel::Max,
        _ => ava_types::ThinkingLevel::Off,
    }
}

fn map_submit_image_media_type(media_type: &str) -> Option<ava_types::ImageMediaType> {
    match media_type {
        "image/png" => Some(ava_types::ImageMediaType::Png),
        "image/jpeg" => Some(ava_types::ImageMediaType::Jpeg),
        "image/gif" => Some(ava_types::ImageMediaType::Gif),
        "image/webp" => Some(ava_types::ImageMediaType::WebP),
        _ => None,
    }
}

fn map_submit_goal_images(images: &[SubmitGoalImageInput]) -> Vec<ava_types::ImageContent> {
    images
        .iter()
        .filter_map(|image| {
            Some(ava_types::ImageContent::new(
                image.data.clone(),
                map_submit_image_media_type(&image.media_type)?,
            ))
        })
        .collect()
}

fn desktop_run_context_from_submit_args(args: &SubmitGoalArgs) -> AgentRunContext {
    let mut context = AgentRunContext {
        provider: args.provider.clone(),
        model: args.model.clone(),
        thinking_level: args.thinking_level.as_deref().map(parse_thinking_level),
        auto_compact: None,
        compaction_threshold: None,
        compaction_provider: None,
        compaction_model: None,
        todo_state: None,
        permission_context: None,
    };

    if args.auto_compact.is_some()
        || args.compaction_threshold.is_some()
        || args.compaction_provider.is_some()
        || args.compaction_model.is_some()
    {
        context.auto_compact = Some(args.auto_compact.unwrap_or(true));
        context.compaction_threshold = Some(args.compaction_threshold.unwrap_or(80));
        context.compaction_provider = args.compaction_provider.clone();
        context.compaction_model = args.compaction_model.clone();
    }

    context
}

fn desktop_run_context_with_state(
    mut context: AgentRunContext,
    run: &std::sync::Arc<crate::bridge::DesktopRunState>,
) -> AgentRunContext {
    context.todo_state = Some(run.todo_state.clone());
    context.permission_context = Some(run.permission_context.clone());
    context
}

fn desktop_run_context_from_session(session: &ava_types::Session) -> AgentRunContext {
    shared_run_context_from_session(session)
}

async fn desktop_effective_run_identity(
    bridge: &DesktopBridge,
    goal: &str,
    images: &[ava_types::ImageContent],
    run_context: &AgentRunContext,
) -> (String, String) {
    if let Ok(decision) = bridge
        .stack
        .resolve_model_route(goal, images, Some(run_context))
        .await
    {
        if bridge
            .stack
            .router
            .route_required(&decision.provider, &decision.model)
            .await
            .is_ok()
        {
            return (decision.provider, decision.model);
        }

        let cfg = bridge.stack.config.get().await;
        if let Some(fallback) = cfg.fallback {
            return (fallback.provider, fallback.model);
        }

        return (decision.provider, decision.model);
    }

    bridge.stack.current_model().await
}

async fn run_agent_inner(
    goal: &str,
    max_turns: usize,
    history: Vec<ava_types::Message>,
    images: Vec<ava_types::ImageContent>,
    session_id: Uuid,
    run_id: String,
    run: std::sync::Arc<crate::bridge::DesktopRunState>,
    app: &AppHandle,
    bridge: &DesktopBridge,
    run_context: Option<AgentRunContext>,
) -> Result<SubmitGoalResult, String> {
    info!(goal = %goal, run_id = %run_id, session_id = %session_id, "run_agent_inner: starting agent");

    let (message_queue, message_queue_tx, message_queue_control) =
        bridge.stack.create_message_queue_with_control();
    if let Some(messages) = bridge.deferred_queue.read().await.get(&session_id) {
        for message in messages.iter().cloned() {
            let _ = message_queue_tx.send(message);
        }
    }
    bridge
        .activate_message_queue(&run_id, message_queue_tx, message_queue_control)
        .await?;

    let (tx, mut rx) = mpsc::unbounded_channel();
    let app_clone = app.clone();
    let todo_state = run.todo_state.clone();
    let checkpoint_sm = bridge.stack.session_manager.clone();
    let checkpoint_last_id = bridge.last_session_id.clone();
    let deferred_queue = bridge.deferred_queue.clone();
    let in_flight_deferred = bridge.in_flight_deferred.clone();
    let bridge_for_forwarder = bridge.clone();
    let deferred_session_id = session_id;
    let event_run_id = run_id.clone();
    let todo_event_run_id = run_id.clone();
    let forwarder = tokio::spawn(async move {
        let mut last_tool_was_todo_write = false;
        while let Some(event) = rx.recv().await {
            if let ava_agent::agent_loop::AgentEvent::Checkpoint(ref session) = event {
                if let Err(e) =
                    save_session_checkpoint(checkpoint_sm.clone(), session.clone()).await
                {
                    tracing::error!("Failed to save session checkpoint: {e}");
                }
                *checkpoint_last_id.write().await = Some(session.id);
                continue;
            }

            if let ava_agent::agent_loop::AgentEvent::ToolCall(ref tc) = event {
                if tc.name == "edit" || tc.name == "write" {
                    if let Some(path) = tc.arguments.get("file_path").and_then(|v| v.as_str()) {
                        match tokio::fs::read_to_string(path).await {
                            Ok(content) => {
                                bridge_for_forwarder
                                    .push_edit(
                                        deferred_session_id,
                                        crate::bridge::FileEditRecord {
                                            file_path: path.to_string(),
                                            previous_content: content,
                                        },
                                    )
                                    .await;
                            }
                            Err(e) => {
                                tracing::warn!(
                                    "Failed to read file for edit tracking (undo will be unavailable \
                                     for this edit): path={path}, error={e}"
                                );
                            }
                        }
                    }
                }
                last_tool_was_todo_write = tc.name == "todo_write";
            } else if let ava_agent::agent_loop::AgentEvent::ToolResult(_) = event {
                if last_tool_was_todo_write {
                    last_tool_was_todo_write = false;
                    let items = todo_state.get();
                    let todos = items
                        .iter()
                        .map(|item| crate::events::TodoItemPayload {
                            content: item.content.clone(),
                            status: item.status.to_string(),
                            priority: item.priority.to_string(),
                        })
                        .collect();
                    if let Err(e) = app_clone.emit(
                        "agent-event",
                        AgentEvent::TodoUpdate {
                            todos,
                            run_id: Some(todo_event_run_id.clone()),
                        },
                    ) {
                        tracing::error!("Failed to emit todo_update event: {e}");
                    }
                }
            } else if !matches!(
                event,
                ava_agent::agent_loop::AgentEvent::SnapshotTaken { .. }
            ) {
                last_tool_was_todo_write = false;
            }

            if let ava_agent::agent_loop::AgentEvent::Progress(ref message) = event {
                sync_deferred_queues_for_progress(
                    message,
                    deferred_session_id,
                    &deferred_queue,
                    &in_flight_deferred,
                )
                .await;
            }
            emit_backend_event(&app_clone, &event, Some(event_run_id.as_str()));
        }
    });

    let result = bridge
        .stack
        .run_with_context(
            goal,
            max_turns,
            Some(tx),
            run.cancel.clone(),
            history,
            Some(message_queue),
            images,
            Some(session_id),
            Some(run_id.clone()),
            run_context,
        )
        .await;

    bridge.clear_message_queue_dispatch(&run_id).await;
    let _ = forwarder.await;

    match &result {
        Ok(run) => info!(
            success = run.success,
            turns = run.turns,
            session_id = %run.session.id,
            "run_agent_inner: agent completed"
        ),
        Err(error) => tracing::warn!(error = %error, "run_agent_inner: agent failed"),
    }

    let run_result: Result<SubmitGoalResult, String> = match result {
        Ok(run_result) => {
            clear_preserved_deferred(
                session_id,
                &bridge.deferred_queue,
                &bridge.in_flight_deferred,
            )
            .await;
            let _ = save_session_checkpoint(
                bridge.stack.session_manager.clone(),
                run_result.session.clone(),
            )
            .await;
            *bridge.last_session_id.write().await = Some(run_result.session.id);
            Ok(SubmitGoalResult {
                success: run_result.success,
                turns: run_result.turns,
                session_id: run_result.session.id.to_string(),
            })
        }
        Err(e) => {
            restore_in_flight_deferred(
                session_id,
                &bridge.deferred_queue,
                &bridge.in_flight_deferred,
            )
            .await;
            Err(e.to_string())
        }
    };

    bridge.finish_run(&run_id).await;
    run_result
}

/// Submit a goal to the agent. Streams events via `agent-event` and returns
/// when the agent completes or is cancelled.
#[tauri::command]
pub async fn submit_goal(
    args: SubmitGoalArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    let _startup_guard = bridge.startup_lock.lock().await;

    let run_context = desktop_run_context_from_submit_args(&args);
    let images = map_submit_goal_images(&args.images);

    let requested_session_id = args
        .session_id
        .as_deref()
        .map(Uuid::parse_str)
        .transpose()
        .map_err(|e| format!("invalid session id: {e}"))?;
    let last_session_id = *bridge.last_session_id.read().await;
    let run_session =
        resolve_session_precedence(requested_session_id, last_session_id, Uuid::new_v4);

    // Load conversation history from the previous session (if any) so the
    // agent has context from prior turns in this desktop session.
    let history = if run_session.source == SessionSelectionSource::New {
        vec![]
    } else {
        bridge
            .stack
            .session_manager
            .get(run_session.session_id)
            .ok()
            .flatten()
            .map(|s| s.messages)
            .unwrap_or_default()
    };

    let run_id = ensure_desktop_run_id(args.run_id);
    let (provider, model) =
        desktop_effective_run_identity(&bridge, &args.goal, &images, &run_context).await;
    let run = bridge
        .register_run(run_id.clone(), run_session.session_id, provider, model)
        .await?;
    let run_context = desktop_run_context_with_state(run_context, &run);
    drop(_startup_guard);

    run_agent_inner(
        &args.goal,
        args.max_turns,
        history,
        images,
        run_session.session_id,
        run_id,
        run,
        &app,
        &bridge,
        Some(run_context),
    )
    .await
}

#[derive(Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct RunCorrelationArgs {
    #[serde(default)]
    pub run_id: Option<String>,
    #[serde(default)]
    pub session_id: Option<String>,
}

fn parse_optional_run_session_id(session_id: Option<&str>) -> Result<Option<Uuid>, String> {
    session_id
        .map(Uuid::parse_str)
        .transpose()
        .map_err(|e| format!("Invalid session id: {e}"))
}

/// Cancel the currently-running agent.
#[tauri::command]
pub async fn cancel_agent(
    args: RunCorrelationArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    let requested_session_id = parse_optional_run_session_id(args.session_id.as_deref())?;
    let run = bridge
        .resolve_run(args.run_id.as_deref(), requested_session_id)
        .await?;
    cancel_desktop_run(&app, &bridge, &run.run_id).await;
    Ok(())
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentStatus {
    pub running: bool,
    pub provider: String,
    pub model: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub run_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pending_approval: Option<crate::events::AgentEvent>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pending_question: Option<crate::events::AgentEvent>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pending_plan: Option<crate::events::AgentEvent>,
}

/// Get current agent status (running, provider, model).
#[tauri::command]
pub async fn get_agent_status(
    args: RunCorrelationArgs,
    bridge: State<'_, DesktopBridge>,
) -> Result<AgentStatus, String> {
    let requested_session_id = parse_optional_run_session_id(args.session_id.as_deref())?;
    let scoped_lookup = args.run_id.is_some() || requested_session_id.is_some();
    let run_id = if scoped_lookup {
        bridge
            .resolve_run(args.run_id.as_deref(), requested_session_id)
            .await
            .map(|run| Some(run.run_id.clone()))
            .or_else(|message| {
                if is_inactive_scoped_status_lookup(
                    args.run_id.as_deref(),
                    requested_session_id,
                    &message,
                ) {
                    Ok(None)
                } else {
                    Err(message)
                }
            })?
    } else {
        bridge.single_active_run_id().await
    };
    let (provider, model) = match run_id.as_deref() {
        Some(active_run_id) => {
            let run = bridge.resolve_run(Some(active_run_id), None).await?;
            (run.provider.clone(), run.model.clone())
        }
        None => bridge.stack.current_model().await,
    };
    let pending_approval = match run_id.as_deref() {
        Some(active_run_id) => {
            bridge
                .current_interactive_request_event(
                    InteractiveRequestKind::Approval,
                    Some(active_run_id),
                )
                .await
        }
        None => None,
    };
    let pending_question = match run_id.as_deref() {
        Some(active_run_id) => {
            bridge
                .current_interactive_request_event(
                    InteractiveRequestKind::Question,
                    Some(active_run_id),
                )
                .await
        }
        None => None,
    };
    let pending_plan = match run_id.as_deref() {
        Some(active_run_id) => {
            bridge
                .current_interactive_request_event(
                    InteractiveRequestKind::Plan,
                    Some(active_run_id),
                )
                .await
        }
        None => None,
    };
    Ok(AgentStatus {
        running: if scoped_lookup {
            run_id.is_some()
        } else {
            run_id.is_some() || bridge.has_active_runs().await
        },
        provider,
        model,
        run_id,
        pending_approval,
        pending_question,
        pending_plan,
    })
}

// ============================================================================
// Approval / Question resolution
// ============================================================================

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ResolveApprovalArgs {
    pub approved: bool,
    #[serde(default)]
    pub always_allow: bool,
    #[serde(default)]
    pub request_id: Option<String>,
}

/// Resolve a pending tool approval request.
#[tauri::command]
pub async fn resolve_approval(
    args: ResolveApprovalArgs,
    bridge: State<'_, DesktopBridge>,
    app: AppHandle,
) -> Result<(), String> {
    let _interactive_guard = bridge.interactive_lifecycle_lock.lock().await;
    let reply = take_resolved_desktop_interactive_request(
        &bridge.pending_approval_reply,
        "approval",
        args.request_id.as_deref(),
    )
    .await?;

    let approval = if args.approved {
        if args.always_allow {
            ToolApproval::AllowAlways
        } else {
            ToolApproval::AllowedForSession
        }
    } else {
        ToolApproval::Rejected(Some("User denied via desktop UI".to_string()))
    };

    if reply.reply.send(approval).is_err() {
        bridge
            .discard_deferred_interactive_request_event(&reply.handle.request_id)
            .await;
        emit_interactive_request_cleared(
            &app,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        emit_promoted_desktop_interactive_request(
            &app,
            &bridge,
            reply.handle.kind,
            reply.handle.run_id.as_deref(),
        )
        .await;
        return Err(
            "Failed to send approval response — the agent may have already moved on".to_string(),
        );
    }

    bridge
        .discard_deferred_interactive_request_event(&reply.handle.request_id)
        .await;
    emit_interactive_request_cleared(
        &app,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );
    emit_promoted_desktop_interactive_request(
        &app,
        &bridge,
        reply.handle.kind,
        reply.handle.run_id.as_deref(),
    )
    .await;

    Ok(())
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ResolveQuestionArgs {
    pub answer: String,
    #[serde(default)]
    pub request_id: Option<String>,
}

/// Resolve a pending question request.
#[tauri::command]
pub async fn resolve_question(
    args: ResolveQuestionArgs,
    bridge: State<'_, DesktopBridge>,
    app: AppHandle,
) -> Result<(), String> {
    let _interactive_guard = bridge.interactive_lifecycle_lock.lock().await;
    let reply = take_resolved_desktop_interactive_request(
        &bridge.pending_question_reply,
        "question",
        args.request_id.as_deref(),
    )
    .await?;

    if reply.reply.send(args.answer).is_err() {
        bridge
            .discard_deferred_interactive_request_event(&reply.handle.request_id)
            .await;
        emit_interactive_request_cleared(
            &app,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        emit_promoted_desktop_interactive_request(
            &app,
            &bridge,
            reply.handle.kind,
            reply.handle.run_id.as_deref(),
        )
        .await;
        return Err(
            "Failed to send question response — the agent may have already moved on".to_string(),
        );
    }

    bridge
        .discard_deferred_interactive_request_event(&reply.handle.request_id)
        .await;
    emit_interactive_request_cleared(
        &app,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );
    emit_promoted_desktop_interactive_request(
        &app,
        &bridge,
        reply.handle.kind,
        reply.handle.run_id.as_deref(),
    )
    .await;

    Ok(())
}

// ============================================================================
// Plan resolution
// ============================================================================

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ResolvePlanArgs {
    pub response: String,
    #[serde(default)]
    pub request_id: Option<String>,
    #[serde(default)]
    pub modified_plan: Option<serde_json::Value>,
    #[serde(default)]
    pub feedback: Option<String>,
    #[allow(dead_code)] // Deserialized from the frontend; not yet consumed in Rust plan handling.
    #[serde(default)]
    pub step_comments: Option<std::collections::HashMap<String, String>>,
}

/// Resolve a pending plan approval request.
#[tauri::command]
pub async fn resolve_plan(
    args: ResolvePlanArgs,
    bridge: State<'_, DesktopBridge>,
    app: AppHandle,
) -> Result<(), String> {
    let decision = parse_plan_decision(&args.response, args.modified_plan, args.feedback)?;
    let _interactive_guard = bridge.interactive_lifecycle_lock.lock().await;

    let reply = take_resolved_desktop_interactive_request(
        &bridge.pending_plan_reply,
        "plan",
        args.request_id.as_deref(),
    )
    .await?;

    if reply.reply.send(decision).is_err() {
        bridge
            .discard_deferred_interactive_request_event(&reply.handle.request_id)
            .await;
        emit_interactive_request_cleared(
            &app,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        emit_promoted_desktop_interactive_request(
            &app,
            &bridge,
            reply.handle.kind,
            reply.handle.run_id.as_deref(),
        )
        .await;
        return Err(
            "Failed to send plan response — the agent may have already moved on".to_string(),
        );
    }

    bridge
        .discard_deferred_interactive_request_event(&reply.handle.request_id)
        .await;
    emit_interactive_request_cleared(
        &app,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );
    emit_promoted_desktop_interactive_request(
        &app,
        &bridge,
        reply.handle.kind,
        reply.handle.run_id.as_deref(),
    )
    .await;

    Ok(())
}

// ============================================================================
// Mid-stream messaging commands
// ============================================================================

#[derive(Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SteerArgs {
    pub message: String,
    #[serde(default)]
    pub run_id: Option<String>,
    #[serde(default)]
    pub session_id: Option<String>,
}

/// Inject a steering message (Tier 1).
#[tauri::command]
pub async fn steer_agent(args: SteerArgs, bridge: State<'_, DesktopBridge>) -> Result<(), String> {
    if args.message.is_empty() {
        return Err("Steering message must not be empty.".to_string());
    }
    let requested_session_id = parse_optional_queue_session_id(args.session_id.as_deref())?;
    bridge
        .enqueue_message(
            ava_types::QueuedMessage {
                text: args.message,
                tier: queue_message_tier(ControlPlaneCommand::SteerAgent, None)
                    .expect("steer command should map to a queue tier"),
            },
            args.run_id.as_deref(),
            requested_session_id,
            false,
        )
        .await
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FollowUpArgs {
    pub message: String,
    #[serde(default)]
    pub run_id: Option<String>,
    #[serde(default)]
    pub session_id: Option<String>,
}

/// Queue a follow-up message (Tier 2).
#[tauri::command]
pub async fn follow_up_agent(
    args: FollowUpArgs,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if args.message.is_empty() {
        return Err("Follow-up message must not be empty.".to_string());
    }
    let requested_session_id = parse_optional_queue_session_id(args.session_id.as_deref())?;

    bridge
        .enqueue_message(
            ava_types::QueuedMessage {
                text: args.message,
                tier: queue_message_tier(ControlPlaneCommand::FollowUpAgent, None)
                    .expect("follow-up command should map to a queue tier"),
            },
            args.run_id.as_deref(),
            requested_session_id,
            true,
        )
        .await
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PostCompleteArgs {
    pub message: String,
    #[serde(default = "default_group")]
    pub group: u32,
    #[serde(default)]
    pub run_id: Option<String>,
    #[serde(default)]
    pub session_id: Option<String>,
}

fn default_group() -> u32 {
    1
}

fn parse_optional_queue_session_id(session_id: Option<&str>) -> Result<Option<Uuid>, String> {
    session_id
        .map(Uuid::parse_str)
        .transpose()
        .map_err(|error| format!("Invalid session id: {error}"))
}

fn parse_plan_decision(
    response: &str,
    modified_plan: Option<serde_json::Value>,
    feedback: Option<String>,
) -> Result<ava_types::PlanDecision, String> {
    let feedback = feedback.unwrap_or_default();

    match response {
        "approved" => Ok(ava_types::PlanDecision::Approved),
        "rejected" => Ok(ava_types::PlanDecision::Rejected { feedback }),
        "modified" => {
            let plan: ava_types::Plan = modified_plan
                .ok_or_else(|| "Modified plan is required for 'modified' response".to_string())
                .and_then(|v| {
                    serde_json::from_value(v).map_err(|e| format!("Invalid modified plan: {e}"))
                })?;
            Ok(ava_types::PlanDecision::Modified { plan, feedback })
        }
        other => Err(format!(
            "Invalid plan response: '{other}'. Expected 'approved', 'rejected', or 'modified'"
        )),
    }
}

/// Queue a post-complete message (Tier 3).
#[tauri::command]
pub async fn post_complete_agent(
    args: PostCompleteArgs,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if args.message.is_empty() {
        return Err("Post-complete message must not be empty.".to_string());
    }
    let requested_session_id = parse_optional_queue_session_id(args.session_id.as_deref())?;

    bridge
        .enqueue_message(
            ava_types::QueuedMessage {
                text: args.message,
                tier: queue_message_tier(ControlPlaneCommand::PostCompleteAgent, Some(args.group))
                    .expect("post-complete command should map to a queue tier"),
            },
            args.run_id.as_deref(),
            requested_session_id,
            true,
        )
        .await
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MessageQueueState {
    pub active: bool,
}

/// Get the current message queue state.
#[tauri::command]
pub async fn get_message_queue(
    args: RunCorrelationArgs,
    bridge: State<'_, DesktopBridge>,
) -> Result<MessageQueueState, String> {
    let requested_session_id = parse_optional_run_session_id(args.session_id.as_deref())?;
    let snapshot = if args.run_id.is_some() || requested_session_id.is_some() {
        bridge
            .queue_dispatch_snapshot(args.run_id.as_deref(), requested_session_id)
            .await?
    } else {
        match bridge.resolve_run(None, None).await {
            Ok(run) => {
                bridge
                    .queue_dispatch_snapshot(Some(&run.run_id), None)
                    .await?
            }
            Err(message) if message == "No active desktop runs" => None,
            Err(message) => return Err(message),
        }
    };
    Ok(MessageQueueState {
        active: snapshot
            .as_ref()
            .is_some_and(|snapshot| snapshot.accepting && snapshot.tx.is_some()),
    })
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ClearMessageQueueArgs {
    pub target: ClearQueueTarget,
    #[serde(default)]
    pub run_id: Option<String>,
    #[serde(default)]
    pub session_id: Option<String>,
}

/// Clear messages from the queue.
#[tauri::command]
pub async fn clear_message_queue(
    args: ClearMessageQueueArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    match clear_queue_semantics(args.target) {
        QueueClearSemantics::CancelRunAndClearSteering => {
            let requested_session_id = parse_optional_run_session_id(args.session_id.as_deref())?;
            let run = bridge
                .resolve_run(args.run_id.as_deref(), requested_session_id)
                .await?;
            cancel_desktop_run(&app, &bridge, &run.run_id).await;
            Ok(())
        }
        QueueClearSemantics::Unsupported => Err(UNSUPPORTED_QUEUE_CLEAR_ERROR.to_string()),
    }
}

// ============================================================================
// Retry / Edit+Resend / Regenerate / Undo
// ============================================================================

/// Retry the last user message.
#[tauri::command]
pub async fn retry_last_message(
    args: RunCorrelationArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    let session_id = args
        .session_id
        .as_deref()
        .map(Uuid::parse_str)
        .transpose()
        .map_err(|e| format!("Invalid session id: {e}"))?
        .or(*bridge.last_session_id.read().await)
        .ok_or_else(|| "No previous session to retry".to_string())?;

    let session = bridge
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| e.to_string())?
        .ok_or_else(|| format!("Session {session_id} not found"))?;

    let SessionPromptContext {
        goal,
        history,
        images,
    } = build_retry_replay_payload(&session).map_err(|error| error.to_string())?;

    info!(goal = %goal, session_id = %session_id, "retry_last_message");
    let run_id = ensure_desktop_run_id(args.run_id);
    let _startup_guard = bridge.startup_lock.lock().await;
    let run_context = desktop_run_context_from_session(&session);
    let (provider, model) =
        desktop_effective_run_identity(&bridge, &goal, &images, &run_context).await;
    let run = bridge
        .register_run(run_id.clone(), session_id, provider, model)
        .await?;
    let run_context = desktop_run_context_with_state(run_context, &run);
    drop(_startup_guard);

    run_agent_inner(
        &goal,
        0,
        history,
        images,
        session_id,
        run_id,
        run,
        &app,
        &bridge,
        Some(run_context),
    )
    .await
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EditAndResendArgs {
    pub message_id: String,
    pub new_content: String,
    #[serde(default)]
    pub run_id: Option<String>,
    #[serde(default)]
    pub session_id: Option<String>,
}

/// Edit a specific message and re-run the agent from that point.
#[tauri::command]
pub async fn edit_and_resend(
    args: EditAndResendArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    let session_id = args
        .session_id
        .as_deref()
        .map(Uuid::parse_str)
        .transpose()
        .map_err(|e| format!("Invalid session id: {e}"))?
        .or(*bridge.last_session_id.read().await)
        .ok_or_else(|| "No previous session to edit".to_string())?;

    let session = bridge
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| e.to_string())?
        .ok_or_else(|| format!("Session {session_id} not found"))?;

    let target_id =
        Uuid::parse_str(&args.message_id).map_err(|e| format!("Invalid message ID: {e}"))?;

    let SessionPromptContext {
        goal,
        history,
        images,
    } = build_edit_replay_payload(&session, Some(target_id), args.new_content)
        .map_err(|error| error.to_string())?;

    info!(new_content = %goal, message_id = %args.message_id, "edit_and_resend");
    let run_id = ensure_desktop_run_id(args.run_id);
    let _startup_guard = bridge.startup_lock.lock().await;
    let run_context = desktop_run_context_from_session(&session);
    let (provider, model) =
        desktop_effective_run_identity(&bridge, &goal, &images, &run_context).await;
    let run = bridge
        .register_run(run_id.clone(), session_id, provider, model)
        .await?;
    let run_context = desktop_run_context_with_state(run_context, &run);
    drop(_startup_guard);

    run_agent_inner(
        &goal,
        0,
        history,
        images,
        session_id,
        run_id,
        run,
        &app,
        &bridge,
        Some(run_context),
    )
    .await
}

/// Regenerate the last assistant response.
#[tauri::command]
pub async fn regenerate_response(
    args: RunCorrelationArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    let session_id = args
        .session_id
        .as_deref()
        .map(Uuid::parse_str)
        .transpose()
        .map_err(|e| format!("Invalid session id: {e}"))?
        .or(*bridge.last_session_id.read().await)
        .ok_or_else(|| "No previous session to regenerate".to_string())?;

    let session = bridge
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| e.to_string())?
        .ok_or_else(|| format!("Session {session_id} not found"))?;

    let SessionPromptContext {
        goal,
        history,
        images,
    } = build_regenerate_replay_payload(&session).map_err(|error| error.to_string())?;

    info!(goal = %goal, session_id = %session_id, "regenerate_response");
    let run_id = ensure_desktop_run_id(args.run_id);
    let _startup_guard = bridge.startup_lock.lock().await;
    let run_context = desktop_run_context_from_session(&session);
    let (provider, model) =
        desktop_effective_run_identity(&bridge, &goal, &images, &run_context).await;
    let run = bridge
        .register_run(run_id.clone(), session_id, provider, model)
        .await?;
    let run_context = desktop_run_context_with_state(run_context, &run);
    drop(_startup_guard);

    run_agent_inner(
        &goal,
        0,
        history,
        images,
        session_id,
        run_id,
        run,
        &app,
        &bridge,
        Some(run_context),
    )
    .await
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct UndoResult {
    pub success: bool,
    pub message: String,
    pub file_path: Option<String>,
}

/// Undo the last file edit made by the agent.
#[tauri::command]
pub async fn undo_last_edit(
    args: Option<RunCorrelationArgs>,
    bridge: State<'_, DesktopBridge>,
) -> Result<UndoResult, String> {
    let args = args.unwrap_or_default();
    let requested_session_id = parse_optional_run_session_id(args.session_id.as_deref())?;
    let session_id = if let Some(session_id) = requested_session_id {
        if let Some(run_id) = args.run_id.as_deref() {
            if let Ok(run) = bridge.resolve_run(Some(run_id), None).await {
                if run.session_id != session_id {
                    return Err(format!("Run {run_id} does not own session {session_id}"));
                }
            }
        }
        session_id
    } else if let Some(run_id) = args.run_id.as_deref() {
        bridge
            .resolve_run(Some(run_id), requested_session_id)
            .await?
            .session_id
    } else {
        (*bridge.last_session_id.read().await)
            .ok_or_else(|| "No previous session to undo".to_string())?
    };

    let record = bridge.pop_last_edit(session_id).await;

    match record {
        Some(edit) => {
            let path = edit.file_path.clone();
            match tokio::fs::write(&edit.file_path, &edit.previous_content).await {
                Ok(()) => {
                    info!(file = %path, "undo_last_edit: restored file");
                    Ok(UndoResult {
                        success: true,
                        message: format!("Restored {path} to its previous content"),
                        file_path: Some(path),
                    })
                }
                Err(e) => Ok(UndoResult {
                    success: false,
                    message: format!("Failed to restore {path}: {e}"),
                    file_path: Some(path),
                }),
            }
        }
        None => Ok(UndoResult {
            success: false,
            message: "No file edits to undo".to_string(),
            file_path: None,
        }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_image(label: &str) -> ava_types::ImageContent {
        ava_types::ImageContent::new(label, ava_types::ImageMediaType::Png)
    }

    #[test]
    fn map_submit_goal_images_maps_supported_media_types() {
        let images = map_submit_goal_images(&[SubmitGoalImageInput {
            data: "base64-image".to_string(),
            media_type: "image/png".to_string(),
        }]);

        assert_eq!(images, vec![sample_image("base64-image")]);
    }

    #[test]
    fn map_submit_goal_images_skips_unsupported_media_types() {
        let images = map_submit_goal_images(&[
            SubmitGoalImageInput {
                data: "good".to_string(),
                media_type: "image/png".to_string(),
            },
            SubmitGoalImageInput {
                data: "bad".to_string(),
                media_type: "image/tiff".to_string(),
            },
        ]);

        assert_eq!(images, vec![sample_image("good")]);
    }

    #[test]
    fn ensure_desktop_run_id_generates_stable_prefixed_id() {
        let generated = ensure_desktop_run_id(None);
        assert!(generated.starts_with("desktop-run-"));
        assert_eq!(
            ensure_desktop_run_id(Some("desktop-run-existing".to_string())),
            "desktop-run-existing"
        );
    }

    fn sample_user_message(
        content: &str,
        images: Vec<ava_types::ImageContent>,
    ) -> ava_types::Message {
        let mut message = ava_types::Message::new(ava_types::Role::User, content);
        message.images = images;
        message
    }

    #[tokio::test]
    async fn take_matching_pending_reply_only_consumes_matching_request_ids() {
        let (matching_tx, _matching_rx) = tokio::sync::oneshot::channel::<String>();
        let store = crate::bridge::PendingQuestionReply::new(
            ava_agent::control_plane::interactive::InteractiveRequestKind::Question,
        );
        let handle = store.register(matching_tx).await;

        let error = match store.resolve(Some("request-2")).await {
            Ok(_) => panic!("stale request should fail"),
            Err(error) => error,
        };
        assert_eq!(
            desktop_interactive_resolve_error("question", error),
            "No matching pending question request to resolve"
        );
        assert_eq!(
            store.current_request_id().await,
            Some(handle.request_id.clone())
        );
        assert!(store.resolve(Some(&handle.request_id)).await.is_ok());
        assert!(store.current_request_id().await.is_none());
    }

    #[test]
    fn parse_plan_decision_rejects_modified_without_plan() {
        let error = parse_plan_decision("modified", None, None).expect_err("missing plan");
        assert!(error.contains("Modified plan is required"));
    }

    #[tokio::test]
    async fn plan_timeout_cleanup_consumes_pending_reply_and_auto_rejects() {
        let (reply_tx, reply_rx) = tokio::sync::oneshot::channel::<ava_types::PlanDecision>();
        let store = crate::bridge::PendingPlanReply::new(
            ava_agent::control_plane::interactive::InteractiveRequestKind::Plan,
        );
        let handle = store.register(reply_tx).await;

        let timeout_reply = store
            .timeout_request(&handle.request_id)
            .await
            .expect("pending plan reply");

        timeout_reply
            .reply
            .send(ava_types::PlanDecision::Rejected {
                feedback: "Timed out waiting for plan response in desktop UI".to_string(),
            })
            .expect("timeout rejection should send");

        assert_eq!(
            timeout_reply.handle.phase,
            ava_agent::control_plane::interactive::InteractiveRequestPhase::TimedOut
        );
        assert!(store.current_request_id().await.is_none());
        match reply_rx.await.expect("timeout reply should arrive") {
            ava_types::PlanDecision::Rejected { feedback } => {
                assert!(feedback.contains("Timed out waiting for plan response in desktop UI"));
            }
            other => panic!("expected rejected timeout decision, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn stale_late_plan_responses_do_not_consume_newer_pending_requests() {
        let store = crate::bridge::PendingPlanReply::new(
            ava_agent::control_plane::interactive::InteractiveRequestKind::Plan,
        );
        let (first_tx, _first_rx) = tokio::sync::oneshot::channel::<ava_types::PlanDecision>();
        let first = store.register(first_tx).await;

        let _ = store
            .resolve(Some(&first.request_id))
            .await
            .expect("first plan reply");

        let (second_tx, _second_rx) = tokio::sync::oneshot::channel::<ava_types::PlanDecision>();
        let second = store.register(second_tx).await;

        let error = match store.resolve(Some(&first.request_id)).await {
            Ok(_) => panic!("stale request should fail"),
            Err(error) => error,
        };
        assert_eq!(
            desktop_interactive_resolve_error("plan", error),
            "No matching pending plan request to resolve"
        );
        assert_eq!(
            store.current_request_id().await,
            Some(second.request_id.clone())
        );
        assert!(store.resolve(Some(&second.request_id)).await.is_ok());
        assert!(store.current_request_id().await.is_none());
    }

    #[test]
    fn desktop_interactive_resolve_error_messages_preserve_missing_and_stale_text() {
        assert_eq!(
            desktop_interactive_resolve_error(
                "approval",
                ResolveInteractiveRequestError::MissingPendingRequest {
                    kind: ava_agent::control_plane::interactive::InteractiveRequestKind::Approval,
                },
            ),
            "No pending approval request to resolve"
        );
        assert_eq!(
            desktop_interactive_resolve_error(
                "approval",
                ResolveInteractiveRequestError::StaleRequestId {
                    kind: ava_agent::control_plane::interactive::InteractiveRequestKind::Approval,
                    request_id: "approval-stale".to_string(),
                    current_request_id: "approval-current".to_string(),
                },
            ),
            "No matching pending approval request to resolve"
        );
    }

    #[tokio::test]
    async fn missing_request_ids_are_rejected_without_consuming_pending_requests() {
        let store = crate::bridge::PendingApprovalReply::new(
            ava_agent::control_plane::interactive::InteractiveRequestKind::Approval,
        );
        let (reply_tx, _reply_rx) = tokio::sync::oneshot::channel::<ToolApproval>();
        let handle = store.register(reply_tx).await;

        let error = match take_resolved_desktop_interactive_request(&store, "approval", None).await
        {
            Ok(_) => panic!("missing request_id should fail"),
            Err(error) => error,
        };

        assert_eq!(
            error,
            "request_id is required to resolve pending approval request"
        );
        assert_eq!(
            store.current_request_id().await,
            Some(handle.request_id.clone())
        );

        let resolved =
            take_resolved_desktop_interactive_request(&store, "approval", Some(&handle.request_id))
                .await
                .expect("current request should resolve after missing-id rejection");
        assert_eq!(resolved.handle.request_id, handle.request_id);
        assert!(store.current_request_id().await.is_none());
    }

    #[test]
    fn interactive_request_cleared_event_preserves_timeout_and_run_correlation() {
        let payload = interactive_request_cleared_event(
            "question-1",
            "question",
            true,
            Some("desktop-run-42"),
        );

        match payload {
            AgentEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
                run_id,
            } => {
                assert_eq!(request_id, "question-1");
                assert_eq!(request_kind, "question");
                assert!(timed_out);
                assert_eq!(run_id.as_deref(), Some("desktop-run-42"));
            }
            _ => panic!("expected interactive clear payload"),
        }
    }

    #[tokio::test]
    async fn interactive_request_cleared_event_smokes_run_id_from_registered_handle() {
        let (reply_tx, _reply_rx) = tokio::sync::oneshot::channel::<String>();
        let store = crate::bridge::PendingQuestionReply::new(
            ava_agent::control_plane::interactive::InteractiveRequestKind::Question,
        );
        let handle = store
            .register_with_run_id(reply_tx, Some("desktop-run-smoke".to_string()))
            .await;

        match interactive_request_cleared_event(
            &handle.request_id,
            handle.kind.as_str(),
            false,
            handle.run_id.as_deref(),
        ) {
            AgentEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
                run_id,
            } => {
                assert_eq!(request_id, handle.request_id);
                assert_eq!(request_kind, "question");
                assert!(!timed_out);
                assert_eq!(run_id.as_deref(), Some("desktop-run-smoke"));
            }
            _ => panic!("expected interactive clear payload"),
        }
    }

    #[tokio::test]
    async fn failed_desktop_question_response_still_leaves_next_request_promotable() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = crate::bridge::DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");

        let (first_tx, first_rx) = tokio::sync::oneshot::channel::<String>();
        let first = bridge
            .pending_question_reply
            .register_with_run_id(first_tx, Some("desktop-run-a".to_string()))
            .await;
        drop(first_rx);
        let (second_tx, _second_rx) = tokio::sync::oneshot::channel::<String>();
        let second = bridge
            .pending_question_reply
            .register_with_run_id(second_tx, Some("desktop-run-b".to_string()))
            .await;

        bridge.deferred_interactive_events.lock().await.insert(
            first.request_id.clone(),
            AgentEvent::QuestionRequest {
                id: first.request_id.clone(),
                question: "Question A".to_string(),
                options: vec![],
                run_id: first.run_id.clone(),
            },
        );
        bridge.deferred_interactive_events.lock().await.insert(
            second.request_id.clone(),
            AgentEvent::QuestionRequest {
                id: second.request_id.clone(),
                question: "Question B".to_string(),
                options: vec![],
                run_id: second.run_id.clone(),
            },
        );

        let reply = take_resolved_desktop_interactive_request(
            &bridge.pending_question_reply,
            "question",
            Some(&first.request_id),
        )
        .await
        .expect("current request should resolve");

        assert!(reply.reply.send("late answer".to_string()).is_err());
        bridge
            .discard_deferred_interactive_request_event(&reply.handle.request_id)
            .await;

        let promoted = bridge
            .take_promoted_interactive_request_event(
                ava_agent::control_plane::interactive::InteractiveRequestKind::Question,
                reply.handle.run_id.as_deref(),
            )
            .await
            .expect("next queued question should still be promotable");

        match promoted {
            AgentEvent::QuestionRequest { id, run_id, .. } => {
                assert_eq!(id, second.request_id);
                assert_eq!(run_id.as_deref(), Some("desktop-run-b"));
            }
            _ => panic!("expected promoted question request"),
        }
    }

    #[test]
    fn unsupported_queue_clear_error_mentions_unimplemented_targets() {
        assert!(UNSUPPORTED_QUEUE_CLEAR_ERROR.contains("not supported yet"));
    }

    #[test]
    fn inactive_scoped_status_lookup_treats_missing_scoped_runs_as_non_errors() {
        let session_id = Uuid::new_v4();
        assert!(is_inactive_scoped_status_lookup(
            Some("desktop-run-a"),
            Some(session_id),
            &format!("Run {} is not active", "desktop-run-a")
        ));
        assert!(is_inactive_scoped_status_lookup(
            None,
            Some(session_id),
            &format!("Session {session_id} does not have an active run")
        ));
        assert!(!is_inactive_scoped_status_lookup(
            Some("desktop-run-a"),
            Some(session_id),
            &format!("Run {} does not own session {session_id}", "desktop-run-a")
        ));
    }

    #[test]
    fn retry_replay_input_preserves_user_images() {
        let mut session = ava_types::Session::new();
        session.add_message(sample_user_message("describe", vec![sample_image("retry")]));

        let SessionPromptContext { images, .. } =
            build_retry_replay_payload(&session).expect("retry replay input");
        assert_eq!(images, vec![sample_image("retry")]);
    }

    #[test]
    fn edit_replay_input_preserves_target_images() {
        let mut session = ava_types::Session::new();
        let target = sample_user_message("before", vec![sample_image("edit")]);
        let target_id = target.id;
        session.add_message(target);

        let SessionPromptContext { images, .. } =
            build_edit_replay_payload(&session, Some(target_id), "after".to_string())
                .expect("edit input");
        assert_eq!(images, vec![sample_image("edit")]);
    }

    #[test]
    fn edit_replay_input_rejects_non_user_targets() {
        let mut session = ava_types::Session::new();
        let assistant = ava_types::Message::new(ava_types::Role::Assistant, "done");
        let assistant_id = assistant.id;
        session.add_message(assistant);

        let error = build_edit_replay_payload(&session, Some(assistant_id), "after".to_string())
            .expect_err("assistant target should fail");
        assert_eq!(error.to_string(), "Only user messages can be edited");
    }

    #[test]
    fn regenerate_replay_input_preserves_last_user_images() {
        let mut session = ava_types::Session::new();
        session.add_message(sample_user_message("before", vec![sample_image("regen")]));
        session.add_message(ava_types::Message::new(ava_types::Role::Assistant, "done"));

        let SessionPromptContext { images, .. } =
            build_regenerate_replay_payload(&session).expect("regen input");
        assert_eq!(images, vec![sample_image("regen")]);
    }

    #[test]
    fn desktop_run_context_from_session_recovers_effective_run_settings() {
        let session = ava_types::Session::new().with_metadata(serde_json::json!({
            "runContext": {
                "provider": "openai",
                "model": "gpt-5.4",
                "thinkingLevel": "high",
                "autoCompact": true,
                "compactionThreshold": 72,
                "compactionProvider": "anthropic",
                "compactionModel": "claude-sonnet-4.6"
            }
        }));

        let context = desktop_run_context_from_session(&session);

        assert_eq!(context.provider.as_deref(), Some("openai"));
        assert_eq!(context.model.as_deref(), Some("gpt-5.4"));
        assert_eq!(context.thinking_level, Some(ava_types::ThinkingLevel::High));
        assert_eq!(context.auto_compact, Some(true));
        assert_eq!(context.compaction_threshold, Some(72));
        assert_eq!(context.compaction_provider.as_deref(), Some("anthropic"));
        assert_eq!(
            context.compaction_model.as_deref(),
            Some("claude-sonnet-4.6")
        );
    }
}
