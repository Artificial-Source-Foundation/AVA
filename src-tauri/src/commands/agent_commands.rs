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
    InteractiveRequestStore, ResolveInteractiveRequestError, TerminalInteractiveRequest,
};
use ava_agent::control_plane::queue::{
    clear_queue_semantics, ClearQueueTarget, QueueClearSemantics, UNSUPPORTED_QUEUE_CLEAR_ERROR,
};
use ava_agent::control_plane::sessions::{
    build_edit_replay_payload, build_regenerate_replay_payload, build_retry_replay_payload,
    resolve_session_precedence, SessionPromptContext, SessionSelectionSource,
};
use ava_tools::permission_middleware::ToolApproval;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, VecDeque};
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

fn queued_post_complete_group(progress: &str) -> Option<u32> {
    progress
        .strip_prefix("post-complete group ")?
        .split(':')
        .next()?
        .parse()
        .ok()
}

fn move_follow_up_to_in_flight(
    deferred: &mut VecDeque<ava_types::QueuedMessage>,
    in_flight: &mut VecDeque<ava_types::QueuedMessage>,
    text: &str,
) {
    in_flight.retain(|message| !matches!(message.tier, ava_types::MessageTier::FollowUp));
    if let Some(index) = deferred.iter().position(|queued| {
        queued.text == text && matches!(queued.tier, ava_types::MessageTier::FollowUp)
    }) {
        if let Some(message) = deferred.remove(index) {
            in_flight.push_back(message);
        }
    }
}

fn move_post_complete_group_to_in_flight(
    deferred: &mut VecDeque<ava_types::QueuedMessage>,
    in_flight: &mut VecDeque<ava_types::QueuedMessage>,
    group_id: u32,
) {
    in_flight
        .retain(|message| !matches!(message.tier, ava_types::MessageTier::PostComplete { .. }));
    let mut retained = VecDeque::new();
    while let Some(message) = deferred.pop_front() {
        if matches!(message.tier, ava_types::MessageTier::PostComplete { group } if group == group_id)
        {
            in_flight.push_back(message);
        } else {
            retained.push_back(message);
        }
    }
    *deferred = retained;
}

async fn restore_in_flight_deferred(
    session_id: Option<Uuid>,
    deferred: &tokio::sync::RwLock<HashMap<Uuid, VecDeque<ava_types::QueuedMessage>>>,
    in_flight: &tokio::sync::RwLock<HashMap<Uuid, VecDeque<ava_types::QueuedMessage>>>,
) {
    let Some(session_id) = session_id else {
        return;
    };

    let mut in_flight_guard = in_flight.write().await;
    let Some(mut session_in_flight) = in_flight_guard.remove(&session_id) else {
        return;
    };
    drop(in_flight_guard);

    if session_in_flight.is_empty() {
        return;
    }

    let mut deferred_guard = deferred.write().await;
    let session_deferred = deferred_guard.entry(session_id).or_default();
    while let Some(message) = session_in_flight.pop_back() {
        session_deferred.push_front(message);
    }
}

async fn clear_preserved_deferred(
    session_id: Option<Uuid>,
    deferred: &tokio::sync::RwLock<HashMap<Uuid, VecDeque<ava_types::QueuedMessage>>>,
    in_flight: &tokio::sync::RwLock<HashMap<Uuid, VecDeque<ava_types::QueuedMessage>>>,
) {
    let Some(session_id) = session_id else {
        return;
    };

    deferred.write().await.remove(&session_id);
    in_flight.write().await.remove(&session_id);
}

