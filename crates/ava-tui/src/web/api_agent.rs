//! Agent-related HTTP API handlers: submit, cancel, status, retry, edit-resend,
//! regenerate, mid-stream messaging (steer/follow-up/post-complete), queue.

use std::collections::{HashMap, VecDeque};
use std::sync::Arc;

use ava_agent::control_plane::commands::{queue_message_tier, ControlPlaneCommand};
use ava_agent::control_plane::interactive::InteractiveRequestKind;
use ava_agent::control_plane::queue::{
    clear_queue_semantics, parse_clear_queue_target, QueueClearSemantics,
    UNSUPPORTED_QUEUE_CLEAR_ERROR,
};
use ava_agent::control_plane::sessions::{
    build_edit_replay_payload, build_regenerate_replay_payload, build_retry_replay_payload,
    resolve_existing_session, resolve_session_precedence, SessionPromptContext, SessionSelection,
    SessionSelectionSource,
};
use ava_tools::core::plan::PlanRequest;
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use axum::extract::{Query, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use std::sync::atomic::Ordering;
use tokio::sync::{broadcast, mpsc};
use tokio::task::JoinHandle;
use tracing::info;

use super::state::{
    FileEditRecord, PlanStepPayload, TodoItemPayload, WebEvent, WebState, WebStateInner,
};

use super::api::{error_response, ErrorResponse};

// ============================================================================
// Submit Goal
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
    /// Optional explicit session ID. When omitted, the shared session
    /// For web-backed sessions, omitting `session_id` now creates a new
    /// session rather than inheriting the process-wide last active session.
    #[serde(default)]
    #[serde(alias = "sessionId")]
    pub session_id: Option<String>,
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
}

#[derive(Serialize)]
pub struct SubmitGoalResponse {
    /// Matches the frontend's `SubmitGoalResult` interface.
    pub success: bool,
    pub turns: usize,
    #[serde(rename = "sessionId")]
    pub session_id: String,
}

fn ensure_web_run_id(run_id: Option<String>) -> String {
    run_id.unwrap_or_else(|| format!("web-run-{}", uuid::Uuid::new_v4()))
}

fn queued_post_complete_group(progress: &str) -> Option<u32> {
    progress
        .strip_prefix("post-complete group ")?
        .split(':')
        .next()?
        .parse()
        .ok()
}

async fn cancel_run(state: &WebState, run_id: &str) {
    let _interactive_guard = state.inner.interactive_lifecycle_lock.lock().await;
    let Ok(run) = state.resolve_run(Some(run_id), None).await else {
        return;
    };
    run.interactive_revoked.store(true, Ordering::SeqCst);
    state.revoke_queue_dispatch(run_id, true).await;
    run.cancel.cancel();

    while let Some(cancelled) = state
        .inner
        .pending_approval_reply
        .cancel_pending_for_run(run_id)
        .await
    {
        let request_id = cancelled.handle.request_id.clone();
        discard_deferred_interactive_request_event(&state.inner, &request_id).await;
        let _ = cancelled.reply.send(ToolApproval::Rejected(Some(
            "Agent run cancelled from web UI".to_string(),
        )));
        emit_interactive_request_cleared(
            &state.inner.event_tx,
            &request_id,
            "approval",
            false,
            cancelled.handle.run_id.as_deref(),
        );
        emit_promoted_interactive_request_event(
            &state.inner,
            cancelled.handle.kind,
            cancelled.handle.run_id.as_deref(),
        )
        .await;
    }
    while let Some(cancelled) = state
        .inner
        .pending_question_reply
        .cancel_pending_for_run(run_id)
        .await
    {
        let request_id = cancelled.handle.request_id.clone();
        discard_deferred_interactive_request_event(&state.inner, &request_id).await;
        let _ = cancelled.reply.send(String::new());
        emit_interactive_request_cleared(
            &state.inner.event_tx,
            &request_id,
            "question",
            false,
            cancelled.handle.run_id.as_deref(),
        );
        emit_promoted_interactive_request_event(
            &state.inner,
            cancelled.handle.kind,
            cancelled.handle.run_id.as_deref(),
        )
        .await;
    }
    while let Some(cancelled) = state
        .inner
        .pending_plan_reply
        .cancel_pending_for_run(run_id)
        .await
    {
        let request_id = cancelled.handle.request_id.clone();
        discard_deferred_interactive_request_event(&state.inner, &request_id).await;
        let _ = cancelled.reply.send(ava_types::PlanDecision::Rejected {
            feedback: "Agent run cancelled from web UI".to_string(),
        });
        emit_interactive_request_cleared(
            &state.inner.event_tx,
            &request_id,
            "plan",
            false,
            cancelled.handle.run_id.as_deref(),
        );
        emit_promoted_interactive_request_event(
            &state.inner,
            cancelled.handle.kind,
            cancelled.handle.run_id.as_deref(),
        )
        .await;
    }
}

fn is_inactive_scoped_status_lookup(
    requested_run_id: Option<&str>,
    requested_session_id: Option<uuid::Uuid>,
    message: &str,
) -> bool {
    match (requested_run_id, requested_session_id) {
        (Some(run_id), Some(session_id)) => {
            message == format!("Run {run_id} is not active")
                || message == format!("Session {session_id} does not have an active run")
        }
        (Some(run_id), None) => message == format!("Run {run_id} is not active"),
        (None, Some(session_id)) => {
            message == format!("Session {session_id} does not have an active run")
        }
        (None, None) => false,
    }
}

fn move_follow_up_to_in_flight(
    deferred: &mut std::collections::VecDeque<ava_types::QueuedMessage>,
    in_flight: &mut std::collections::VecDeque<ava_types::QueuedMessage>,
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
    deferred: &mut std::collections::VecDeque<ava_types::QueuedMessage>,
    in_flight: &mut std::collections::VecDeque<ava_types::QueuedMessage>,
    group_id: u32,
) {
    in_flight
        .retain(|message| !matches!(message.tier, ava_types::MessageTier::PostComplete { .. }));
    let mut retained = std::collections::VecDeque::new();
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
    session_id: uuid::Uuid,
    deferred: &tokio::sync::RwLock<HashMap<uuid::Uuid, VecDeque<ava_types::QueuedMessage>>>,
    in_flight: &tokio::sync::RwLock<HashMap<uuid::Uuid, VecDeque<ava_types::QueuedMessage>>>,
) {
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
    session_id: uuid::Uuid,
    deferred: &tokio::sync::RwLock<HashMap<uuid::Uuid, VecDeque<ava_types::QueuedMessage>>>,
    in_flight: &tokio::sync::RwLock<HashMap<uuid::Uuid, VecDeque<ava_types::QueuedMessage>>>,
) {
    deferred.write().await.remove(&session_id);
    in_flight.write().await.remove(&session_id);
}

