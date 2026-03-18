//! Tauri commands for running the agent loop, mid-stream messaging,
//! approval/question resolution, and retry/regenerate/undo actions.
//!
//! The approval and question flows work as follows:
//! 1. The agent's permission middleware sends an `ApprovalRequest` through the bridge
//! 2. A spawned forwarder task picks it up and emits an `approval_request` event to the frontend
//! 3. The frontend shows the ApprovalDock and calls `resolve_approval` when the user decides
//! 4. The resolve command sends the response through the stored oneshot channel
//! 5. The permission middleware receives it and continues or blocks the tool

use ava_types::MessageTier;
use ava_tools::permission_middleware::ToolApproval;
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager, State};
use tokio::sync::mpsc;
use tracing::info;
use uuid::Uuid;

use crate::bridge::DesktopBridge;
use crate::events::{emit_backend_event, AgentEvent};

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
    app: &AppHandle,
    bridge: &DesktopBridge,
) -> Result<SubmitGoalResult, String> {
    // Prevent concurrent runs
    {
        let running = bridge.running.read().await;
        if *running {
            return Err("Agent is already running. Cancel first.".to_string());
        }
    }
    *bridge.running.write().await = true;

    let cancel = bridge.new_cancel_token().await;

    // Create a message queue for mid-stream messaging
    let message_queue = bridge.new_message_queue().await;

    // Create an event channel; spawn a forwarder that emits to all Tauri windows
    let (tx, mut rx) = mpsc::unbounded_channel();
    let app_clone = app.clone();

    // Clone the Arc-wrapped edit history so the forwarder can record file edits
    let edit_history = bridge.edit_history.clone();
    let forwarder = tokio::spawn(async move {
        while let Some(event) = rx.recv().await {
            // Track write/edit tool calls for undo support
            if let ava_agent::agent_loop::AgentEvent::ToolCall(ref tc) = event {
                if tc.name == "edit" || tc.name == "write" {
                    if let Some(path) = tc
                        .arguments
                        .get("file_path")
                        .and_then(|v| v.as_str())
                    {
                        if let Ok(content) = tokio::fs::read_to_string(path).await {
                            let mut hist = edit_history.write().await;
                            if hist.len() >= 100 {
                                hist.pop_front();
                            }
                            hist.push_back(crate::bridge::FileEditRecord {
                                file_path: path.to_string(),
                                previous_content: content,
                                timestamp: chrono::Utc::now(),
                            });
                        }
                    }
                }
            }
            emit_backend_event(&app_clone, &event);
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

    let pending_approval = bridge.pending_approval_reply.clone();
    let pending_question = bridge.pending_question_reply.clone();

    // Spawn approval forwarder
    let app_approval = app.clone();
    let approval_forwarder = tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            let risk_level = format!("{:?}", req.inspection.risk_level).to_lowercase();
            let id = format!("approval-{}", uuid::Uuid::new_v4());

            *pending_approval.lock().await = Some(req.reply);

            let _ = app_approval.emit(
                "agent-event",
                AgentEvent::ApprovalRequest {
                    id,
                    tool_name: req.call.name.clone(),
                    args: req.call.arguments.clone(),
                    risk_level,
                    reason: req.inspection.reason.clone(),
                    warnings: req.inspection.warnings.clone(),
                },
            );
        }
    });

    // Spawn question forwarder
    let app_question = app.clone();
    let question_forwarder = tokio::spawn(async move {
        while let Some(req) = question_rx.recv().await {
            let id = format!("question-{}", uuid::Uuid::new_v4());

            *pending_question.lock().await = Some(req.reply);

            let _ = app_question.emit(
                "agent-event",
                AgentEvent::QuestionRequest {
                    id,
                    question: req.question.clone(),
                    options: req.options.clone(),
                },
            );
        }
    });

    info!(goal = %goal, "run_agent_inner: starting agent");

    // Debug: write to file so we can trace desktop issues
    let _ = std::fs::OpenOptions::new()
        .create(true).append(true)
        .open("/tmp/ava-debug/rust-agent.log")
        .and_then(|mut f| {
            use std::io::Write;
            writeln!(f, "[{}] run_agent_inner: goal={}, max_turns={}", chrono::Utc::now(), goal, max_turns)
        });

    let result = bridge
        .stack
        .run(
            goal,
            max_turns,
            Some(tx),
            cancel,
            history,
            Some(message_queue),
            vec![], // no images
        )
        .await;

    // Wait for the forwarder to drain; abort the approval/question forwarders
    let _ = forwarder.await;
    approval_forwarder.abort();
    question_forwarder.abort();

    // Clean up
    bridge.clear_message_tx().await;
    *bridge.running.write().await = false;

    // Debug log result
    let _ = std::fs::OpenOptions::new()
        .create(true).append(true)
        .open("/tmp/ava-debug/rust-agent.log")
        .and_then(|mut f| {
            use std::io::Write;
            match &result {
                Ok(r) => writeln!(f, "[{}] run_agent_inner: success={}, turns={}, session={}", chrono::Utc::now(), r.success, r.turns, r.session.id),
                Err(e) => writeln!(f, "[{}] run_agent_inner: ERROR={}", chrono::Utc::now(), e),
            }
        });

    match result {
        Ok(run_result) => {
            let _ = bridge.stack.session_manager.save(&run_result.session);
            *bridge.last_session_id.write().await = Some(run_result.session.id);
            Ok(SubmitGoalResult {
                success: run_result.success,
                turns: run_result.turns,
                session_id: run_result.session.id.to_string(),
            })
        }
        Err(e) => Err(e.to_string()),
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
        bridge.stack.set_thinking_level(level).await;
    }

    // Load conversation history from the previous session (if any) so the
    // agent has context from prior turns in this desktop session.
    let history = if let Some(session_id) = *bridge.last_session_id.read().await {
        bridge
            .stack
            .session_manager
            .get(session_id)
            .ok()
            .flatten()
            .map(|s| s.messages)
            .unwrap_or_default()
    } else {
        vec![]
    };

    run_agent_inner(&args.goal, args.max_turns, history, &app, &bridge).await
}