async fn cancel_active_run(app: &AppHandle, bridge: &DesktopBridge) {
    let _interactive_guard = bridge.interactive_lifecycle_lock.lock().await;
    bridge.interactive_revoked.store(true, Ordering::SeqCst);
    bridge.revoke_queue_dispatch(true).await;
    bridge.cancel().await;
    while let Some(pending) = bridge.pending_approval_reply.cancel_pending().await {
        let request_id = pending.handle.request_id.clone();
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
    }
    while let Some(pending) = bridge.pending_question_reply.cancel_pending().await {
        let request_id = pending.handle.request_id.clone();
        let _ = pending.reply.send(String::new());
        emit_interactive_request_cleared(
            app,
            &request_id,
            "question",
            false,
            pending.handle.run_id.as_deref(),
        );
    }
    while let Some(pending) = bridge.pending_plan_reply.cancel_pending().await {
        let request_id = pending.handle.request_id.clone();
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

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SubmitGoalResult {
    pub success: bool,
    pub turns: usize,
    pub session_id: String,
}

/// Internal helper that runs the agent, streams events, tracks edits for undo,
/// handles approval/question forwarding, and manages the message queue.
async fn run_agent_inner(
    goal: &str,
    max_turns: usize,
    history: Vec<ava_types::Message>,
    images: Vec<ava_types::ImageContent>,
    session_id: Option<Uuid>,
    run_id: Option<String>,
    app: &AppHandle,
    bridge: &DesktopBridge,
) -> Result<SubmitGoalResult, String> {
    let _startup_guard = bridge.startup_lock.lock().await;

    // Prevent concurrent runs
    {
        let running = bridge.running.read().await;
        if *running {
            return Err("Agent is already running. Cancel first.".to_string());
        }
    }

    let cancel = bridge.new_cancel_token().await;
    bridge.interactive_revoked.store(false, Ordering::SeqCst);

    // Create a message queue for mid-stream messaging
    let message_queue = bridge.new_message_queue(session_id).await;
    *bridge.running.write().await = true;

    // Create an event channel; spawn a forwarder that emits to all Tauri windows
    let (tx, mut rx) = mpsc::unbounded_channel();
    let app_clone = app.clone();

    // Clone the Arc-wrapped edit history so the forwarder can record file edits
    let edit_history = bridge.edit_history.clone();
    // Clone the todo state so the forwarder can emit todo updates
    let todo_state = bridge.stack.todo_state.clone();
    let checkpoint_sm = bridge.stack.session_manager.clone();
    let checkpoint_last_id = bridge.last_session_id.clone();
    let deferred_queue = bridge.deferred_queue.clone();
    let in_flight_deferred = bridge.in_flight_deferred.clone();
    let deferred_session_id = session_id;
    let event_run_id = run_id.clone();
    let todo_event_run_id = run_id.clone();
    let plan_event_run_id = run_id.clone();
    let approval_event_run_id = run_id.clone();
    let question_event_run_id = run_id.clone();
    let forwarder = tokio::spawn(async move {
        let mut last_tool_was_todo_write = false;
        while let Some(event) = rx.recv().await {
            // Checkpoint: incrementally save session so progress survives crashes.
            // Also update last_session_id so the next run can load history even
            // if the current run is cancelled/interrupted.
            if let ava_agent::agent_loop::AgentEvent::Checkpoint(ref session) = event {
                if let Err(e) =
                    save_session_checkpoint(checkpoint_sm.clone(), session.clone()).await
                {
                    tracing::error!("Failed to save session checkpoint: {e}");
                }
                *checkpoint_last_id.write().await = Some(session.id);
                continue;
            }
            // Track write/edit tool calls for undo support
            if let ava_agent::agent_loop::AgentEvent::ToolCall(ref tc) = event {
                if tc.name == "edit" || tc.name == "write" {
                    if let Some(path) = tc.arguments.get("file_path").and_then(|v| v.as_str()) {
                        match tokio::fs::read_to_string(path).await {
                            Ok(content) => {
                                let mut hist = edit_history.write().await;
                                if hist.len() >= crate::bridge::MAX_EDIT_HISTORY {
                                    hist.pop_front();
                                }
                                hist.push_back(crate::bridge::FileEditRecord {
                                    file_path: path.to_string(),
                                    previous_content: content,
                                });
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
                // Track when todo_write is called so we emit a todo_update after its result
                last_tool_was_todo_write = tc.name == "todo_write";
            } else if let ava_agent::agent_loop::AgentEvent::ToolResult(_) = event {
                // After a todo_write result, emit the updated todo list
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
                            run_id: todo_event_run_id.clone(),
                        },
                    ) {
                        tracing::error!("Failed to emit todo_update event: {e}");
                    }
                }
            } else if !matches!(
                event,
                ava_agent::agent_loop::AgentEvent::SnapshotTaken { .. }
            ) {
                // Only reset the flag for events that aren't snapshots —
                // SnapshotTaken fires between ToolCall and ToolResult and would
                // incorrectly reset last_tool_was_todo_write.
                last_tool_was_todo_write = false;
            }
            if let ava_agent::agent_loop::AgentEvent::Progress(ref message) = event {
                if let Some(session_id) = deferred_session_id {
                    if let Some(text) = message.strip_prefix("follow-up: ") {
                        let mut deferred = deferred_queue.write().await;
                        let mut in_flight = in_flight_deferred.write().await;
                        move_follow_up_to_in_flight(
                            deferred.entry(session_id).or_default(),
                            in_flight.entry(session_id).or_default(),
                            text,
                        );
                    } else if let Some(group_id) = queued_post_complete_group(message) {
                        let mut deferred = deferred_queue.write().await;
                        let mut in_flight = in_flight_deferred.write().await;
                        move_post_complete_group_to_in_flight(
                            deferred.entry(session_id).or_default(),
                            in_flight.entry(session_id).or_default(),
                            group_id,
                        );
                    }
                }
            }
            emit_backend_event(&app_clone, &event, event_run_id.as_deref());
        }
    });

    // Take the approval and question receivers out of the bridge for this run
    let mut approval_rx = {
        let mut lock = bridge.approval_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };
    let mut question_rx = {
        let mut lock = bridge.question_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };

    let mut plan_rx = {
        let mut lock = bridge.plan_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };

    let pending_approval = bridge.pending_approval_reply.clone();
    let pending_question = bridge.pending_question_reply.clone();
    let pending_plan = bridge.pending_plan_reply.clone();
    let approval_revoked = bridge.interactive_revoked.clone();
    let question_revoked = bridge.interactive_revoked.clone();
    let plan_revoked = bridge.interactive_revoked.clone();
    let approval_lifecycle_lock = bridge.interactive_lifecycle_lock.clone();
    let question_lifecycle_lock = bridge.interactive_lifecycle_lock.clone();
    let plan_lifecycle_lock = bridge.interactive_lifecycle_lock.clone();

    // Spawn approval forwarder with timeout protection.
    // If the frontend does not respond within the timeout, auto-deny the approval
    // so the agent doesn't hang forever.
    let app_approval = app.clone();
    let approval_forwarder = tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            let _interactive_guard = approval_lifecycle_lock.lock().await;
            if approval_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(ToolApproval::Rejected(Some(
                    "Agent run cancelled from desktop UI".to_string(),
                )));
                continue;
            }
            let risk_level = format!("{:?}", req.inspection.risk_level).to_lowercase();
            let handle = pending_approval
                .register_with_run_id(req.reply, approval_event_run_id.clone())
                .await;
            let id = handle.request_id.clone();

            if let Err(e) = app_approval.emit(
                "agent-event",
                AgentEvent::ApprovalRequest {
                    id: id.clone(),
                    tool_call_id: req.call.id.clone(),
                    tool_name: req.call.name.clone(),
                    args: req.call.arguments.clone(),
                    risk_level: risk_level.clone(),
                    reason: req.inspection.reason.clone(),
                    warnings: req.inspection.warnings.clone(),
                    run_id: handle.run_id.clone(),
                },
            ) {
                tracing::error!("Failed to emit approval_request event to frontend: {e}");
            }

            // Spawn a timeout watchdog: if the frontend hasn't consumed the
            // pending reply within the timeout, auto-deny so the agent unblocks.
            let watchdog_pending = pending_approval.clone();
            let watchdog_id = id.clone();
            let watchdog_app = app_approval.clone();
            tokio::spawn(async move {
                let watchdog_timeout = watchdog_pending.timeout();
                if let Some(reply) = watchdog_pending.await_timeout_request(&watchdog_id).await {
                    tracing::warn!(
                        "Approval request {id} timed out after {}s — auto-denying to unblock agent",
                        watchdog_timeout.as_secs()
                    );
                    let _ = reply.reply.send(ToolApproval::Rejected(Some(
                        "Timed out waiting for user approval in desktop UI".to_string(),
                    )));
                    emit_interactive_request_cleared(
                        &watchdog_app,
                        &reply.handle.request_id,
                        reply.handle.kind.as_str(),
                        true,
                        reply.handle.run_id.as_deref(),
                    );
                }
            });
        }
    });

    // Spawn question forwarder with timeout protection.
    let app_question = app.clone();
    let question_forwarder = tokio::spawn(async move {
        while let Some(req) = question_rx.recv().await {
            let _interactive_guard = question_lifecycle_lock.lock().await;
            if question_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(String::new());
                continue;
            }
            let handle = pending_question
                .register_with_run_id(req.reply, question_event_run_id.clone())
                .await;
            let id = handle.request_id.clone();

            if let Err(e) = app_question.emit(
                "agent-event",
                AgentEvent::QuestionRequest {
                    id: id.clone(),
                    question: req.question.clone(),
                    options: req.options.clone(),
                    run_id: handle.run_id.clone(),
                },
            ) {
                tracing::error!("Failed to emit question_request event to frontend: {e}");
            }

            // Timeout watchdog for question responses
            let watchdog_pending = pending_question.clone();
            let watchdog_id = id.clone();
            let watchdog_app = app_question.clone();
            tokio::spawn(async move {
                let watchdog_timeout = watchdog_pending.timeout();
                if let Some(reply) = watchdog_pending.await_timeout_request(&watchdog_id).await {
                    tracing::warn!(
                        "Question request {id} timed out after {}s — sending empty response to unblock agent",
                        watchdog_timeout.as_secs()
                    );
                    let _ = reply.reply.send(String::new());
                    emit_interactive_request_cleared(
                        &watchdog_app,
                        &reply.handle.request_id,
                        reply.handle.kind.as_str(),
                        true,
                        reply.handle.run_id.as_deref(),
                    );
                }
            });
        }
    });

    // Spawn plan forwarder with timeout protection.
    let app_plan = app.clone();
    let plan_forwarder = tokio::spawn(async move {
        use crate::events::{PlanPayload, PlanStepPayload};
        while let Some(req) = plan_rx.recv().await {
            let _interactive_guard = plan_lifecycle_lock.lock().await;
            if plan_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(ava_types::PlanDecision::Rejected {
                    feedback: "Agent run cancelled from desktop UI".to_string(),
                });
                continue;
            }
            let handle = pending_plan
                .register_with_run_id(req.reply, plan_event_run_id.clone())
                .await;
            let id = handle.request_id.clone();
            let steps = req
                .plan
                .steps
                .iter()
                .map(|s| {
                    let action = match s.action {
                        ava_types::PlanAction::Research => "research",
                        ava_types::PlanAction::Implement => "implement",
                        ava_types::PlanAction::Test => "test",
                        ava_types::PlanAction::Review => "review",
                    };
                    PlanStepPayload {
                        id: s.id.clone(),
                        description: s.description.clone(),
                        files: s.files.clone(),
                        action: action.to_string(),
                        depends_on: s.depends_on.clone(),
                    }
                })
                .collect();

            if let Err(e) = app_plan.emit(
                "agent-event",
                AgentEvent::PlanCreated {
                    id: id.clone(),
                    plan: PlanPayload {
                        summary: req.plan.summary.clone(),
                        steps,
                        estimated_turns: req.plan.estimated_turns.unwrap_or(0) as usize,
                    },
                    run_id: handle.run_id.clone(),
                },
            ) {
                tracing::error!("Failed to emit plan_created event to frontend: {e}");
            }

            let watchdog_pending = pending_plan.clone();
            let watchdog_id = id.clone();
            let watchdog_app = app_plan.clone();
            tokio::spawn(async move {
                let watchdog_timeout = watchdog_pending.timeout();
                if let Some(reply) = watchdog_pending.await_timeout_request(&watchdog_id).await {
                    tracing::warn!(
                        "Plan request {watchdog_id} timed out after {}s — auto-rejecting to unblock agent",
                        watchdog_timeout.as_secs()
                    );
                    let _ = reply.reply.send(ava_types::PlanDecision::Rejected {
                        feedback: "Timed out waiting for plan response in desktop UI".to_string(),
                    });
                    emit_interactive_request_cleared(
                        &watchdog_app,
                        &reply.handle.request_id,
                        reply.handle.kind.as_str(),
                        true,
                        reply.handle.run_id.as_deref(),
                    );
                }
            });
        }
    });

    info!(goal = %goal, "run_agent_inner: starting agent");

    let result = bridge
        .stack
        .run(
            goal,
            max_turns,
            Some(tx),
            cancel,
            history,
            Some(message_queue),
            images,
            session_id,
            run_id.clone(),
        )
        .await;

    // Stop accepting queue mutations as soon as the run reaches a terminal state.
    bridge.clear_message_tx().await;

    // Wait for the forwarder to drain; abort the approval/question/plan forwarders
    let _ = forwarder.await;
    approval_forwarder.abort();
    question_forwarder.abort();
    plan_forwarder.abort();

    // Clean up
    *bridge.running.write().await = false;
    match &result {
        Ok(run) => info!(
            success = run.success,
            turns = run.turns,
            session_id = %run.session.id,
            "run_agent_inner: agent completed"
        ),
        Err(error) => tracing::warn!(error = %error, "run_agent_inner: agent failed"),
    }

    match result {
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
    }
}