fn resolve_web_submit_session(requested_session_id: Option<uuid::Uuid>) -> SessionSelection {
    resolve_session_precedence(requested_session_id, None, uuid::Uuid::new_v4)
}

fn resolve_web_compaction_session(
    requested_session_id: Option<uuid::Uuid>,
) -> Option<SessionSelection> {
    resolve_existing_session(requested_session_id, None)
}

fn resolve_web_replay_session(
    requested_session_id: Option<uuid::Uuid>,
) -> Option<SessionSelection> {
    resolve_existing_session(requested_session_id, None)
}

fn parse_optional_session_id(
    session_id: Option<&str>,
) -> Result<Option<uuid::Uuid>, (StatusCode, Json<ErrorResponse>)> {
    session_id
        .map(uuid::Uuid::parse_str)
        .transpose()
        .map_err(|e| error_response(StatusCode::BAD_REQUEST, &format!("Invalid session_id: {e}")))
}

async fn load_owned_replay_session(
    state: &WebState,
    session_id: Option<&str>,
    missing_message: &str,
) -> Result<(uuid::Uuid, ava_types::Session), (StatusCode, Json<ErrorResponse>)> {
    let requested_session_id = parse_optional_session_id(session_id)?;
    let session_selection = resolve_web_replay_session(requested_session_id)
        .ok_or_else(|| error_response(StatusCode::BAD_REQUEST, missing_message))?;
    let session_id = session_selection.session_id;

    let session = state
        .inner
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    Ok((session_id, session))
}

fn resolve_web_queue_clear_semantics(target_str: &str) -> Result<QueueClearSemantics, String> {
    let target = parse_clear_queue_target(target_str)
        .ok_or_else(|| format!("Unsupported clear target '{target_str}'"))?;

    match clear_queue_semantics(target) {
        semantics @ QueueClearSemantics::CancelRunAndClearSteering => Ok(semantics),
        QueueClearSemantics::Unsupported => Err(UNSUPPORTED_QUEUE_CLEAR_ERROR.to_string()),
    }
}

fn emit_interactive_request_cleared(
    event_tx: &broadcast::Sender<WebEvent>,
    request_id: &str,
    request_kind: &str,
    timed_out: bool,
    run_id: Option<&str>,
) {
    let _ = event_tx.send(WebEvent::InteractiveRequestCleared {
        request_id: request_id.to_string(),
        request_kind: request_kind.to_string(),
        timed_out,
        run_id: run_id.map(str::to_string),
    });
}

async fn current_request_id_for_kind(
    inner: &WebStateInner,
    kind: InteractiveRequestKind,
    run_id: Option<&str>,
) -> Option<String> {
    match kind {
        InteractiveRequestKind::Approval => {
            inner
                .pending_approval_reply
                .current_request_id_for_run(run_id)
                .await
        }
        InteractiveRequestKind::Question => {
            inner
                .pending_question_reply
                .current_request_id_for_run(run_id)
                .await
        }
        InteractiveRequestKind::Plan => {
            inner
                .pending_plan_reply
                .current_request_id_for_run(run_id)
                .await
        }
    }
}

async fn emit_or_defer_interactive_request_event(
    inner: &Arc<WebStateInner>,
    request_id: &str,
    kind: InteractiveRequestKind,
    run_id: Option<&str>,
    event: WebEvent,
) {
    let is_actionable_now = current_request_id_for_kind(inner, kind, run_id)
        .await
        .is_some_and(|current_id| current_id == request_id);

    if is_actionable_now {
        let _ = inner.event_tx.send(event);
        return;
    }

    inner
        .deferred_interactive_events
        .lock()
        .await
        .insert(request_id.to_string(), event);
}

pub(super) async fn discard_deferred_interactive_request_event(
    inner: &Arc<WebStateInner>,
    request_id: &str,
) {
    inner
        .deferred_interactive_events
        .lock()
        .await
        .remove(request_id);
}

pub(super) async fn emit_promoted_interactive_request_event(
    inner: &Arc<WebStateInner>,
    kind: InteractiveRequestKind,
    run_id: Option<&str>,
) {
    let Some(request_id) = current_request_id_for_kind(inner, kind, run_id).await else {
        return;
    };
    if let Some(event) = inner
        .deferred_interactive_events
        .lock()
        .await
        .remove(&request_id)
    {
        let _ = inner.event_tx.send(event);
    }
}

fn plan_step_payloads(plan: &ava_types::Plan) -> Vec<PlanStepPayload> {
    plan.steps
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
        .collect()
}

