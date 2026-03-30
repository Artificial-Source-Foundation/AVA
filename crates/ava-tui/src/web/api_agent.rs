//! Agent-related HTTP API handlers: submit, cancel, status, retry, edit-resend,
//! regenerate, mid-stream messaging (steer/follow-up/post-complete), queue.

use axum::extract::State;
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tracing::info;

use super::state::{FileEditRecord, PlanStepPayload, TodoItemPayload, WebEvent, WebState};

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
pub(crate) async fn submit_goal(
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

        // Forward raw agent events to the WS broadcast channel, tracking edits and todos
        let checkpoint_stack = stack.clone();
        let todo_state = stack.todo_state.clone();
        let forwarder = tokio::spawn(async move {
            let mut last_tool_was_todo_write = false;
            while let Some(event) = rx.recv().await {
                // Checkpoint: incrementally save session so progress survives crashes.
                // Uses add_messages (INSERT OR REPLACE) instead of full save
                // (DELETE-all + INSERT-all) to avoid data loss on crash.
                if let ava_agent::agent_loop::AgentEvent::Checkpoint(ref session) = event {
                    let _ = checkpoint_stack
                        .session_manager
                        .add_messages(session.id, &session.messages);
                    continue; // Don't forward checkpoint events to WebSocket clients
                }
                // Track write/edit tool calls for undo support, and detect todo_write
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
                    // After a todo_write result, emit the updated todo list
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
                        let _ = event_broadcast.send(WebEvent::TodoUpdate { todos });
                    }
                } else {
                    last_tool_was_todo_write = false;
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
                let is_cancelled = matches!(e, ava_types::AvaError::Cancelled);
                if is_cancelled {
                    tracing::info!("Agent run cancelled by user (session {session_uuid})");
                    // Record the session ID so the frontend can load any checkpointed
                    // messages that were saved before cancellation.
                    *inner.last_session_id.write().await = Some(session_uuid);
                    // Send an error event so the frontend knows the agent was cancelled
                    // and can preserve partial streaming content.
                    let _ = inner.event_tx.send(WebEvent::Agent(
                        ava_agent::agent_loop::AgentEvent::Error(
                            "Agent run cancelled by user".to_string(),
                        ),
                    ));
                } else {
                    tracing::error!("Agent run failed: {e}");
                    // Send an error event so the frontend's completion promise resolves
                    let _ = inner.event_tx.send(WebEvent::Agent(
                        ava_agent::agent_loop::AgentEvent::Error(e.to_string()),
                    ));
                }
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
pub(crate) async fn cancel_agent(State(state): State<WebState>) -> impl IntoResponse {
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
pub(crate) async fn agent_status(State(state): State<WebState>) -> impl IntoResponse {
    let running = *state.inner.running.read().await;
    let (provider, model) = state.inner.stack.current_model().await;
    Json(AgentStatusResponse {
        running,
        provider,
        model,
    })
}

// ============================================================================
// Retry / Edit+Resend / Regenerate
// ============================================================================

/// Helper: run a new agent task with the given goal and history, returning
/// the same format as SubmitGoalResponse. Used by retry/regenerate/edit-resend.
pub(crate) async fn run_agent_from_history(
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
pub(crate) async fn retry_last_message(
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
///
/// The frontend generates its own message IDs (client-side UUIDs stored in
/// IndexedDB) which may not match the backend's session IDs in SQLite.
/// When the target message cannot be found by ID, we fall back to using
/// all existing session history as context and treat `new_content` as a
/// fresh goal. This ensures edit-resend works in web mode where frontend
/// and backend message IDs diverge.
pub(crate) async fn edit_and_resend(
    State(state): State<WebState>,
    Json(req): Json<EditAndResendRequest>,
) -> Result<Json<SubmitGoalResponse>, (StatusCode, Json<ErrorResponse>)> {
    let session_id = state.inner.last_session_id.read().await;

    // Try to load the session and find the target message for precise history
    // truncation. If anything fails (no session, message not found), fall back
    // to using whatever history exists.
    let history: Vec<ava_types::Message> = if let Some(sid) = session_id.as_ref() {
        let session = state.inner.stack.session_manager.get(*sid).ok().flatten();

        if let Some(session) = session {
            // Try to find the exact message to truncate history at that point
            let target_id = uuid::Uuid::parse_str(&req.message_id).ok();
            let pos = target_id.and_then(|tid| session.messages.iter().position(|m| m.id == tid));

            if let Some(pos) = pos {
                // Found the message — use history before it
                session.messages[..pos].to_vec()
            } else {
                // Message ID not found (frontend/backend ID mismatch).
                // Use all history up to (but not including) the last user message
                // so we don't duplicate the message the user just edited.
                collect_history_before_last_user(&session.messages)
            }
        } else {
            vec![]
        }
    } else {
        vec![]
    };
    // Drop the read guard before calling run_agent_from_history which takes
    // its own locks on the state.
    drop(session_id);

    info!(new_content = %req.new_content, message_id = %req.message_id, "Web: edit_and_resend");
    run_agent_from_history(&state, req.new_content, history).await
}

/// Regenerate the last assistant response.
pub(crate) async fn regenerate_response(
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
// Mid-stream Messaging (3-tier)
// ============================================================================

#[derive(Deserialize)]
pub struct SteerRequest {
    pub message: String,
}

/// Inject a steering message (Tier 1) into the running agent.
pub(crate) async fn steer_agent(
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
pub(crate) async fn follow_up_agent(
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
pub(crate) async fn post_complete_agent(
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
pub(crate) async fn get_message_queue(State(state): State<WebState>) -> impl IntoResponse {
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
    let session_uuid = req
        .session_id
        .as_deref()
        .map(uuid::Uuid::parse_str)
        .transpose()
        .map_err(|e| error_response(StatusCode::BAD_REQUEST, &format!("invalid session id: {e}")))?
        .or(*state.inner.last_session_id.read().await);

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
/// queue). For follow-up and post-complete, returns OK — those are drained by
/// the agent loop when it processes them; there is no safe external drain path.
pub(crate) async fn clear_message_queue(
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