/// Submit a goal to the agent. Streams events via `agent-event` and returns
/// when the agent completes or is cancelled.
#[tauri::command]
pub async fn submit_goal(
    args: SubmitGoalArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    // Apply model override if requested
    if let (Some(ref provider), Some(ref model)) = (&args.provider, &args.model) {
        bridge
            .stack
            .switch_model(provider, model)
            .await
            .map_err(|e| e.to_string())?;
    }

    // Apply thinking level from frontend
    if let Some(ref level_str) = args.thinking_level {
        let level = match level_str.as_str() {
            "low" => ava_types::ThinkingLevel::Low,
            "medium" => ava_types::ThinkingLevel::Medium,
            "high" => ava_types::ThinkingLevel::High,
            "max" | "xhigh" => ava_types::ThinkingLevel::Max,
            _ => ava_types::ThinkingLevel::Off,
        };
        if let Err(e) = bridge.stack.set_thinking_level(level).await {
            tracing::warn!("Failed to set thinking level: {e}");
        }
    }

    if args.auto_compact.is_some()
        || args.compaction_threshold.is_some()
        || args.compaction_provider.is_some()
        || args.compaction_model.is_some()
    {
        let auto_compact = args.auto_compact.unwrap_or(true);
        let threshold = args.compaction_threshold.unwrap_or(80);
        let override_model = match (&args.compaction_provider, &args.compaction_model) {
            (Some(provider), Some(model)) => Some((provider.clone(), model.clone())),
            _ => None,
        };
        bridge
            .stack
            .set_compaction_settings(auto_compact, threshold, override_model)
            .await
            .map_err(|e| e.to_string())?;
    }

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

    run_agent_inner(
        &args.goal,
        args.max_turns,
        history,
        vec![],
        Some(run_session.session_id),
        Some(run_id),
        &app,
        &bridge,
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

/// Cancel the currently-running agent.
#[tauri::command]
pub async fn cancel_agent(app: AppHandle, bridge: State<'_, DesktopBridge>) -> Result<(), String> {
    cancel_active_run(&app, &bridge).await;
    Ok(())
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentStatus {
    pub running: bool,
    pub provider: String,
    pub model: String,
}

/// Get current agent status (running, provider, model).
#[tauri::command]
pub async fn get_agent_status(bridge: State<'_, DesktopBridge>) -> Result<AgentStatus, String> {
    let running = *bridge.running.read().await;
    let (provider, model) = bridge.stack.current_model().await;
    Ok(AgentStatus {
        running,
        provider,
        model,
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
        emit_interactive_request_cleared(
            &app,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        return Err(
            "Failed to send approval response — the agent may have already moved on".to_string(),
        );
    }

    emit_interactive_request_cleared(
        &app,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );

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
    let reply = take_resolved_desktop_interactive_request(
        &bridge.pending_question_reply,
        "question",
        args.request_id.as_deref(),
    )
    .await?;

    if reply.reply.send(args.answer).is_err() {
        emit_interactive_request_cleared(
            &app,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        return Err(
            "Failed to send question response — the agent may have already moved on".to_string(),
        );
    }

    emit_interactive_request_cleared(
        &app,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );

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

    let reply = take_resolved_desktop_interactive_request(
        &bridge.pending_plan_reply,
        "plan",
        args.request_id.as_deref(),
    )
    .await?;

    if reply.reply.send(decision).is_err() {
        emit_interactive_request_cleared(
            &app,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        return Err(
            "Failed to send plan response — the agent may have already moved on".to_string(),
        );
    }

    emit_interactive_request_cleared(
        &app,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );

    Ok(())
}

// ============================================================================
// Mid-stream messaging commands
// ============================================================================

/// Inject a steering message (Tier 1).
#[tauri::command]
pub async fn steer_agent(message: String, bridge: State<'_, DesktopBridge>) -> Result<(), String> {
    if message.is_empty() {
        return Err("Steering message must not be empty.".to_string());
    }
    bridge
        .send_message(
            message,
            queue_message_tier(ControlPlaneCommand::SteerAgent, None)
                .expect("steer command should map to a queue tier"),
            None,
        )
        .await
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FollowUpArgs {
    pub message: String,
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
        .send_message(
            args.message,
            queue_message_tier(ControlPlaneCommand::FollowUpAgent, None)
                .expect("follow-up command should map to a queue tier"),
            requested_session_id,
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
        .send_message(
            args.message,
            queue_message_tier(ControlPlaneCommand::PostCompleteAgent, Some(args.group))
                .expect("post-complete command should map to a queue tier"),
            requested_session_id,
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
    bridge: State<'_, DesktopBridge>,
) -> Result<MessageQueueState, String> {
    let running = *bridge.running.read().await;
    let snapshot = bridge.queue_dispatch_snapshot().await;
    Ok(MessageQueueState {
        active: running && snapshot.accepting && snapshot.tx.is_some(),
    })
}

/// Clear messages from the queue.
#[tauri::command]
pub async fn clear_message_queue(
    target: ClearQueueTarget,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    match clear_queue_semantics(target) {
        QueueClearSemantics::CancelRunAndClearSteering => {
            cancel_active_run(&app, &bridge).await;
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

    run_agent_inner(
        &goal,
        0,
        history,
        images,
        Some(session_id),
        Some(run_id),
        &app,
        &bridge,
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

    run_agent_inner(
        &goal,
        0,
        history,
        images,
        Some(session_id),
        Some(run_id),
        &app,
        &bridge,
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

    run_agent_inner(
        &goal,
        0,
        history,
        images,
        Some(session_id),
        Some(run_id),
        &app,
        &bridge,
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
pub async fn undo_last_edit(bridge: State<'_, DesktopBridge>) -> Result<UndoResult, String> {
    let record = bridge.pop_last_edit().await;

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

    #[test]
    fn unsupported_queue_clear_error_mentions_unimplemented_targets() {
        assert!(UNSUPPORTED_QUEUE_CLEAR_ERROR.contains("not supported yet"));
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
}