pub(super) fn spawn_interactive_forwarders(
    inner: Arc<WebStateInner>,
    mut approval_rx: mpsc::UnboundedReceiver<ApprovalRequest>,
    mut question_rx: mpsc::UnboundedReceiver<QuestionRequest>,
    mut plan_rx: mpsc::UnboundedReceiver<PlanRequest>,
) -> (JoinHandle<()>, JoinHandle<()>, JoinHandle<()>) {
    let approval_inner = inner.clone();
    let approval_forwarder = tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            let _interactive_guard = approval_inner.interactive_lifecycle_lock.lock().await;
            let run_id = req.run_id.clone();
            let Some(run) = (match run_id.as_deref() {
                Some(run_id) => approval_inner.runs.read().await.get(run_id).cloned(),
                None => None,
            }) else {
                let _ = req.reply.send(ToolApproval::Rejected(Some(
                    "Agent run is no longer active in web UI".to_string(),
                )));
                continue;
            };
            if run.interactive_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(ToolApproval::Rejected(Some(
                    "Agent run cancelled from web UI".to_string(),
                )));
                continue;
            }
            let risk_level = format!("{:?}", req.inspection.risk_level).to_lowercase();
            let handle = approval_inner
                .pending_approval_reply
                .register_with_run_id(req.reply, run_id.clone())
                .await;
            let request_id = handle.request_id.clone();

            emit_or_defer_interactive_request_event(
                &approval_inner,
                &request_id,
                InteractiveRequestKind::Approval,
                handle.run_id.as_deref(),
                WebEvent::ApprovalRequest {
                    id: request_id.clone(),
                    tool_call_id: req.call.id.clone(),
                    tool_name: req.call.name.clone(),
                    args: req.call.arguments.clone(),
                    risk_level,
                    reason: req.inspection.reason.clone(),
                    warnings: req.inspection.warnings.clone(),
                    run_id: handle.run_id.clone(),
                },
            )
            .await;

            let pending = approval_inner.pending_approval_reply.clone();
            let event_tx = approval_inner.event_tx.clone();
            let promotion_inner = approval_inner.clone();
            tokio::spawn(async move {
                let timeout = pending.timeout();
                if let Some(timed_out) = pending.await_timeout_request(&request_id).await {
                    tracing::warn!(
                        request_id = %timed_out.handle.request_id,
                        timeout_secs = timeout.as_secs(),
                        "Web approval request timed out — auto-denying to unblock agent"
                    );
                    let _ = timed_out.reply.send(ToolApproval::Rejected(Some(
                        "Timed out waiting for user approval in web UI".to_string(),
                    )));
                    emit_interactive_request_cleared(
                        &event_tx,
                        &timed_out.handle.request_id,
                        timed_out.handle.kind.as_str(),
                        true,
                        timed_out.handle.run_id.as_deref(),
                    );
                    emit_promoted_interactive_request_event(
                        &promotion_inner,
                        timed_out.handle.kind,
                        timed_out.handle.run_id.as_deref(),
                    )
                    .await;
                }
            });
        }
    });

    let question_inner = inner.clone();
    let question_forwarder = tokio::spawn(async move {
        while let Some(req) = question_rx.recv().await {
            let _interactive_guard = question_inner.interactive_lifecycle_lock.lock().await;
            let run_id = req.run_id.clone();
            let Some(run) = (match run_id.as_deref() {
                Some(run_id) => question_inner.runs.read().await.get(run_id).cloned(),
                None => None,
            }) else {
                let _ = req.reply.send(String::new());
                continue;
            };
            if run.interactive_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(String::new());
                continue;
            }
            let handle = question_inner
                .pending_question_reply
                .register_with_run_id(req.reply, run_id.clone())
                .await;
            let request_id = handle.request_id.clone();

            emit_or_defer_interactive_request_event(
                &question_inner,
                &request_id,
                InteractiveRequestKind::Question,
                handle.run_id.as_deref(),
                WebEvent::QuestionRequest {
                    id: request_id.clone(),
                    question: req.question.clone(),
                    options: req.options.clone(),
                    run_id: handle.run_id.clone(),
                },
            )
            .await;

            let pending = question_inner.pending_question_reply.clone();
            let event_tx = question_inner.event_tx.clone();
            let promotion_inner = question_inner.clone();
            tokio::spawn(async move {
                let timeout = pending.timeout();
                if let Some(timed_out) = pending.await_timeout_request(&request_id).await {
                    tracing::warn!(
                        request_id = %timed_out.handle.request_id,
                        timeout_secs = timeout.as_secs(),
                        "Web question request timed out — sending empty response to unblock agent"
                    );
                    let _ = timed_out.reply.send(String::new());
                    emit_interactive_request_cleared(
                        &event_tx,
                        &timed_out.handle.request_id,
                        timed_out.handle.kind.as_str(),
                        true,
                        timed_out.handle.run_id.as_deref(),
                    );
                    emit_promoted_interactive_request_event(
                        &promotion_inner,
                        timed_out.handle.kind,
                        timed_out.handle.run_id.as_deref(),
                    )
                    .await;
                }
            });
        }
    });

    let plan_forwarder = tokio::spawn(async move {
        while let Some(req) = plan_rx.recv().await {
            let _interactive_guard = inner.interactive_lifecycle_lock.lock().await;
            let run_id = req.run_id.clone();
            let Some(run) = (match run_id.as_deref() {
                Some(run_id) => inner.runs.read().await.get(run_id).cloned(),
                None => None,
            }) else {
                let _ = req.reply.send(ava_types::PlanDecision::Rejected {
                    feedback: "Agent run is no longer active in web UI".to_string(),
                });
                continue;
            };
            if run.interactive_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(ava_types::PlanDecision::Rejected {
                    feedback: "Agent run cancelled from web UI".to_string(),
                });
                continue;
            }
            let handle = inner
                .pending_plan_reply
                .register_with_run_id(req.reply, run_id.clone())
                .await;
            let request_id = handle.request_id.clone();

            emit_or_defer_interactive_request_event(
                &inner,
                &request_id,
                InteractiveRequestKind::Plan,
                handle.run_id.as_deref(),
                WebEvent::PlanCreated {
                    id: request_id.clone(),
                    summary: req.plan.summary.clone(),
                    steps: plan_step_payloads(&req.plan),
                    estimated_turns: req.plan.estimated_turns.unwrap_or(0) as usize,
                    run_id: handle.run_id.clone(),
                },
            )
            .await;

            let pending = inner.pending_plan_reply.clone();
            let event_tx = inner.event_tx.clone();
            let promotion_inner = inner.clone();
            tokio::spawn(async move {
                let timeout = pending.timeout();
                if let Some(timed_out) = pending.await_timeout_request(&request_id).await {
                    tracing::warn!(
                        request_id = %timed_out.handle.request_id,
                        timeout_secs = timeout.as_secs(),
                        "Web plan request timed out — auto-rejecting to unblock agent"
                    );
                    let _ = timed_out.reply.send(ava_types::PlanDecision::Rejected {
                        feedback: "Timed out waiting for plan response in web UI".to_string(),
                    });
                    emit_interactive_request_cleared(
                        &event_tx,
                        &timed_out.handle.request_id,
                        timed_out.handle.kind.as_str(),
                        true,
                        timed_out.handle.run_id.as_deref(),
                    );
                    emit_promoted_interactive_request_event(
                        &promotion_inner,
                        timed_out.handle.kind,
                        timed_out.handle.run_id.as_deref(),
                    )
                    .await;
                }
            });
        }
    });

    (approval_forwarder, question_forwarder, plan_forwarder)
}