/// Cancel the currently-running agent.
#[tauri::command]
pub async fn cancel_agent(bridge: State<'_, DesktopBridge>) -> Result<(), String> {
    bridge.cancel().await;
    let _ = bridge.pending_approval_reply.lock().await.take();
    let _ = bridge.pending_question_reply.lock().await.take();
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
}

/// Resolve a pending tool approval request.
#[tauri::command]
pub async fn resolve_approval(
    args: ResolveApprovalArgs,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    let reply = bridge
        .pending_approval_reply
        .lock()
        .await
        .take()
        .ok_or_else(|| "No pending approval request to resolve".to_string())?;

    let approval = if args.approved {
        if args.always_allow {
            ToolApproval::AllowAlways
        } else {
            ToolApproval::AllowedForSession
        }
    } else {
        ToolApproval::Rejected(Some("User denied via desktop UI".to_string()))
    };

    reply.send(approval).map_err(|_| {
        "Failed to send approval response — the agent may have already moved on".to_string()
    })?;

    Ok(())
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ResolveQuestionArgs {
    pub answer: String,
}

/// Resolve a pending question request.
#[tauri::command]
pub async fn resolve_question(
    args: ResolveQuestionArgs,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    let reply = bridge
        .pending_question_reply
        .lock()
        .await
        .take()
        .ok_or_else(|| "No pending question request to resolve".to_string())?;

    reply.send(args.answer).map_err(|_| {
        "Failed to send question response — the agent may have already moved on".to_string()
    })?;

    Ok(())
}

// ============================================================================
// Mid-stream messaging commands
// ============================================================================

/// Inject a steering message (Tier 1).
#[tauri::command]
pub async fn steer_agent(
    message: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if message.is_empty() {
        return Err("Steering message must not be empty.".to_string());
    }
    bridge
        .send_message(message, MessageTier::Steering)
        .await
}

/// Queue a follow-up message (Tier 2).
#[tauri::command]
pub async fn follow_up_agent(
    message: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if message.is_empty() {
        return Err("Follow-up message must not be empty.".to_string());
    }
    bridge
        .send_message(message, MessageTier::FollowUp)
        .await
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PostCompleteArgs {
    pub message: String,
    #[serde(default = "default_group")]
    pub group: u32,
}

fn default_group() -> u32 {
    1
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
    bridge
        .send_message(
            args.message,
            MessageTier::PostComplete { group: args.group },
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
    let has_tx = bridge.message_tx.read().await.is_some();
    Ok(MessageQueueState {
        active: running && has_tx,
    })
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ClearTarget {
    All,
    Steering,
    FollowUp,
    PostComplete,
}

/// Clear messages from the queue.
#[tauri::command]
pub async fn clear_message_queue(
    target: ClearTarget,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    match target {
        ClearTarget::All | ClearTarget::Steering => {
            bridge.cancel().await;
            Ok(())
        }
        ClearTarget::FollowUp | ClearTarget::PostComplete => Ok(()),
    }
}

// ============================================================================
// Retry / Edit+Resend / Regenerate / Undo
// ============================================================================

/// Retry the last user message.
#[tauri::command]
pub async fn retry_last_message(
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    let session_id = bridge
        .last_session_id
        .read()
        .await
        .ok_or_else(|| "No previous session to retry".to_string())?;

    let session = bridge
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| e.to_string())?
        .ok_or_else(|| format!("Session {session_id} not found"))?;

    let last_user_msg = session
        .messages
        .iter()
        .rev()
        .find(|m| m.role == ava_types::Role::User)
        .ok_or_else(|| "No user message found in session to retry".to_string())?;

    let goal = last_user_msg.content.clone();
    let history = collect_history_before_last_user(&session.messages);

    info!(goal = %goal, session_id = %session_id, "retry_last_message");
    run_agent_inner(&goal, 0, history, &app, &bridge).await
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EditAndResendArgs {
    pub message_id: String,
    pub new_content: String,
}

/// Edit a specific message and re-run the agent from that point.
#[tauri::command]
pub async fn edit_and_resend(
    args: EditAndResendArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    let session_id = bridge
        .last_session_id
        .read()
        .await
        .ok_or_else(|| "No previous session to edit".to_string())?;

    let session = bridge
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| e.to_string())?
        .ok_or_else(|| format!("Session {session_id} not found"))?;

    let target_id =
        Uuid::parse_str(&args.message_id).map_err(|e| format!("Invalid message ID: {e}"))?;

    let pos = session
        .messages
        .iter()
        .position(|m| m.id == target_id)
        .ok_or_else(|| format!("Message {target_id} not found in session"))?;

    let history: Vec<ava_types::Message> = session.messages[..pos].to_vec();

    info!(new_content = %args.new_content, message_id = %args.message_id, "edit_and_resend");
    run_agent_inner(&args.new_content, 0, history, &app, &bridge).await
}

/// Regenerate the last assistant response.
#[tauri::command]
pub async fn regenerate_response(
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<SubmitGoalResult, String> {
    let session_id = bridge
        .last_session_id
        .read()
        .await
        .ok_or_else(|| "No previous session to regenerate".to_string())?;

    let session = bridge
        .stack
        .session_manager
        .get(session_id)
        .map_err(|e| e.to_string())?
        .ok_or_else(|| format!("Session {session_id} not found"))?;

    let last_user_pos = session
        .messages
        .iter()
        .rposition(|m| m.role == ava_types::Role::User)
        .ok_or_else(|| "No user message found in session to regenerate from".to_string())?;

    let goal = session.messages[last_user_pos].content.clone();
    let history: Vec<ava_types::Message> = session.messages[..last_user_pos].to_vec();

    info!(goal = %goal, session_id = %session_id, "regenerate_response");
    run_agent_inner(&goal, 0, history, &app, &bridge).await
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
    bridge: State<'_, DesktopBridge>,
) -> Result<UndoResult, String> {
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

// ============================================================================
// Praxis multi-agent commands
// ============================================================================

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StartPraxisArgs {
    pub goal: String,
    /// Domain hint for task routing (auto-detected if None). Reserved for future use.
    #[serde(default)]
    #[allow(dead_code)]
    pub domain: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PraxisStatus {
    pub running: bool,
    pub total_workers: usize,
    pub succeeded: usize,
    pub failed: usize,
}

/// Start a Praxis multi-agent task. Spawns a Director that delegates to
/// domain-specific leads and streams PraxisEvents to the frontend.
#[tauri::command]
pub async fn start_praxis(
    args: StartPraxisArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    // Prevent concurrent runs (shares the running flag with single-agent)
    {
        let running = bridge.running.read().await;
        if *running {
            return Err("Agent is already running. Cancel first.".to_string());
        }
    }
    *bridge.running.write().await = true;

    let cancel = bridge.new_cancel_token().await;
    let stack = bridge.stack.clone();

    // Resolve the current provider
    let (provider_name, model_name) = stack.current_model().await;
    let provider = stack
        .router
        .route_required(&provider_name, &model_name)
        .await
        .map_err(|e| e.to_string())?;

    let platform = std::sync::Arc::new(ava_platform::StandardPlatform);

    // Clone the app handle so we can access bridge state from the spawned task
    let app_handle = app.clone();

    tokio::spawn(async move {
        let mut director = ava_praxis::Director::new(ava_praxis::DirectorConfig {
            budget: ava_praxis::Budget::interactive(200, 10.0),
            default_provider: provider,
            domain_providers: std::collections::HashMap::new(),
            platform: Some(platform),
        });

        let worker = match director.delegate(ava_praxis::Task {
            description: args.goal.clone(),
            task_type: ava_praxis::TaskType::Simple,
            files: vec![],
        }) {
            Ok(worker) => worker,
            Err(err) => {
                let _ = app_handle.emit(
                    "agent-event",
                    crate::events::AgentEvent::Error {
                        message: format!("Praxis delegation failed: {err}"),
                    },
                );
                let bridge_ref = app_handle.state::<DesktopBridge>();
                *bridge_ref.running.write().await = false;
                return;
            }
        };

        let (tx, mut rx) = mpsc::unbounded_channel();

        // Spawn a forwarder that converts PraxisEvents to Tauri events
        let app_fwd = app_handle.clone();
        let forwarder = tokio::spawn(async move {
            while let Some(event) = rx.recv().await {
                crate::events::emit_praxis_event(&app_fwd, &event);
            }
        });

        let result = director.coordinate(vec![worker], cancel, tx).await;
        let _ = forwarder.await;

        match &result {
            Ok(_session) => {
                info!(goal = %args.goal, "Praxis task completed successfully");
            }
            Err(e) => {
                let _ = app_handle.emit(
                    "agent-event",
                    crate::events::AgentEvent::Error {
                        message: format!("Praxis coordination failed: {e}"),
                    },
                );
            }
        }

        let bridge_ref = app_handle.state::<DesktopBridge>();
        *bridge_ref.running.write().await = false;
    });

    Ok(())
}

/// Get the current Praxis status (running state).
#[tauri::command]
pub async fn get_praxis_status(
    bridge: State<'_, DesktopBridge>,
) -> Result<PraxisStatus, String> {
    let running = *bridge.running.read().await;
    Ok(PraxisStatus {
        running,
        total_workers: 0,
        succeeded: 0,
        failed: 0,
    })
}

/// Cancel a running Praxis task (uses the same cancel token as single-agent).
#[tauri::command]
pub async fn cancel_praxis(
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    bridge.cancel().await;
    Ok(())
}

/// Send a steering message to a specific Praxis lead (currently forwards to
/// the shared message queue — individual lead steering requires tracking
/// per-lead channels which will be added when the Director supports it).
#[tauri::command]
pub async fn steer_lead(
    lead_id: String,
    message: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if message.is_empty() {
        return Err("Steering message must not be empty.".to_string());
    }
    info!(lead_id = %lead_id, message = %message, "steer_lead: forwarding as steering message");
    bridge
        .send_message(message, MessageTier::Steering)
        .await
}

fn collect_history_before_last_user(messages: &[ava_types::Message]) -> Vec<ava_types::Message> {
    let last_user_pos = messages
        .iter()
        .rposition(|m| m.role == ava_types::Role::User);
    match last_user_pos {
        Some(pos) => messages[..pos].to_vec(),
        None => vec![],
    }
}
