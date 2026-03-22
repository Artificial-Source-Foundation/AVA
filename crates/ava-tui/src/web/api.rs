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

use super::state::{FileEditRecord, PlanStepPayload, WebEvent, WebState};

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

    // If a session_id was provided, use it so frontend and backend share the same ID.
    // Also load that session's messages as history if it already exists in the DB.
    let (session_uuid, history) =
        if let Some(ref sid) = req.session_id {
            let uuid = uuid::Uuid::parse_str(sid).map_err(|e| {
                error_response(StatusCode::BAD_REQUEST, &format!("Invalid session_id: {e}"))
            })?;
            let session =
                state.inner.stack.session_manager.get(uuid).map_err(|e| {
                    error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string())
                })?;
            let msgs = session.map(|s| s.messages).unwrap_or_default();
            (uuid, msgs)
        } else {
            (uuid::Uuid::new_v4(), vec![])
        };
    let session_id_str = session_uuid.to_string();

    let cancel = state.new_cancel_token().await;
    let inner = state.inner.clone();
    let stack = inner.stack.clone();

    let max_turns = if req.max_turns > 0 { req.max_turns } else { 0 };
    let goal = req.goal.clone();

    info!(goal = %goal, max_turns, "Web: starting agent (async)");

    // Create message queue for mid-stream messaging (3-tier)
    let (msg_queue, msg_queue_tx) = stack.create_message_queue();
    *state.inner.message_queue.write().await = Some(msg_queue_tx);

    // Take approval/question/plan receivers out of the state for this run
    let mut approval_rx = {
        let mut lock = inner.approval_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };
    let mut question_rx = {
        let mut lock = inner.question_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };
    let mut plan_rx = {
        let mut lock = inner.plan_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };

    let pending_approval = inner.pending_approval_reply.clone();
    let pending_question = inner.pending_question_reply.clone();
    let pending_plan = inner.pending_plan_reply.clone();
    let edit_history = inner.edit_history.clone();

    // Spawn the agent run in a background task
    tokio::spawn(async move {
        // Create an mpsc channel for the agent to send events into,
        // then forward those events to the broadcast channel.
        let (tx, mut rx) = mpsc::unbounded_channel();
        let event_broadcast = inner.event_tx.clone();
        let edit_hist = edit_history.clone();

        // Forward raw agent events to the WS broadcast channel, tracking edits
        let checkpoint_stack = stack.clone();
        let forwarder = tokio::spawn(async move {
            while let Some(event) = rx.recv().await {
                // Checkpoint: incrementally save session so progress survives crashes
                if let ava_agent::agent_loop::AgentEvent::Checkpoint(ref session) = event {
                    let _ = checkpoint_stack.session_manager.save(session);
                    continue; // Don't forward checkpoint events to WebSocket clients
                }
                // Track write/edit tool calls for undo support
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
                }
                let _ = event_broadcast.send(WebEvent::Agent(event));
            }
        });

        // Forward approval requests as WebEvent::ApprovalRequest
        let event_broadcast_approval = inner.event_tx.clone();
        let approval_forwarder = tokio::spawn(async move {
            while let Some(req) = approval_rx.recv().await {
                let risk_level = format!("{:?}", req.inspection.risk_level).to_lowercase();
                let id = format!("approval-{}", uuid::Uuid::new_v4());
                *pending_approval.lock().await = Some(req.reply);
                let _ = event_broadcast_approval.send(WebEvent::ApprovalRequest {
                    id,
                    tool_name: req.call.name.clone(),
                    args: req.call.arguments.clone(),
                    risk_level,
                    reason: req.inspection.reason.clone(),
                    warnings: req.inspection.warnings.clone(),
                });
            }
        });

        // Forward question requests as WebEvent::QuestionRequest
        let event_broadcast_question = inner.event_tx.clone();
        let question_forwarder = tokio::spawn(async move {
            while let Some(req) = question_rx.recv().await {
                let id = format!("question-{}", uuid::Uuid::new_v4());
                *pending_question.lock().await = Some(req.reply);
                let _ = event_broadcast_question.send(WebEvent::QuestionRequest {
                    id,
                    question: req.question.clone(),
                    options: req.options.clone(),
                });
            }
        });

        // Forward plan requests as WebEvent::PlanCreated
        let event_broadcast_plan = inner.event_tx.clone();
        let plan_forwarder = tokio::spawn(async move {
            while let Some(req) = plan_rx.recv().await {
                *pending_plan.lock().await = Some(req.reply);
                let steps: Vec<PlanStepPayload> = req
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
                let _ = event_broadcast_plan.send(WebEvent::PlanCreated {
                    summary: req.plan.summary.clone(),
                    steps,
                    estimated_turns: req.plan.estimated_turns.unwrap_or(0) as usize,
                });
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
                vec![],             // no images
                Some(session_uuid), // use frontend's session ID
            )
            .await;

        // Wait for the forwarder to drain; abort the interactive forwarders
        let _ = forwarder.await;
        approval_forwarder.abort();
        question_forwarder.abort();
        plan_forwarder.abort();

        // Clear the message queue sender and mark as not running
        *inner.message_queue.write().await = None;
        *inner.running.write().await = false;

        match result {
            Ok(run_result) => {
                match stack.session_manager.save(&run_result.session) {
                    Ok(()) => {
                        *inner.last_session_id.write().await = Some(run_result.session.id);
                    }
                    Err(e) => {
                        // Log the error but still record the session ID so that
                        // retry/regenerate can attempt a re-save rather than returning
                        // a confusing 404 "session not found" when the session object
                        // is valid but the DB write failed transiently.
                        tracing::error!("Failed to persist session {}: {e}", run_result.session.id);
                        *inner.last_session_id.write().await = Some(run_result.session.id);
                    }
                }
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
    // Clear any pending interactive replies
    let _ = state.inner.pending_approval_reply.lock().await.take();
    let _ = state.inner.pending_question_reply.lock().await.take();
    let _ = state.inner.pending_plan_reply.lock().await.take();
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
// Approval / Question / Plan resolution
// ============================================================================

#[derive(Deserialize)]
pub struct ResolveApprovalRequest {
    pub approved: bool,
    #[serde(default)]
    pub always_allow: bool,
}

/// Resolve a pending tool approval request.
///
/// The frontend calls this after the user clicks Approve or Deny in the ApprovalDock.
pub async fn resolve_approval(
    State(state): State<WebState>,
    Json(req): Json<ResolveApprovalRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    use ava_tools::permission_middleware::ToolApproval;

    let reply = state
        .inner
        .pending_approval_reply
        .lock()
        .await
        .take()
        .ok_or_else(|| error_response(StatusCode::CONFLICT, "No pending approval request"))?;

    let approval = if req.approved {
        if req.always_allow {
            ToolApproval::AllowAlways
        } else {
            ToolApproval::AllowedForSession
        }
    } else {
        ToolApproval::Rejected(Some("User denied via web UI".to_string()))
    };

    reply.send(approval).map_err(|_| {
        error_response(
            StatusCode::GONE,
            "Failed to send approval — the agent may have already moved on",
        )
    })?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct ResolveQuestionRequest {
    pub answer: String,
}

/// Resolve a pending question request.
pub async fn resolve_question(
    State(state): State<WebState>,
    Json(req): Json<ResolveQuestionRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let reply = state
        .inner
        .pending_question_reply
        .lock()
        .await
        .take()
        .ok_or_else(|| error_response(StatusCode::CONFLICT, "No pending question request"))?;

    reply.send(req.answer).map_err(|_| {
        error_response(
            StatusCode::GONE,
            "Failed to send answer — the agent may have already moved on",
        )
    })?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct ResolvePlanRequest {
    pub response: String,
    #[serde(default)]
    pub modified_plan: Option<serde_json::Value>,
    #[serde(default)]
    pub feedback: Option<String>,
    #[serde(default)]
    pub step_comments: Option<std::collections::HashMap<String, String>>,
}

/// Resolve a pending plan approval request.
pub async fn resolve_plan(
    State(state): State<WebState>,
    Json(req): Json<ResolvePlanRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let reply = state
        .inner
        .pending_plan_reply
        .lock()
        .await
        .take()
        .ok_or_else(|| error_response(StatusCode::CONFLICT, "No pending plan request"))?;

    let feedback = req.feedback.unwrap_or_default();
    let decision = match req.response.as_str() {
        "approved" => ava_types::PlanDecision::Approved,
        "rejected" => ava_types::PlanDecision::Rejected { feedback },
        "modified" => {
            let plan: ava_types::Plan = req
                .modified_plan
                .ok_or_else(|| {
                    error_response(
                        StatusCode::BAD_REQUEST,
                        "modified_plan is required for 'modified' response",
                    )
                })
                .and_then(|v| {
                    serde_json::from_value(v).map_err(|e| {
                        error_response(
                            StatusCode::BAD_REQUEST,
                            &format!("Invalid modified_plan: {e}"),
                        )
                    })
                })?;
            ava_types::PlanDecision::Modified { plan, feedback }
        }
        other => {
            return Err(error_response(
                StatusCode::BAD_REQUEST,
                &format!(
                    "Invalid response '{other}'. Expected 'approved', 'rejected', or 'modified'"
                ),
            ));
        }
    };

    reply.send(decision).map_err(|_| {
        error_response(
            StatusCode::GONE,
            "Failed to send plan decision — the agent may have already moved on",
        )
    })?;

    Ok(Json(serde_json::json!({ "ok": true })))
}

// ============================================================================
// Retry / Edit+Resend / Regenerate / Undo
// ============================================================================

/// Helper: run a new agent task with the given goal and history, returning
/// the same format as SubmitGoalResponse. Used by retry/regenerate/edit-resend.
async fn run_agent_from_history(
    state: &WebState,
    goal: String,
    history: Vec<ava_types::Message>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    // Build a fake SubmitGoalRequest and reuse submit_goal logic
    let req = SubmitGoalRequest {
        goal,
        max_turns: 0,
        provider: None,
        model: None,
        session_id: None,
    };
    // We override the history by temporarily injecting it via a one-shot path.
    // Since submit_goal loads history from session_id, we instead call the inner
    // logic directly here.
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

    let session_id_str = uuid::Uuid::new_v4().to_string();
    let cancel = state.new_cancel_token().await;
    let inner = state.inner.clone();
    let stack = inner.stack.clone();
    let max_turns = 0usize;
    let goal = req.goal.clone();

    let (msg_queue, msg_queue_tx) = stack.create_message_queue();
    *state.inner.message_queue.write().await = Some(msg_queue_tx);

    let mut approval_rx = {
        let mut lock = inner.approval_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };
    let mut question_rx = {
        let mut lock = inner.question_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };
    let mut plan_rx = {
        let mut lock = inner.plan_rx.lock().await;
        let (_, empty) = mpsc::unbounded_channel();
        std::mem::replace(&mut *lock, empty)
    };

    let pending_approval = inner.pending_approval_reply.clone();
    let pending_question = inner.pending_question_reply.clone();
    let pending_plan = inner.pending_plan_reply.clone();
    let edit_history = inner.edit_history.clone();

    tokio::spawn(async move {
        let (tx, mut rx) = mpsc::unbounded_channel();
        let event_broadcast = inner.event_tx.clone();
        let edit_hist = edit_history.clone();

        let forwarder = tokio::spawn(async move {
            while let Some(event) = rx.recv().await {
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
                }
                let _ = event_broadcast.send(WebEvent::Agent(event));
            }
        });

        let event_broadcast_approval = inner.event_tx.clone();
        let approval_forwarder = tokio::spawn(async move {
            while let Some(req) = approval_rx.recv().await {
                let risk_level = format!("{:?}", req.inspection.risk_level).to_lowercase();
                let id = format!("approval-{}", uuid::Uuid::new_v4());
                *pending_approval.lock().await = Some(req.reply);
                let _ = event_broadcast_approval.send(WebEvent::ApprovalRequest {
                    id,
                    tool_name: req.call.name.clone(),
                    args: req.call.arguments.clone(),
                    risk_level,
                    reason: req.inspection.reason.clone(),
                    warnings: req.inspection.warnings.clone(),
                });
            }
        });

        let event_broadcast_question = inner.event_tx.clone();
        let question_forwarder = tokio::spawn(async move {
            while let Some(req) = question_rx.recv().await {
                let id = format!("question-{}", uuid::Uuid::new_v4());
                *pending_question.lock().await = Some(req.reply);
                let _ = event_broadcast_question.send(WebEvent::QuestionRequest {
                    id,
                    question: req.question.clone(),
                    options: req.options.clone(),
                });
            }
        });

        let event_broadcast_plan = inner.event_tx.clone();
        let plan_forwarder = tokio::spawn(async move {
            while let Some(req) = plan_rx.recv().await {
                *pending_plan.lock().await = Some(req.reply);
                let steps: Vec<PlanStepPayload> = req
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
                let _ = event_broadcast_plan.send(WebEvent::PlanCreated {
                    summary: req.plan.summary.clone(),
                    steps,
                    estimated_turns: req.plan.estimated_turns.unwrap_or(0) as usize,
                });
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
                vec![],
                None, // retry/regenerate — let backend generate a new session ID
            )
            .await;

        let _ = forwarder.await;
        approval_forwarder.abort();
        question_forwarder.abort();
        plan_forwarder.abort();

        *inner.message_queue.write().await = None;
        *inner.running.write().await = false;

        match result {
            Ok(run_result) => {
                if let Err(e) = stack.session_manager.save(&run_result.session) {
                    tracing::error!(
                        "Failed to persist session {} (retry/regen): {e}",
                        run_result.session.id
                    );
                }
                *inner.last_session_id.write().await = Some(run_result.session.id);
            }
            Err(e) => {
                tracing::error!("Agent run (retry/regen) failed: {e}");
            }
        }
    });

    Ok(Json(SubmitGoalResponse {
        success: true,
        turns: 0,
        session_id: session_id_str,
    }))
}

/// Retry the last user message.
pub async fn retry_last_message(
    State(state): State<WebState>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let session_id = state
        .inner
        .last_session_id
        .read()
        .await
        .ok_or_else(|| error_response(StatusCode::CONFLICT, "No previous session to retry"))?;

    let session = state
        .inner
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    let last_user_msg = session
        .messages
        .iter()
        .rev()
        .find(|m| m.role == ava_types::Role::User)
        .ok_or_else(|| error_response(StatusCode::CONFLICT, "No user message found in session"))?;

    let goal = last_user_msg.content.clone();
    let history = collect_history_before_last_user(&session.messages);

    info!(goal = %goal, %session_id, "Web: retry_last_message");
    run_agent_from_history(&state, goal, history).await
}

#[derive(Deserialize)]
pub struct EditAndResendRequest {
    pub message_id: String,
    pub new_content: String,
}

/// Edit a specific user message and re-run the agent from that point.
pub async fn edit_and_resend(
    State(state): State<WebState>,
    Json(req): Json<EditAndResendRequest>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let session_id = state
        .inner
        .last_session_id
        .read()
        .await
        .ok_or_else(|| error_response(StatusCode::CONFLICT, "No previous session to edit"))?;

    let session = state
        .inner
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    let target_id = uuid::Uuid::parse_str(&req.message_id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid message_id: {e}"))
    })?;

    let pos = session
        .messages
        .iter()
        .position(|m| m.id == target_id)
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Message not found in session"))?;

    let history: Vec<ava_types::Message> = session.messages[..pos].to_vec();

    info!(new_content = %req.new_content, message_id = %req.message_id, "Web: edit_and_resend");
    run_agent_from_history(&state, req.new_content, history).await
}

/// Regenerate the last assistant response.
pub async fn regenerate_response(
    State(state): State<WebState>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let session_id = state.inner.last_session_id.read().await.ok_or_else(|| {
        error_response(
            StatusCode::CONFLICT,
            "No previous session to regenerate from",
        )
    })?;

    let session = state
        .inner
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    let last_user_pos = session
        .messages
        .iter()
        .rposition(|m| m.role == ava_types::Role::User)
        .ok_or_else(|| {
            error_response(
                StatusCode::CONFLICT,
                "No user message found in session to regenerate from",
            )
        })?;

    let goal = session.messages[last_user_pos].content.clone();
    let history: Vec<ava_types::Message> = session.messages[..last_user_pos].to_vec();

    info!(goal = %goal, %session_id, "Web: regenerate_response");
    run_agent_from_history(&state, goal, history).await
}

#[derive(Serialize)]
pub struct UndoResult {
    pub success: bool,
    pub message: String,
    #[serde(rename = "filePath")]
    pub file_path: Option<String>,
}

/// Undo the last file edit made by the agent.
pub async fn undo_last_edit(
    State(state): State<WebState>,
) -> Result<Json<UndoResult>, (StatusCode, Json<ErrorResponse>)> {
    let record = state.inner.edit_history.write().await.pop_back();

    match record {
        Some(edit) => {
            let path = edit.file_path.clone();
            match tokio::fs::write(&edit.file_path, &edit.previous_content).await {
                Ok(()) => {
                    info!(file = %path, "Web: undo_last_edit restored file");
                    Ok(Json(UndoResult {
                        success: true,
                        message: format!("Restored {path} to its previous content"),
                        file_path: Some(path),
                    }))
                }
                Err(e) => Ok(Json(UndoResult {
                    success: false,
                    message: format!("Failed to restore {path}: {e}"),
                    file_path: Some(path),
                })),
            }
        }
        None => Ok(Json(UndoResult {
            success: false,
            message: "No file edits to undo".to_string(),
            file_path: None,
        })),
    }
}

/// Collect all messages before the last user message (for retry/regenerate).
fn collect_history_before_last_user(messages: &[ava_types::Message]) -> Vec<ava_types::Message> {
    if let Some(pos) = messages
        .iter()
        .rposition(|m| m.role == ava_types::Role::User)
    {
        messages[..pos].to_vec()
    } else {
        vec![]
    }
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
pub async fn get_session_messages(
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

#[derive(Deserialize)]
pub struct SearchSessionsRequest {
    pub query: String,
}

/// Search sessions by message content.
pub async fn search_sessions(
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

// ── Messages ─────────────────────────────────────────────────────────────────

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

// ── Update message ────────────────────────────────────────────────────────────

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
pub async fn update_message(
    State(state): State<WebState>,
    Path((id, msg_id)): Path<(String, String)>,
    Json(req): Json<UpdateMessageRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let session_uuid = uuid::Uuid::parse_str(&id).map_err(|e| {
        error_response(StatusCode::BAD_REQUEST, &format!("Invalid session ID: {e}"))
    })?;
    // Accept both UUID and custom string IDs (frontend uses "asst-{timestamp}-{random}")
    let msg_uuid_opt = uuid::Uuid::parse_str(&msg_id).ok();

    let mut session = state
        .inner
        .stack
        .session_manager
        .get(session_uuid)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Session not found"))?;

    let msg = session
        .messages
        .iter_mut()
        .find(|m| {
            if let Some(ref uuid) = msg_uuid_opt {
                m.id == *uuid
            } else {
                m.id.to_string() == msg_id
            }
        })
        .ok_or_else(|| error_response(StatusCode::NOT_FOUND, "Message not found"))?;

    if let Some(content) = req.content {
        msg.content = content;
    }

    // Metadata, tokens, cost, model are frontend-only fields not stored in
    // ava_types::Message.  We persist the content update in the backend
    // session (for LLM context) and return success; the frontend's in-memory
    // store holds the rich metadata for its own display needs.
    session.updated_at = chrono::Utc::now();

    state
        .inner
        .stack
        .session_manager
        .save(&session)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;

    Ok(Json(serde_json::json!({ "ok": true })))
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

#[derive(Serialize)]
pub struct CurrentModel {
    pub provider: String,
    pub model: String,
}

/// Get the currently-active provider and model.
pub async fn get_current_model(State(state): State<WebState>) -> impl IntoResponse {
    let (provider, model) = state.inner.stack.current_model().await;
    Json(CurrentModel { provider, model })
}

#[derive(Deserialize)]
pub struct SwitchModelRequest {
    pub provider: String,
    pub model: String,
}

/// Switch the active provider and model.
pub async fn switch_model(
    State(state): State<WebState>,
    Json(req): Json<SwitchModelRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
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
pub async fn list_providers(State(state): State<WebState>) -> impl IntoResponse {
    let names = state.inner.stack.router.available_providers().await;
    let providers: Vec<ProviderInfo> = names
        .into_iter()
        .map(|name| ProviderInfo { name })
        .collect();
    Json(providers)
}

// ============================================================================
// Config
// ============================================================================

/// Get the full configuration as JSON.
pub async fn get_config(
    State(state): State<WebState>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let cfg = state.inner.stack.config.get().await;
    let value = serde_json::to_value(&cfg)
        .map_err(|e| error_response(StatusCode::INTERNAL_SERVER_ERROR, &e.to_string()))?;
    Ok(Json(value))
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
    pub status: String,
}

/// List all configured MCP servers with their connection status and tool count.
pub async fn list_mcp_servers(State(state): State<WebState>) -> impl IntoResponse {
    let servers = state.inner.stack.mcp_server_info().await;
    let response: Vec<McpServerResponse> = servers
        .into_iter()
        .map(|s| {
            let status = match &s.status {
                ava_agent::stack::McpServerStatus::Connected => "connected",
                ava_agent::stack::McpServerStatus::Disabled => "disabled",
                ava_agent::stack::McpServerStatus::Failed(_) => "failed",
                ava_agent::stack::McpServerStatus::Connecting => "connecting",
            }
            .to_string();
            McpServerResponse {
                name: s.name,
                tool_count: s.tool_count,
                scope: s.scope.to_string(),
                enabled: s.enabled,
                status,
            }
        })
        .collect();
    Json(response)
}

#[derive(Deserialize)]
pub struct McpServerNamePath {
    pub name: String,
}

/// Enable a previously disabled MCP server.
pub async fn enable_mcp_server(
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
pub async fn disable_mcp_server(
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
pub async fn reload_mcp(
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
pub async fn list_plugins(State(state): State<WebState>) -> impl IntoResponse {
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
pub async fn get_permission_level(State(state): State<WebState>) -> impl IntoResponse {
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
pub async fn set_permission_level(
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
pub async fn toggle_permission_level(State(state): State<WebState>) -> impl IntoResponse {
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

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ClearTarget {
    All,
    Steering,
    FollowUp,
    PostComplete,
}

/// Clear the message queue.
///
/// Cancels the agent for "all" and "steering" targets (which clears the steering
/// queue). For follow-up and post-complete, returns OK — those are drained by
/// the agent loop when it processes them; there is no safe external drain path.
pub async fn clear_message_queue(
    State(state): State<WebState>,
    body: Option<Json<serde_json::Value>>,
) -> impl IntoResponse {
    let target_str = body
        .and_then(|b| b.get("target").and_then(|v| v.as_str()).map(String::from))
        .unwrap_or_else(|| "all".to_string());

    match target_str.as_str() {
        "all" | "steering" | "All" | "Steering" => {
            state.cancel().await;
        }
        _ => {}
    }
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
    #[serde(rename = "question_request")]
    QuestionRequest {
        id: String,
        question: String,
        options: Vec<String>,
    },
    #[serde(rename = "plan_created")]
    PlanCreated { plan: PlanPayload },
}

/// Plan payload for the frontend (matches `PlanPayload` in `src-tauri/src/events.rs`).
#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanPayload {
    pub summary: String,
    pub steps: Vec<PlanStepFrontend>,
    pub estimated_turns: usize,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanStepFrontend {
    pub id: String,
    pub description: String,
    pub files: Vec<String>,
    pub action: String,
    pub depends_on: Vec<String>,
}

/// Convert a `WebEvent` to a frontend-compatible `WebAgentEvent`.
/// Returns `None` for events that have no direct frontend representation.
pub fn convert_web_event(event: &WebEvent) -> Option<WebAgentEvent> {
    match event {
        WebEvent::Agent(backend_event) => convert_agent_event(backend_event),
        WebEvent::ApprovalRequest {
            id,
            tool_name,
            args,
            risk_level,
            reason,
            warnings,
        } => Some(WebAgentEvent::ApprovalRequest {
            id: id.clone(),
            tool_name: tool_name.clone(),
            args: args.clone(),
            risk_level: risk_level.clone(),
            reason: reason.clone(),
            warnings: warnings.clone(),
        }),
        WebEvent::QuestionRequest {
            id,
            question,
            options,
        } => Some(WebAgentEvent::QuestionRequest {
            id: id.clone(),
            question: question.clone(),
            options: options.clone(),
        }),
        WebEvent::PlanCreated {
            summary,
            steps,
            estimated_turns,
        } => Some(WebAgentEvent::PlanCreated {
            plan: PlanPayload {
                summary: summary.clone(),
                steps: steps
                    .iter()
                    .map(|s| PlanStepFrontend {
                        id: s.id.clone(),
                        description: s.description.clone(),
                        files: s.files.clone(),
                        action: s.action.clone(),
                        depends_on: s.depends_on.clone(),
                    })
                    .collect(),
                estimated_turns: *estimated_turns,
            },
        }),
    }
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
        // ToolStats, DiffPreview, SubAgentComplete have no direct frontend representation.
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