async fn launch_web_run(
    state: &WebState,
    session_id: uuid::Uuid,
    goal: String,
    history: Vec<ava_types::Message>,
    images: Vec<ava_types::ImageContent>,
    max_turns: usize,
    run_id: String,
    model_override: Option<(String, String)>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let startup_guard = state.inner.startup_lock.lock().await;
    if let Some((provider, model)) = model_override {
        if state.has_active_runs().await {
            return Err(error_response(
                StatusCode::CONFLICT,
                "Cannot switch models while web runs are active.",
            ));
        }
        state
            .inner
            .stack
            .switch_model(&provider, &model)
            .await
            .map_err(|e| error_response(StatusCode::BAD_REQUEST, &e.to_string()))?;
    }
    let run = state
        .register_run(run_id.clone(), session_id)
        .await
        .map_err(|message| error_response(StatusCode::CONFLICT, &message))?;

    let inner = state.inner.clone();
    let stack = inner.stack.clone();
    let session_id_str = session_id.to_string();

    info!(goal = %goal, max_turns, run_id = %run_id, session_id = %session_id, "Web: starting agent (async)");

    let (msg_queue, msg_queue_tx, msg_queue_control) = stack.create_message_queue_with_control();
    if let Some(messages) = inner.deferred_queue.read().await.get(&session_id) {
        for message in messages.iter().cloned() {
            let _ = msg_queue_tx.send(message);
        }
    }
    if let Err(message) = state
        .activate_message_queue(&run_id, msg_queue_tx, msg_queue_control)
        .await
    {
        state.finish_run(&run_id).await;
        return Err(error_response(StatusCode::CONFLICT, &message));
    }

    let edit_history = inner.edit_history.clone();
    let state_for_run = state.clone();
    drop(startup_guard);

    tokio::spawn(async move {
        let (tx, mut rx) = mpsc::unbounded_channel();
        let event_broadcast = inner.event_tx.clone();
        let edit_hist = edit_history.clone();
        let checkpoint_stack = stack.clone();
        let checkpoint_last_id = inner.clone();
        let deferred_queue = inner.deferred_queue.clone();
        let in_flight_deferred = inner.in_flight_deferred.clone();
        let deferred_session_id = session_id;
        let todo_state = stack.todo_state.clone();
        let forwarder_run_id = run_id.clone();

        let forwarder = tokio::spawn(async move {
            let mut last_tool_was_todo_write = false;
            while let Some(event) = rx.recv().await {
                if let ava_agent::agent_loop::AgentEvent::Checkpoint(ref session) = event {
                    let _ = checkpoint_stack
                        .session_manager
                        .add_messages(session.id, &session.messages);
                    *checkpoint_last_id.last_session_id.write().await = Some(session.id);
                    continue;
                }
                if let ava_agent::agent_loop::AgentEvent::ToolCall(ref tc) = event {
                    if tc.name == "edit" || tc.name == "write" {
                        if let Some(path) = tc.arguments.get("file_path").and_then(|v| v.as_str()) {
                            if let Ok(content) = tokio::fs::read_to_string(path).await {
                                let mut hist = edit_hist.write().await;
                                if hist.len() >= 100 {
                                    hist.pop_front();
                                }
                                hist.push_back(FileEditRecord {
                                    file_path: path.to_string(),
                                    previous_content: content,
                                });
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
                            .map(|item| TodoItemPayload {
                                content: item.content.clone(),
                                status: item.status.to_string(),
                                priority: item.priority.to_string(),
                            })
                            .collect();
                        let _ = event_broadcast.send(WebEvent::TodoUpdate {
                            todos,
                            run_id: Some(forwarder_run_id.clone()),
                        });
                    }
                } else {
                    last_tool_was_todo_write = false;
                }

                if let ava_agent::agent_loop::AgentEvent::Progress(ref message) = event {
                    if let Some(text) = message.strip_prefix("follow-up: ") {
                        let mut deferred = deferred_queue.write().await;
                        let mut in_flight = in_flight_deferred.write().await;
                        move_follow_up_to_in_flight(
                            deferred.entry(deferred_session_id).or_default(),
                            in_flight.entry(deferred_session_id).or_default(),
                            text,
                        );
                    } else if let Some(group_id) = queued_post_complete_group(message) {
                        let mut deferred = deferred_queue.write().await;
                        let mut in_flight = in_flight_deferred.write().await;
                        move_post_complete_group_to_in_flight(
                            deferred.entry(deferred_session_id).or_default(),
                            in_flight.entry(deferred_session_id).or_default(),
                            group_id,
                        );
                    }
                }

                let _ = event_broadcast.send(WebEvent::Agent {
                    event,
                    run_id: Some(forwarder_run_id.clone()),
                });
            }
        });

        let result = stack
            .run(
                &goal,
                max_turns,
                Some(tx),
                run.cancel.clone(),
                history,
                Some(msg_queue),
                images,
                Some(session_id),
                Some(run_id.clone()),
            )
            .await;

        state_for_run.clear_message_queue_dispatch(&run_id).await;
        let _ = forwarder.await;

        match result {
            Ok(run_result) => {
                clear_preserved_deferred(
                    session_id,
                    &inner.deferred_queue,
                    &inner.in_flight_deferred,
                )
                .await;
                match stack.session_manager.save(&run_result.session) {
                    Ok(()) => {
                        *inner.last_session_id.write().await = Some(run_result.session.id);
                    }
                    Err(e) => {
                        tracing::error!("Failed to persist session {}: {e}", run_result.session.id);
                        *inner.last_session_id.write().await = Some(run_result.session.id);
                    }
                }
            }
            Err(e) => {
                restore_in_flight_deferred(
                    session_id,
                    &inner.deferred_queue,
                    &inner.in_flight_deferred,
                )
                .await;
                if matches!(e, ava_types::AvaError::Cancelled) {
                    tracing::info!("Agent run cancelled by user (session {session_id})");
                    *inner.last_session_id.write().await = Some(session_id);
                    let _ = inner.event_tx.send(WebEvent::Agent {
                        event: ava_agent::agent_loop::AgentEvent::Error(
                            "Agent run cancelled by user".to_string(),
                        ),
                        run_id: Some(run_id.clone()),
                    });
                } else {
                    tracing::error!("Agent run failed: {e}");
                    let _ = inner.event_tx.send(WebEvent::Agent {
                        event: ava_agent::agent_loop::AgentEvent::Error(e.to_string()),
                        run_id: Some(run_id.clone()),
                    });
                }
            }
        }

        state_for_run.finish_run(&run_id).await;
    });

    Ok(Json(SubmitGoalResponse {
        success: true,
        turns: 0,
        session_id: session_id_str,
    }))
}

/// Start the agent with a goal asynchronously.
///
/// Returns immediately with the session ID and `"running"` status.
/// Agent events stream over the WebSocket broadcast channel as the agent runs.
/// When the agent finishes, a `Complete` or `Error` event is sent.
pub(crate) async fn submit_goal(
    State(state): State<WebState>,
    Json(req): Json<SubmitGoalRequest>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let run_id = ensure_web_run_id(req.run_id);

    let requested_session_id = req
        .session_id
        .as_deref()
        .map(uuid::Uuid::parse_str)
        .transpose()
        .map_err(|e| {
            error_response(StatusCode::BAD_REQUEST, &format!("Invalid session_id: {e}"))
        })?;
    let session_selection = resolve_web_submit_session(requested_session_id);
    let session_uuid = session_selection.session_id;
    let history = if session_selection.source == SessionSelectionSource::New {
        vec![]
    } else {
        state
            .inner
            .stack
            .session_manager
            .get(session_uuid)
            .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
            .map(|s| s.messages)
            .unwrap_or_default()
    };

    let max_turns = if req.max_turns > 0 { req.max_turns } else { 0 };

    launch_web_run(
        &state,
        session_uuid,
        req.goal.clone(),
        history,
        vec![],
        max_turns,
        run_id,
        req.provider.zip(req.model),
    )
    .await
}

/// Cancel the currently-running agent.
pub(crate) async fn cancel_agent(
    State(state): State<WebState>,
    maybe_req: Option<Json<RunCorrelationRequest>>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let req = maybe_req.map(|Json(req)| req).unwrap_or_default();
    let requested_session_id = parse_optional_session_id(req.session_id.as_deref())?;

    let run = if req.run_id.is_some() || requested_session_id.is_some() {
        state
            .resolve_run(req.run_id.as_deref(), requested_session_id)
            .await
            .map_err(|message| error_response(StatusCode::CONFLICT, &message))?
    } else {
        state
            .resolve_run(None, None)
            .await
            .map_err(|message| error_response(StatusCode::CONFLICT, &message))?
    };
    cancel_run(&state, &run.run_id).await;

    Ok(Json(serde_json::json!({ "cancelled": true })))
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentStatusResponse {
    pub running: bool,
    pub provider: String,
    pub model: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub run_id: Option<String>,
}

/// Get current agent status.
pub(crate) async fn agent_status(
    State(state): State<WebState>,
    Query(req): Query<RunCorrelationRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let requested_session_id = parse_optional_session_id(req.session_id.as_deref())?;
    let scoped_lookup = req.run_id.is_some() || requested_session_id.is_some();

    let run_id = if scoped_lookup {
        state
            .resolve_run(req.run_id.as_deref(), requested_session_id)
            .await
            .map(|run| Some(run.run_id.clone()))
            .or_else(|message| {
                if is_inactive_scoped_status_lookup(
                    req.run_id.as_deref(),
                    requested_session_id,
                    &message,
                ) {
                    Ok(None)
                } else {
                    Err(error_response(StatusCode::CONFLICT, &message))
                }
            })?
    } else {
        state.single_active_run_id().await
    };
    let (provider, model) = state.inner.stack.current_model().await;
    Ok(Json(AgentStatusResponse {
        running: if scoped_lookup {
            run_id.is_some()
        } else {
            run_id.is_some() || state.has_active_runs().await
        },
        provider,
        model,
        run_id,
    }))
}

// ============================================================================
// Retry / Edit+Resend / Regenerate
// ============================================================================

/// Helper: run a new agent task with the given goal and history, returning
/// the same format as SubmitGoalResponse. Used by retry/regenerate/edit-resend.
pub(crate) async fn run_agent_from_history(
    state: &WebState,
    session_id: uuid::Uuid,
    goal: String,
    history: Vec<ava_types::Message>,
    images: Vec<ava_types::ImageContent>,
    run_id: Option<String>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let run_id = ensure_web_run_id(run_id);
    launch_web_run(state, session_id, goal, history, images, 0, run_id, None).await
}

#[derive(Deserialize, Default)]
/// Optional run/session correlation for web control-plane routes.
///
/// Accepts both snake_case (`run_id`, `session_id`) and camelCase aliases
/// (`runId`, `sessionId`) for compatibility with browser-mode callers.
pub struct RunCorrelationRequest {
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    #[serde(alias = "sessionId")]
    pub session_id: Option<String>,
}

#[derive(Deserialize, Default)]
/// Replay routes keep the same wire field names as other web control calls,
/// but here `run_id` seeds the *new* replay run rather than targeting an
/// existing active run. Making that request shape explicit avoids implying the
/// replay routes perform ownership lookup against a live run.
pub struct ReplayRunRequest {
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    #[serde(alias = "sessionId")]
    pub session_id: Option<String>,
}

/// Retry the last user message.
pub(crate) async fn retry_last_message(
    State(state): State<WebState>,
    maybe_req: Option<Json<ReplayRunRequest>>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let req = maybe_req.map(|Json(req)| req).unwrap_or_default();
    let (session_id, session) = load_owned_replay_session(
        &state,
        req.session_id.as_deref(),
        "session_id is required for web retry requests",
    )
    .await?;

    let SessionPromptContext {
        goal,
        history,
        images,
    } = build_retry_replay_payload(&session)
        .map_err(|error| error_response(StatusCode::CONFLICT, &error.to_string()))?;

    info!(goal = %goal, %session_id, "Web: retry_last_message");
    run_agent_from_history(&state, session_id, goal, history, images, req.run_id).await
}

#[derive(Deserialize)]
pub struct EditAndResendRequest {
    #[serde(alias = "messageId")]
    pub message_id: String,
    #[serde(alias = "newContent")]
    pub new_content: String,
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    #[serde(alias = "sessionId")]
    pub session_id: Option<String>,
}

/// Edit a specific user message and re-run the agent from that point.
///
/// The frontend generates its own message IDs (client-side UUIDs stored in
/// IndexedDB) which may not match the backend's session IDs in SQLite.
/// When the target message cannot be resolved by ID, the request is rejected.
/// Browser mode must not silently edit the wrong turn.
pub(crate) async fn edit_and_resend(
    State(state): State<WebState>,
    Json(req): Json<EditAndResendRequest>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let (session_id, session) = load_owned_replay_session(
        &state,
        req.session_id.as_deref(),
        "session_id is required for web edit-resend requests",
    )
    .await?;
    let SessionPromptContext {
        goal,
        history,
        images,
    } = {
        let target_id = uuid::Uuid::parse_str(&req.message_id).ok();
        build_edit_replay_payload(&session, target_id, req.new_content)
            .map_err(|error| error_response(StatusCode::CONFLICT, &error.to_string()))?
    };
    info!(new_content = %goal, message_id = %req.message_id, "Web: edit_and_resend");
    run_agent_from_history(&state, session_id, goal, history, images, req.run_id).await
}

/// Regenerate the last assistant response.
pub(crate) async fn regenerate_response(
    State(state): State<WebState>,
    maybe_req: Option<Json<ReplayRunRequest>>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let req = maybe_req.map(|Json(req)| req).unwrap_or_default();
    let (session_id, session) = load_owned_replay_session(
        &state,
        req.session_id.as_deref(),
        "session_id is required for web regenerate requests",
    )
    .await?;

    let SessionPromptContext {
        goal,
        history,
        images,
    } = build_regenerate_replay_payload(&session)
        .map_err(|error| error_response(StatusCode::CONFLICT, &error.to_string()))?;

    info!(goal = %goal, %session_id, "Web: regenerate_response");
    run_agent_from_history(&state, session_id, goal, history, images, req.run_id).await
}

// ============================================================================
// Mid-stream Messaging (3-tier)
// ============================================================================

#[derive(Deserialize)]
pub struct SteerRequest {
    pub message: String,
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    #[serde(alias = "sessionId")]
    pub session_id: Option<String>,
}

/// Inject a steering message (Tier 1) into the running agent.
pub(crate) async fn steer_agent(
    State(state): State<WebState>,
    Json(req): Json<SteerRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    if req.message.is_empty() {
        return Err(error_response(
            StatusCode::BAD_REQUEST,
            "Steering message must not be empty.",
        ));
    }
    let requested_session_id = parse_optional_session_id(req.session_id.as_deref())?;
    state
        .enqueue_message(
            ava_types::QueuedMessage {
                text: req.message,
                tier: queue_message_tier(ControlPlaneCommand::SteerAgent, None)
                    .expect("steer command should map to a queue tier"),
            },
            req.run_id.as_deref(),
            requested_session_id,
            false,
        )
        .await
        .map_err(|message| error_response(StatusCode::CONFLICT, &message))?;
    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct FollowUpRequest {
    pub message: String,
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    #[serde(alias = "sessionId")]
    pub session_id: Option<String>,
}

/// Queue a follow-up message (Tier 2) for after the current task.
pub(crate) async fn follow_up_agent(
    State(state): State<WebState>,
    Json(req): Json<FollowUpRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    if req.message.is_empty() {
        return Err(error_response(
            StatusCode::BAD_REQUEST,
            "Follow-up message must not be empty.",
        ));
    }
    let requested_session_id = parse_optional_session_id(req.session_id.as_deref())?;
    let message = ava_types::QueuedMessage {
        text: req.message,
        tier: queue_message_tier(ControlPlaneCommand::FollowUpAgent, None)
            .expect("follow-up command should map to a queue tier"),
    };
    state
        .enqueue_message(message, req.run_id.as_deref(), requested_session_id, true)
        .await
        .map_err(|message| error_response(StatusCode::CONFLICT, &message))?;
    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct PostCompleteRequest {
    pub message: String,
    #[serde(default = "default_group")]
    pub group: u32,
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    #[serde(alias = "sessionId")]
    pub session_id: Option<String>,
}

fn default_group() -> u32 {
    1
}

/// Queue a post-complete message (Tier 3) for after the agent stops.
pub(crate) async fn post_complete_agent(
    State(state): State<WebState>,
    Json(req): Json<PostCompleteRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    if req.message.is_empty() {
        return Err(error_response(
            StatusCode::BAD_REQUEST,
            "Post-complete message must not be empty.",
        ));
    }
    let requested_session_id = parse_optional_session_id(req.session_id.as_deref())?;
    let message = ava_types::QueuedMessage {
        text: req.message,
        tier: queue_message_tier(ControlPlaneCommand::PostCompleteAgent, Some(req.group))
            .expect("post-complete command should map to a queue tier"),
    };
    state
        .enqueue_message(message, req.run_id.as_deref(), requested_session_id, true)
        .await
        .map_err(|message| error_response(StatusCode::CONFLICT, &message))?;
    Ok(Json(serde_json::json!({ "ok": true })))
}

/// Get current message queue state.
pub(crate) async fn get_message_queue(
    State(state): State<WebState>,
    Query(req): Query<RunCorrelationRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let requested_session_id = parse_optional_session_id(req.session_id.as_deref())?;
    let snapshot = if req.run_id.is_some() || requested_session_id.is_some() {
        state
            .queue_dispatch_snapshot(req.run_id.as_deref(), requested_session_id)
            .await
            .map_err(|message| error_response(StatusCode::CONFLICT, &message))?
    } else {
        match state.resolve_run(None, None).await {
            Ok(run) => state
                .queue_dispatch_snapshot(Some(&run.run_id), None)
                .await
                .map_err(|message| error_response(StatusCode::CONFLICT, &message))?,
            Err(message) if message == "No active web runs" => None,
            Err(message) => return Err(error_response(StatusCode::CONFLICT, &message)),
        }
    };
    Ok(Json(serde_json::json!({
        "active": snapshot
            .as_ref()
            .is_some_and(|snapshot| snapshot.accepting && snapshot.tx.is_some())
    })))
}

// ============================================================================
// Context Compaction
// ============================================================================

#[derive(Deserialize)]
pub struct CompactContextRequest {
    #[serde(default)]
    pub messages: Vec<CompactMessageIn>,
    #[serde(default)]
    pub focus: Option<String>,
    #[serde(default)]
    pub context_window: Option<usize>,
    #[serde(default)]
    pub session_id: Option<String>,
    #[serde(default)]
    pub compaction_provider: Option<String>,
    #[serde(default)]
    pub compaction_model: Option<String>,
}

#[derive(Deserialize)]
pub struct CompactMessageIn {
    pub role: String,
    pub content: String,
}

#[derive(Serialize, Clone)]
pub struct CompactMessageOut {
    pub role: String,
    pub content: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CompactContextResponse {
    pub messages: Vec<CompactMessageOut>,
    pub tokens_before: usize,
    pub tokens_after: usize,
    pub tokens_saved: usize,
    pub messages_before: usize,
    pub messages_after: usize,
    pub summary: String,
    pub context_summary: String,
    pub usage_before_percent: f64,
}

/// Web-mode summarizer that delegates to an LLM provider.
struct WebSummarizer(std::sync::Arc<dyn ava_llm::provider::LLMProvider>);

#[async_trait::async_trait]
impl ava_context::strategies::Summarizer for WebSummarizer {
    async fn summarize(&self, text: &str) -> std::result::Result<String, String> {
        let messages = vec![ava_types::Message::new(
            ava_types::Role::User,
            text.to_string(),
        )];
        self.0.generate(&messages).await.map_err(|e| e.to_string())
    }
}

fn parse_role(role: &str) -> ava_types::Role {
    match role {
        "user" => ava_types::Role::User,
        "assistant" => ava_types::Role::Assistant,
        "tool" => ava_types::Role::Tool,
        _ => ava_types::Role::System,
    }
}

fn role_to_str(role: &ava_types::Role) -> &'static str {
    match role {
        ava_types::Role::User => "user",
        ava_types::Role::Assistant => "assistant",
        ava_types::Role::Tool => "tool",
        ava_types::Role::System => "system",
    }
}

fn to_compact_messages(messages: &[ava_types::Message]) -> Vec<CompactMessageOut> {
    messages
        .iter()
        .map(|m| CompactMessageOut {
            role: role_to_str(&m.role).to_string(),
            content: m.content.clone(),
        })
        .collect()
}

fn extract_context_summary(messages: &[ava_types::Message]) -> Option<String> {
    messages
        .iter()
        .rev()
        .find(|m| {
            m.role == ava_types::Role::System && m.content.starts_with("## Conversation Summary")
        })
        .map(|m| m.content.clone())
}

fn is_compaction_summary(message: &ava_types::Message) -> bool {
    message.role == ava_types::Role::System
        && message.content.starts_with("## Conversation Summary")
}

/// Compact the conversation context, mirroring the Tauri `compact_context` command.
///
/// Accepts messages from the frontend (or loads them from the session DB) and
/// runs hybrid compaction (tool truncation + sliding window + optional LLM
/// summarization). The compacted messages are persisted back to the session.
pub(crate) async fn compact_context(
    State(state): State<WebState>,
    Json(req): Json<CompactContextRequest>,
) -> Result<Json<CompactContextResponse>, (StatusCode, Json<ErrorResponse>)> {
    let requested_session_id = req
        .session_id
        .as_deref()
        .map(uuid::Uuid::parse_str)
        .transpose()
        .map_err(|e| {
            error_response(StatusCode::BAD_REQUEST, &format!("invalid session id: {e}"))
        })?;
    let session_uuid =
        resolve_web_compaction_session(requested_session_id).map(|selection| selection.session_id);

    let existing_session =
        session_uuid.and_then(|id| state.inner.stack.session_manager.get(id).ok().flatten());

    let source_messages = existing_session
        .as_ref()
        .map(|s| s.messages.clone())
        .filter(|msgs| !msgs.is_empty())
        .unwrap_or_else(|| {
            req.messages
                .iter()
                .map(|m| ava_types::Message::new(parse_role(&m.role), &m.content))
                .collect()
        });

    if source_messages.is_empty() {
        return Ok(Json(CompactContextResponse {
            messages: Vec::new(),
            tokens_before: 0,
            tokens_after: 0,
            tokens_saved: 0,
            messages_before: 0,
            messages_after: 0,
            summary: "Nothing to compact -- conversation is empty.".to_string(),
            context_summary: String::new(),
            usage_before_percent: 0.0,
        }));
    }

    let context_window = req.context_window.unwrap_or(128_000);
    let tokens_before: usize = source_messages
        .iter()
        .map(ava_context::estimate_tokens_for_message)
        .sum();
    let usage_before_percent = if context_window == 0 {
        0.0
    } else {
        (tokens_before as f64 / context_window as f64) * 100.0
    };

    // Resolve the LLM provider for summarization
    let provider = if let (Some(ref prov), Some(ref model)) =
        (&req.compaction_provider, &req.compaction_model)
    {
        state
            .inner
            .stack
            .router
            .route_required(prov, model)
            .await
            .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
    } else {
        let (prov, model) = state.inner.stack.current_model().await;
        state
            .inner
            .stack
            .router
            .route_required(&prov, &model)
            .await
            .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
    };

    let summarizer: std::sync::Arc<dyn ava_context::strategies::Summarizer> =
        std::sync::Arc::new(WebSummarizer(provider));
    let config = ava_context::CondenserConfig {
        max_tokens: context_window,
        target_tokens: context_window * 3 / 4,
        preserve_recent_messages: 4,
        preserve_recent_turns: 2,
        focus: req.focus.clone(),
        ..Default::default()
    };
    let mut condenser = ava_context::create_hybrid_condenser(config, Some(summarizer));

    let condensed = condenser
        .force_condense(&source_messages)
        .await
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    let tokens_after = condensed.estimated_tokens;
    let messages_after = condensed.messages.len();
    let context_summary = extract_context_summary(&condensed.messages).unwrap_or_default();

    // If compaction didn't reduce anything, return as-is
    if messages_after >= source_messages.len() && tokens_after >= tokens_before {
        return Ok(Json(CompactContextResponse {
            messages: to_compact_messages(&source_messages),
            tokens_before,
            tokens_after: tokens_before,
            tokens_saved: 0,
            messages_before: source_messages.len(),
            messages_after: source_messages.len(),
            summary: format!(
                "Conversation is already compact. {} tokens across {} messages.",
                tokens_before,
                source_messages.len()
            ),
            context_summary: String::new(),
            usage_before_percent,
        }));
    }

    // Persist the compacted session
    if let Some(session_uuid) = session_uuid {
        let mut active_messages = condensed.messages.clone();
        if let Some(summary_index) = active_messages.iter().position(is_compaction_summary) {
            let next_timestamp = active_messages
                .iter()
                .skip(summary_index + 1)
                .find(|m| !is_compaction_summary(m))
                .map(|m| m.timestamp);
            let previous_timestamp = condensed
                .compacted_messages
                .last()
                .map(|m| m.timestamp)
                .or_else(|| {
                    active_messages[..summary_index]
                        .iter()
                        .rev()
                        .find(|m| !is_compaction_summary(m))
                        .map(|m| m.timestamp)
                });

            if let Some(summary) = active_messages.get_mut(summary_index) {
                if let Some(ts) = next_timestamp {
                    summary.timestamp = ts - chrono::Duration::milliseconds(1);
                } else if let Some(ts) = previous_timestamp {
                    summary.timestamp = ts + chrono::Duration::milliseconds(1);
                }
            }
        }

        let mut persisted_messages = condensed.compacted_messages.clone();
        persisted_messages.extend(active_messages);
        persisted_messages.sort_by_key(|m| m.timestamp);

        let session = {
            let mut s = existing_session
                .clone()
                .unwrap_or_else(|| ava_types::Session::new().with_id(session_uuid));
            s.messages = persisted_messages;
            s.updated_at = chrono::Utc::now();
            s
        };
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;
        *state.inner.last_session_id.write().await = Some(session_uuid);
    }

    let saved = tokens_before.saturating_sub(tokens_after);
    let condensed_count = source_messages.len().saturating_sub(messages_after);
    let summary = match req.focus.as_deref().filter(|f| !f.trim().is_empty()) {
        Some(focus) => format!(
            "Conversation compacted (focus: {focus}): {} messages -> summary (saved {saved} tokens, condensed {condensed_count} messages).",
            source_messages.len()
        ),
        None => format!(
            "Conversation compacted: {} messages -> summary (saved {saved} tokens, condensed {condensed_count} messages).",
            source_messages.len()
        ),
    };

    Ok(Json(CompactContextResponse {
        messages: to_compact_messages(&condensed.messages),
        tokens_before,
        tokens_after,
        tokens_saved: saved,
        messages_before: source_messages.len(),
        messages_after,
        summary,
        context_summary,
        usage_before_percent,
    }))
}

/// Clear the message queue.
///
/// Cancels the agent for "all" and "steering" targets (which clears the steering
/// queue). Follow-up and post-complete clears are rejected until a real drain
/// path exists.
pub(crate) async fn clear_message_queue(
    State(state): State<WebState>,
    body: Option<Json<serde_json::Value>>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let body = body.map(|Json(body)| body).unwrap_or_default();
    let target_str = body
        .get("target")
        .and_then(|v| v.as_str())
        .map(String::from)
        .unwrap_or_else(|| "all".to_string());
    let requested_run_id = body
        .get("run_id")
        .or_else(|| body.get("runId"))
        .and_then(|v| v.as_str())
        .map(str::to_string);
    let requested_session_id = parse_optional_session_id(
        body.get("session_id")
            .or_else(|| body.get("sessionId"))
            .and_then(|v| v.as_str()),
    )?;

    match resolve_web_queue_clear_semantics(&target_str)
        .map_err(|message| error_response(StatusCode::BAD_REQUEST, &message))?
    {
        QueueClearSemantics::CancelRunAndClearSteering => {
            let run = if requested_run_id.is_some() || requested_session_id.is_some() {
                state
                    .resolve_run(requested_run_id.as_deref(), requested_session_id)
                    .await
                    .map_err(|message| error_response(StatusCode::CONFLICT, &message))?
            } else {
                state
                    .resolve_run(None, None)
                    .await
                    .map_err(|message| error_response(StatusCode::CONFLICT, &message))?
            };
            cancel_run(&state, &run.run_id).await;
            Ok(Json(serde_json::json!({ "ok": true })))
        }
        QueueClearSemantics::Unsupported => {
            unreachable!("unsupported targets should have been rejected before execution")
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_image(label: &str) -> ava_types::ImageContent {
        ava_types::ImageContent::new(label, ava_types::ImageMediaType::Png)
    }

    #[test]
    fn ensure_web_run_id_generates_stable_prefixed_id() {
        let generated = ensure_web_run_id(None);
        assert!(generated.starts_with("web-run-"));
        assert_eq!(
            ensure_web_run_id(Some("web-run-existing".to_string())),
            "web-run-existing"
        );
    }

    #[test]
    fn web_submit_session_precedence_prefers_requested_session() {
        let requested = uuid::Uuid::new_v4();

        assert_eq!(
            resolve_web_submit_session(Some(requested)),
            SessionSelection {
                session_id: requested,
                source: SessionSelectionSource::Requested,
            }
        );
    }

    #[test]
    fn web_submit_session_no_requested_session_starts_new_session() {
        assert_eq!(
            resolve_web_submit_session(None).source,
            SessionSelectionSource::New,
        );
    }

    #[test]
    fn web_compaction_session_without_request_does_not_fallback_to_last_active() {
        assert!(
            resolve_web_compaction_session(None).is_none(),
            "omitted compaction session_id should not resolve to last_session_id fallback"
        );
    }

    #[test]
    fn web_compaction_session_precedence_prefers_requested_session() {
        let requested = uuid::Uuid::new_v4();

        assert_eq!(
            resolve_web_compaction_session(Some(requested)),
            Some(SessionSelection {
                session_id: requested,
                source: SessionSelectionSource::Requested,
            })
        );
    }

    #[test]
    fn web_replay_session_without_request_does_not_fallback_to_last_active() {
        assert!(
            resolve_web_replay_session(None).is_none(),
            "omitted replay session_id should not resolve to last_session_id fallback"
        );
    }

    #[test]
    fn web_replay_session_precedence_prefers_requested_session() {
        let requested = uuid::Uuid::new_v4();

        assert_eq!(
            resolve_web_replay_session(Some(requested)),
            Some(SessionSelection {
                session_id: requested,
                source: SessionSelectionSource::Requested,
            })
        );
    }

    #[test]
    fn web_queue_clear_supports_all_target() {
        assert_eq!(
            resolve_web_queue_clear_semantics("all").expect("all target should be supported"),
            QueueClearSemantics::CancelRunAndClearSteering
        );
    }

    #[test]
    fn web_queue_clear_rejects_unimplemented_targets() {
        let error = resolve_web_queue_clear_semantics("followUp")
            .expect_err("follow-up target should be rejected");
        assert_eq!(error, UNSUPPORTED_QUEUE_CLEAR_ERROR);
    }

    #[test]
    fn run_correlation_request_accepts_camel_case_aliases() {
        let parsed: RunCorrelationRequest = serde_json::from_value(serde_json::json!({
            "runId": "web-run-1",
            "sessionId": "00000000-0000-0000-0000-000000000001"
        }))
        .expect("camelCase correlation payload should deserialize");
        assert_eq!(parsed.run_id.as_deref(), Some("web-run-1"));
        assert_eq!(
            parsed.session_id.as_deref(),
            Some("00000000-0000-0000-0000-000000000001")
        );
    }

    #[test]
    fn steer_follow_up_and_post_complete_accept_camel_case_aliases() {
        let steer: SteerRequest = serde_json::from_value(serde_json::json!({
            "message": "steer",
            "runId": "web-run-1",
            "sessionId": "00000000-0000-0000-0000-000000000001"
        }))
        .expect("steer camelCase payload should deserialize");
        assert_eq!(steer.run_id.as_deref(), Some("web-run-1"));
        assert_eq!(
            steer.session_id.as_deref(),
            Some("00000000-0000-0000-0000-000000000001")
        );

        let follow_up: FollowUpRequest = serde_json::from_value(serde_json::json!({
            "message": "follow",
            "runId": "web-run-2",
            "sessionId": "00000000-0000-0000-0000-000000000002"
        }))
        .expect("follow-up camelCase payload should deserialize");
        assert_eq!(follow_up.run_id.as_deref(), Some("web-run-2"));
        assert_eq!(
            follow_up.session_id.as_deref(),
            Some("00000000-0000-0000-0000-000000000002")
        );

        let post_complete: PostCompleteRequest = serde_json::from_value(serde_json::json!({
            "message": "post",
            "group": 3,
            "runId": "web-run-3",
            "sessionId": "00000000-0000-0000-0000-000000000003"
        }))
        .expect("post-complete camelCase payload should deserialize");
        assert_eq!(post_complete.run_id.as_deref(), Some("web-run-3"));
        assert_eq!(
            post_complete.session_id.as_deref(),
            Some("00000000-0000-0000-0000-000000000003")
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

    #[test]
    fn retry_replay_payload_preserves_user_images() {
        let mut session = ava_types::Session::new();
        session.add_message(sample_user_message("describe", vec![sample_image("retry")]));

        let SessionPromptContext { images, .. } =
            build_retry_replay_payload(&session).expect("retry payload");
        assert_eq!(images, vec![sample_image("retry")]);
    }

    #[test]
    fn edit_replay_payload_preserves_target_images() {
        let mut session = ava_types::Session::new();
        let target = sample_user_message("before", vec![sample_image("edit")]);
        let target_id = target.id;
        session.add_message(target);

        let SessionPromptContext { images, .. } =
            build_edit_replay_payload(&session, Some(target_id), "after".to_string())
                .expect("edit payload");
        assert_eq!(images, vec![sample_image("edit")]);
    }

    #[test]
    fn edit_replay_payload_rejects_missing_targets() {
        let session = ava_types::Session::new();

        let error = build_edit_replay_payload(&session, None, "after".to_string())
            .expect_err("missing target should fail");
        assert!(error.to_string().contains("Invalid message ID"));
    }

    #[test]
    fn edit_replay_payload_rejects_non_user_targets() {
        let mut session = ava_types::Session::new();
        let assistant = ava_types::Message::new(ava_types::Role::Assistant, "done");
        let assistant_id = assistant.id;
        session.add_message(assistant);

        let error = build_edit_replay_payload(&session, Some(assistant_id), "after".to_string())
            .expect_err("assistant target should fail");
        assert!(error.to_string().contains("Only user messages"));
    }

    #[test]
    fn regenerate_replay_payload_preserves_last_user_images() {
        let mut session = ava_types::Session::new();
        session.add_message(sample_user_message("before", vec![sample_image("regen")]));
        session.add_message(ava_types::Message::new(ava_types::Role::Assistant, "done"));

        let SessionPromptContext { images, .. } =
            build_regenerate_replay_payload(&session).expect("regen payload");
        assert_eq!(images, vec![sample_image("regen")]);
    }
}
