//! Bridge between the Tauri desktop frontend and the Rust agent backend.

use std::collections::{HashMap, VecDeque};
use std::ops::Deref;
use std::path::PathBuf;
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};

use ava_agent::control_plane::interactive::{InteractiveRequestKind, InteractiveRequestStore};
use ava_agent::control_plane::queue::resolve_deferred_queue_session;
use ava_agent::message_queue::MessageQueueControl;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_permissions::inspector::InspectionContext;
use ava_tools::core::plan::PlanRequest;
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::{MessageTier, PlanDecision, QueuedMessage, TodoState};
use tauri::{AppHandle, Emitter};
use tokio::sync::{mpsc, Mutex, RwLock};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

use crate::events::{AgentEvent, PlanPayload, PlanStepPayload};

pub type PendingApprovalReply = InteractiveRequestStore<ToolApproval>;
pub type PendingQuestionReply = InteractiveRequestStore<String>;
pub type PendingPlanReply = InteractiveRequestStore<PlanDecision>;

#[derive(Clone)]
pub struct QueueDispatchSnapshot {
    pub accepting: bool,
    pub tx: Option<mpsc::UnboundedSender<QueuedMessage>>,
}

/// Tracks a file edit so that undo can restore the previous content.
#[derive(Debug, Clone)]
pub struct FileEditRecord {
    pub file_path: String,
    pub previous_content: String,
}

/// Maximum number of file edit records to keep per session.
pub(crate) const MAX_EDIT_HISTORY: usize = 100;

pub struct DesktopRunState {
    pub run_id: String,
    pub session_id: Uuid,
    pub provider: String,
    pub model: String,
    pub cancel: CancellationToken,
    pub queue_dispatch: Mutex<Option<QueueDispatchSnapshot>>,
    pub queue_control: Mutex<Option<MessageQueueControl>>,
    pub interactive_revoked: AtomicBool,
    pub todo_state: TodoState,
    pub permission_context: Arc<RwLock<InspectionContext>>,
}

impl DesktopRunState {
    fn new(
        run_id: String,
        session_id: Uuid,
        provider: String,
        model: String,
        todo_state: TodoState,
        permission_context: Arc<RwLock<InspectionContext>>,
    ) -> Self {
        Self {
            run_id,
            session_id,
            provider,
            model,
            cancel: CancellationToken::new(),
            queue_dispatch: Mutex::new(None),
            queue_control: Mutex::new(None),
            interactive_revoked: AtomicBool::new(false),
            todo_state,
            permission_context,
        }
    }
}

pub struct DesktopBridgeInner {
    pub stack: Arc<AgentStack>,
    pub startup_lock: Mutex<()>,
    pub queue_lifecycle_lock: Mutex<()>,
    pub interactive_lifecycle_lock: Arc<Mutex<()>>,
    pub runs: RwLock<HashMap<String, Arc<DesktopRunState>>>,
    pub session_runs: RwLock<HashMap<Uuid, String>>,
    pub session_permission_contexts: Arc<RwLock<HashMap<Uuid, Arc<RwLock<InspectionContext>>>>>,
    pub pending_approval_reply: PendingApprovalReply,
    pub pending_question_reply: PendingQuestionReply,
    pub pending_plan_reply: PendingPlanReply,
    // Cache pending interactive request payloads keyed by request_id so queued
    // promotions and session rehydration can reconstruct the current UI state.
    pub deferred_interactive_events: Mutex<HashMap<String, AgentEvent>>,
    pub last_session_id: Arc<RwLock<Option<Uuid>>>,
    pub edit_history: Arc<RwLock<HashMap<Uuid, VecDeque<FileEditRecord>>>>,
    pub deferred_queue: Arc<RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>>,
    pub in_flight_deferred: Arc<RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>>,
}

#[derive(Clone)]
pub struct DesktopBridge {
    inner: Arc<DesktopBridgeInner>,
}

impl Deref for DesktopBridge {
    type Target = DesktopBridgeInner;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl DesktopBridge {
    pub async fn init<R: tauri::Runtime>(
        data_dir: PathBuf,
        app: AppHandle<R>,
    ) -> Result<Self, String> {
        let config = AgentStackConfig::for_desktop(data_dir);
        let (stack, question_rx, approval_rx, plan_rx) =
            AgentStack::new(config).await.map_err(|e| e.to_string())?;

        let bridge = Self::new_inner(stack);

        spawn_interactive_forwarders(app, bridge.clone(), approval_rx, question_rx, plan_rx);
        Ok(bridge)
    }

    #[cfg(test)]
    pub async fn init_for_tests(data_dir: PathBuf) -> Result<Self, String> {
        let config = AgentStackConfig::for_desktop(data_dir);
        let (stack, _question_rx, _approval_rx, _plan_rx) =
            AgentStack::new(config).await.map_err(|e| e.to_string())?;
        Ok(Self::new_inner(stack))
    }

    fn new_inner(stack: AgentStack) -> Self {
        Self {
            inner: Arc::new(DesktopBridgeInner {
                stack: Arc::new(stack),
                startup_lock: Mutex::new(()),
                queue_lifecycle_lock: Mutex::new(()),
                interactive_lifecycle_lock: Arc::new(Mutex::new(())),
                runs: RwLock::new(HashMap::new()),
                session_runs: RwLock::new(HashMap::new()),
                session_permission_contexts: Arc::new(RwLock::new(HashMap::new())),
                pending_approval_reply: InteractiveRequestStore::new(
                    InteractiveRequestKind::Approval,
                ),
                pending_question_reply: InteractiveRequestStore::new(
                    InteractiveRequestKind::Question,
                ),
                pending_plan_reply: InteractiveRequestStore::new(InteractiveRequestKind::Plan),
                deferred_interactive_events: Mutex::new(HashMap::new()),
                last_session_id: Arc::new(RwLock::new(None)),
                edit_history: Arc::new(RwLock::new(HashMap::new())),
                deferred_queue: Arc::new(RwLock::new(HashMap::new())),
                in_flight_deferred: Arc::new(RwLock::new(HashMap::new())),
            }),
        }
    }

    pub async fn register_run(
        &self,
        run_id: String,
        session_id: Uuid,
        provider: String,
        model: String,
    ) -> Result<Arc<DesktopRunState>, String> {
        {
            let runs = self.runs.read().await;
            if runs.contains_key(&run_id) {
                return Err(format!("Run {run_id} is already active"));
            }
        }

        {
            let session_runs = self.session_runs.read().await;
            if let Some(existing_run_id) = session_runs.get(&session_id) {
                return Err(format!(
                    "Session {session_id} already has an active run ({existing_run_id})"
                ));
            }
        }

        let permission_context = {
            let existing = self
                .session_permission_contexts
                .read()
                .await
                .get(&session_id)
                .cloned();
            match existing {
                Some(context) => context,
                None => {
                    let context = self.stack.cloned_permission_context().await;
                    self.session_permission_contexts
                        .write()
                        .await
                        .insert(session_id, context.clone());
                    context
                }
            }
        };
        let run = Arc::new(DesktopRunState::new(
            run_id.clone(),
            session_id,
            provider,
            model,
            TodoState::new(),
            permission_context,
        ));
        self.runs.write().await.insert(run_id.clone(), run.clone());
        self.session_runs.write().await.insert(session_id, run_id);
        Ok(run)
    }

    pub async fn finish_run(&self, run_id: &str) {
        let removed = self.runs.write().await.remove(run_id);
        if let Some(run) = removed {
            self.session_runs.write().await.remove(&run.session_id);
        }
    }

    pub async fn active_run_count(&self) -> usize {
        self.runs.read().await.len()
    }

    pub async fn has_active_runs(&self) -> bool {
        self.active_run_count().await > 0
    }

    pub async fn resolve_run(
        &self,
        run_id: Option<&str>,
        session_id: Option<Uuid>,
    ) -> Result<Arc<DesktopRunState>, String> {
        let runs = self.runs.read().await;
        let session_runs = self.session_runs.read().await;

        match (run_id, session_id) {
            (Some(run_id), Some(session_id)) => {
                let run = runs
                    .get(run_id)
                    .cloned()
                    .ok_or_else(|| format!("Run {run_id} is not active"))?;
                if run.session_id != session_id {
                    return Err(format!("Run {run_id} does not own session {session_id}"));
                }
                Ok(run)
            }
            (Some(run_id), None) => runs
                .get(run_id)
                .cloned()
                .ok_or_else(|| format!("Run {run_id} is not active")),
            (None, Some(session_id)) => {
                let run_id = session_runs
                    .get(&session_id)
                    .ok_or_else(|| format!("Session {session_id} does not have an active run"))?;
                runs.get(run_id)
                    .cloned()
                    .ok_or_else(|| format!("Run {run_id} is not active"))
            }
            (None, None) => match runs.len() {
                0 => Err("No active desktop runs".to_string()),
                1 => runs
                    .values()
                    .next()
                    .cloned()
                    .ok_or_else(|| "No active desktop runs".to_string()),
                _ => Err(
                    "Multiple desktop runs are active; provide run_id or session_id".to_string(),
                ),
            },
        }
    }

    pub async fn single_active_run_id(&self) -> Option<String> {
        let runs = self.runs.read().await;
        (runs.len() == 1)
            .then(|| runs.values().next().map(|run| run.run_id.clone()))
            .flatten()
    }

    pub async fn activate_message_queue(
        &self,
        run_id: &str,
        tx: mpsc::UnboundedSender<QueuedMessage>,
        control: MessageQueueControl,
    ) -> Result<(), String> {
        let run = self.resolve_run(Some(run_id), None).await?;
        *run.queue_dispatch.lock().await = Some(QueueDispatchSnapshot {
            accepting: true,
            tx: Some(tx),
        });
        *run.queue_control.lock().await = Some(control);
        Ok(())
    }

    pub async fn clear_message_queue_dispatch(&self, run_id: &str) {
        if let Ok(run) = self.resolve_run(Some(run_id), None).await {
            *run.queue_control.lock().await = None;
            *run.queue_dispatch.lock().await = None;
        }
    }

    pub async fn queue_dispatch_snapshot(
        &self,
        run_id: Option<&str>,
        session_id: Option<Uuid>,
    ) -> Result<Option<QueueDispatchSnapshot>, String> {
        let run = self.resolve_run(run_id, session_id).await?;
        let snapshot = run.queue_dispatch.lock().await.clone();
        Ok(snapshot)
    }

    pub async fn revoke_queue_dispatch(&self, run_id: &str, clear_steering: bool) {
        let _queue_guard = self.queue_lifecycle_lock.lock().await;
        if let Ok(run) = self.resolve_run(Some(run_id), None).await {
            let control = run.queue_control.lock().await.take();
            *run.queue_dispatch.lock().await = None;
            if clear_steering {
                if let Some(control) = control {
                    control.clear_steering();
                }
            }
        }
    }

    pub async fn enqueue_message(
        &self,
        message: QueuedMessage,
        run_id: Option<&str>,
        requested_session_id: Option<Uuid>,
        persist_deferred: bool,
    ) -> Result<(), String> {
        let _queue_guard = self.queue_lifecycle_lock.lock().await;
        let run = self.resolve_run(run_id, requested_session_id).await?;
        let dispatch = run.queue_dispatch.lock().await;
        let snapshot = dispatch
            .clone()
            .ok_or_else(|| "Agent queue is unavailable".to_string())?;
        if !snapshot.accepting {
            return Err("Agent queue is unavailable".to_string());
        }

        let deferred_owner = if matches!(message.tier, MessageTier::Steering) || !persist_deferred {
            None
        } else {
            Some(
                resolve_deferred_queue_session(requested_session_id, Some(run.session_id))
                    .map_err(|error| error.to_string())?,
            )
        };

        let tx = snapshot
            .tx
            .as_ref()
            .ok_or_else(|| "Agent queue is unavailable".to_string())?;
        tx.send(message.clone())
            .map_err(|_| "Agent queue is unavailable".to_string())?;
        drop(dispatch);

        if let Some(session_id) = deferred_owner {
            self.deferred_queue
                .write()
                .await
                .entry(session_id)
                .or_default()
                .push_back(message);
        }

        Ok(())
    }

    pub async fn push_edit(&self, session_id: Uuid, record: FileEditRecord) {
        let mut history = self.edit_history.write().await;
        let session_history = history.entry(session_id).or_default();
        if session_history.len() >= MAX_EDIT_HISTORY {
            session_history.pop_front();
        }
        session_history.push_back(record);
    }

    pub async fn pop_last_edit(&self, session_id: Uuid) -> Option<FileEditRecord> {
        let mut history = self.edit_history.write().await;
        let session_history = history.get_mut(&session_id)?;
        let record = session_history.pop_back();
        if session_history.is_empty() {
            history.remove(&session_id);
        }
        record
    }

    pub async fn discard_deferred_interactive_request_event(&self, request_id: &str) {
        self.deferred_interactive_events
            .lock()
            .await
            .remove(request_id);
    }

    pub async fn take_promoted_interactive_request_event(
        &self,
        kind: InteractiveRequestKind,
        _run_id: Option<&str>,
    ) -> Option<AgentEvent> {
        let request_id = self.current_request_id_for_kind(kind).await?;
        self.deferred_interactive_events
            .lock()
            .await
            .get(&request_id)
            .cloned()
    }

    pub async fn current_interactive_request_event(
        &self,
        kind: InteractiveRequestKind,
        run_id: Option<&str>,
    ) -> Option<AgentEvent> {
        let request_id = self
            .current_actionable_request_id_for_kind_run(kind, run_id)
            .await?;
        self.deferred_interactive_events
            .lock()
            .await
            .get(&request_id)
            .cloned()
    }

    async fn current_request_id_for_kind(&self, kind: InteractiveRequestKind) -> Option<String> {
        match kind {
            InteractiveRequestKind::Approval => {
                self.pending_approval_reply.current_request_id().await
            }
            InteractiveRequestKind::Question => {
                self.pending_question_reply.current_request_id().await
            }
            InteractiveRequestKind::Plan => self.pending_plan_reply.current_request_id().await,
        }
    }

    async fn current_actionable_request_id_for_kind_run(
        &self,
        kind: InteractiveRequestKind,
        run_id: Option<&str>,
    ) -> Option<String> {
        match kind {
            InteractiveRequestKind::Approval => {
                self.pending_approval_reply
                    .current_actionable_request_id_for_run(run_id)
                    .await
            }
            InteractiveRequestKind::Question => {
                self.pending_question_reply
                    .current_actionable_request_id_for_run(run_id)
                    .await
            }
            InteractiveRequestKind::Plan => {
                self.pending_plan_reply
                    .current_actionable_request_id_for_run(run_id)
                    .await
            }
        }
    }

    pub(crate) async fn promoted_interactive_request_event_after_current_change(
        &self,
        kind: InteractiveRequestKind,
        removed_request_id: &str,
        previous_global_request_id: Option<&str>,
    ) -> Option<AgentEvent> {
        if previous_global_request_id != Some(removed_request_id) {
            return None;
        }

        let next_request_id = self.current_request_id_for_kind(kind).await?;
        if next_request_id == removed_request_id {
            return None;
        }

        self.deferred_interactive_events
            .lock()
            .await
            .get(&next_request_id)
            .cloned()
    }
}

fn plan_step_payloads(plan: &ava_types::Plan) -> Vec<PlanStepPayload> {
    plan.steps
        .iter()
        .map(|step| PlanStepPayload {
            id: step.id.clone(),
            description: step.description.clone(),
            files: step.files.clone(),
            action: match step.action {
                ava_types::PlanAction::Research => "research",
                ava_types::PlanAction::Implement => "implement",
                ava_types::PlanAction::Test => "test",
                ava_types::PlanAction::Review => "review",
            }
            .to_string(),
            depends_on: step.depends_on.clone(),
        })
        .collect()
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

async fn emit_or_defer_interactive_request_event<R: tauri::Runtime>(
    app: &AppHandle<R>,
    bridge: &DesktopBridge,
    request_id: &str,
    kind: InteractiveRequestKind,
    run_id: Option<&str>,
    event: AgentEvent,
) {
    bridge
        .deferred_interactive_events
        .lock()
        .await
        .insert(request_id.to_string(), event.clone());

    let current = bridge
        .current_actionable_request_id_for_kind_run(kind, run_id)
        .await;
    if current.as_deref() == Some(request_id) {
        if let Err(error) = app.emit("agent-event", event) {
            tracing::error!("Failed to emit interactive request event to frontend: {error}");
        }
    }
}

async fn emit_promoted_interactive_request_event<R: tauri::Runtime>(
    app: &AppHandle<R>,
    bridge: &DesktopBridge,
    kind: InteractiveRequestKind,
    _run_id: Option<&str>,
) {
    let Some(request_id) = bridge.current_request_id_for_kind(kind).await else {
        return;
    };
    if let Some(event) = bridge
        .deferred_interactive_events
        .lock()
        .await
        .get(&request_id)
        .cloned()
    {
        if let Err(error) = app.emit("agent-event", event) {
            tracing::error!("Failed to emit promoted interactive request event: {error}");
        }
    }
}

fn spawn_interactive_forwarders<R: tauri::Runtime>(
    app: AppHandle<R>,
    bridge: DesktopBridge,
    mut approval_rx: mpsc::UnboundedReceiver<ApprovalRequest>,
    mut question_rx: mpsc::UnboundedReceiver<QuestionRequest>,
    mut plan_rx: mpsc::UnboundedReceiver<PlanRequest>,
) {
    let approval_bridge = bridge.clone();
    let approval_app = app.clone();
    tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            let _interactive_guard = approval_bridge.interactive_lifecycle_lock.lock().await;
            let run_id = req.run_id.clone();
            let Some(run) = (match run_id.as_deref() {
                Some(run_id) => approval_bridge.runs.read().await.get(run_id).cloned(),
                None => None,
            }) else {
                let _ = req.reply.send(ToolApproval::Rejected(Some(
                    "Agent run is no longer active in desktop UI".to_string(),
                )));
                continue;
            };
            if run.interactive_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(ToolApproval::Rejected(Some(
                    "Agent run cancelled from desktop UI".to_string(),
                )));
                continue;
            }

            let risk_level = format!("{:?}", req.inspection.risk_level).to_lowercase();
            let handle = approval_bridge
                .pending_approval_reply
                .register_with_run_id(req.reply, run_id.clone())
                .await;
            let request_id = handle.request_id.clone();
            emit_or_defer_interactive_request_event(
                &approval_app,
                &approval_bridge,
                &request_id,
                InteractiveRequestKind::Approval,
                handle.run_id.as_deref(),
                AgentEvent::ApprovalRequest {
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

            let pending = approval_bridge.pending_approval_reply.clone();
            let timeout_bridge = approval_bridge.clone();
            let timeout_app = approval_app.clone();
            tokio::spawn(async move {
                let timeout = pending.timeout();
                if let Some(timed_out) = pending.await_timeout_request(&request_id).await {
                    tracing::warn!(
                        request_id = %timed_out.handle.request_id,
                        timeout_secs = timeout.as_secs(),
                        "Desktop approval request timed out"
                    );
                    timeout_bridge
                        .discard_deferred_interactive_request_event(&timed_out.handle.request_id)
                        .await;
                    let _ = timed_out.reply.send(ToolApproval::Rejected(Some(
                        "Timed out waiting for user approval in desktop UI".to_string(),
                    )));
                    let payload = interactive_request_cleared_event(
                        &timed_out.handle.request_id,
                        timed_out.handle.kind.as_str(),
                        true,
                        timed_out.handle.run_id.as_deref(),
                    );
                    if let Err(error) = timeout_app.emit("agent-event", payload) {
                        tracing::error!("Failed to emit approval timeout event: {error}");
                    }
                    emit_promoted_interactive_request_event(
                        &timeout_app,
                        &timeout_bridge,
                        timed_out.handle.kind,
                        timed_out.handle.run_id.as_deref(),
                    )
                    .await;
                }
            });
        }
    });

    let question_bridge = bridge.clone();
    let question_app = app.clone();
    tokio::spawn(async move {
        while let Some(req) = question_rx.recv().await {
            let _interactive_guard = question_bridge.interactive_lifecycle_lock.lock().await;
            let run_id = req.run_id.clone();
            let Some(run) = (match run_id.as_deref() {
                Some(run_id) => question_bridge.runs.read().await.get(run_id).cloned(),
                None => None,
            }) else {
                let _ = req.reply.send(String::new());
                continue;
            };
            if run.interactive_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(String::new());
                continue;
            }

            let handle = question_bridge
                .pending_question_reply
                .register_with_run_id(req.reply, run_id.clone())
                .await;
            let request_id = handle.request_id.clone();
            emit_or_defer_interactive_request_event(
                &question_app,
                &question_bridge,
                &request_id,
                InteractiveRequestKind::Question,
                handle.run_id.as_deref(),
                AgentEvent::QuestionRequest {
                    id: request_id.clone(),
                    question: req.question.clone(),
                    options: req.options.clone(),
                    run_id: handle.run_id.clone(),
                },
            )
            .await;

            let pending = question_bridge.pending_question_reply.clone();
            let timeout_bridge = question_bridge.clone();
            let timeout_app = question_app.clone();
            tokio::spawn(async move {
                let timeout = pending.timeout();
                if let Some(timed_out) = pending.await_timeout_request(&request_id).await {
                    tracing::warn!(
                        request_id = %timed_out.handle.request_id,
                        timeout_secs = timeout.as_secs(),
                        "Desktop question request timed out"
                    );
                    timeout_bridge
                        .discard_deferred_interactive_request_event(&timed_out.handle.request_id)
                        .await;
                    let _ = timed_out.reply.send(String::new());
                    let payload = interactive_request_cleared_event(
                        &timed_out.handle.request_id,
                        timed_out.handle.kind.as_str(),
                        true,
                        timed_out.handle.run_id.as_deref(),
                    );
                    if let Err(error) = timeout_app.emit("agent-event", payload) {
                        tracing::error!("Failed to emit question timeout event: {error}");
                    }
                    emit_promoted_interactive_request_event(
                        &timeout_app,
                        &timeout_bridge,
                        timed_out.handle.kind,
                        timed_out.handle.run_id.as_deref(),
                    )
                    .await;
                }
            });
        }
    });

    tokio::spawn(async move {
        while let Some(req) = plan_rx.recv().await {
            let _interactive_guard = bridge.interactive_lifecycle_lock.lock().await;
            let run_id = req.run_id.clone();
            let Some(run) = (match run_id.as_deref() {
                Some(run_id) => bridge.runs.read().await.get(run_id).cloned(),
                None => None,
            }) else {
                let _ = req.reply.send(PlanDecision::Rejected {
                    feedback: "Agent run is no longer active in desktop UI".to_string(),
                });
                continue;
            };
            if run.interactive_revoked.load(Ordering::SeqCst) {
                let _ = req.reply.send(PlanDecision::Rejected {
                    feedback: "Agent run cancelled from desktop UI".to_string(),
                });
                continue;
            }

            let handle = bridge
                .pending_plan_reply
                .register_with_run_id(req.reply, run_id.clone())
                .await;
            let request_id = handle.request_id.clone();
            emit_or_defer_interactive_request_event(
                &app,
                &bridge,
                &request_id,
                InteractiveRequestKind::Plan,
                handle.run_id.as_deref(),
                AgentEvent::PlanCreated {
                    id: request_id.clone(),
                    plan: PlanPayload {
                        summary: req.plan.summary.clone(),
                        steps: plan_step_payloads(&req.plan),
                        estimated_turns: req.plan.estimated_turns.unwrap_or(0) as usize,
                    },
                    run_id: handle.run_id.clone(),
                },
            )
            .await;

            let pending = bridge.pending_plan_reply.clone();
            let timeout_bridge = bridge.clone();
            let timeout_app = app.clone();
            tokio::spawn(async move {
                let timeout = pending.timeout();
                if let Some(timed_out) = pending.await_timeout_request(&request_id).await {
                    tracing::warn!(
                        request_id = %timed_out.handle.request_id,
                        timeout_secs = timeout.as_secs(),
                        "Desktop plan request timed out"
                    );
                    timeout_bridge
                        .discard_deferred_interactive_request_event(&timed_out.handle.request_id)
                        .await;
                    let _ = timed_out.reply.send(PlanDecision::Rejected {
                        feedback: "Timed out waiting for plan response in desktop UI".to_string(),
                    });
                    let payload = interactive_request_cleared_event(
                        &timed_out.handle.request_id,
                        timed_out.handle.kind.as_str(),
                        true,
                        timed_out.handle.run_id.as_deref(),
                    );
                    if let Err(error) = timeout_app.emit("agent-event", payload) {
                        tracing::error!("Failed to emit plan timeout event: {error}");
                    }
                    emit_promoted_interactive_request_event(
                        &timeout_app,
                        &timeout_bridge,
                        timed_out.handle.kind,
                        timed_out.handle.run_id.as_deref(),
                    )
                    .await;
                }
            });
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{TodoItem, TodoPriority, TodoStatus};

    #[tokio::test]
    async fn register_run_creates_isolated_run_state() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");

        let run_a = bridge
            .register_run(
                "desktop-run-a".to_string(),
                Uuid::new_v4(),
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("run a");
        let run_b = bridge
            .register_run(
                "desktop-run-b".to_string(),
                Uuid::new_v4(),
                "anthropic".to_string(),
                "claude-sonnet-4.6".to_string(),
            )
            .await
            .expect("run b");

        run_a.todo_state.set(vec![TodoItem {
            content: "Ship desktop fix".to_string(),
            status: TodoStatus::InProgress,
            priority: TodoPriority::High,
        }]);
        run_a
            .permission_context
            .write()
            .await
            .session_approved
            .insert("bash".to_string());

        assert_eq!(run_a.provider, "openai");
        assert_eq!(run_a.model, "gpt-5.4");
        assert_eq!(run_b.provider, "anthropic");
        assert_eq!(run_b.model, "claude-sonnet-4.6");
        assert_eq!(run_a.todo_state.get().len(), 1);
        assert!(run_b.todo_state.get().is_empty());
        assert!(run_a
            .permission_context
            .read()
            .await
            .session_approved
            .contains("bash"));
        assert!(!run_b
            .permission_context
            .read()
            .await
            .session_approved
            .contains("bash"));
    }

    #[tokio::test]
    async fn edit_history_is_scoped_per_session() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");
        let session_a = Uuid::new_v4();
        let session_b = Uuid::new_v4();

        bridge
            .push_edit(
                session_a,
                FileEditRecord {
                    file_path: "a.txt".to_string(),
                    previous_content: "before-a".to_string(),
                },
            )
            .await;
        bridge
            .push_edit(
                session_b,
                FileEditRecord {
                    file_path: "b.txt".to_string(),
                    previous_content: "before-b".to_string(),
                },
            )
            .await;

        let popped_a = bridge
            .pop_last_edit(session_a)
            .await
            .expect("session a edit");
        let popped_b = bridge
            .pop_last_edit(session_b)
            .await
            .expect("session b edit");

        assert_eq!(popped_a.file_path, "a.txt");
        assert_eq!(popped_b.file_path, "b.txt");
        assert!(bridge.pop_last_edit(session_a).await.is_none());
        assert!(bridge.pop_last_edit(session_b).await.is_none());
    }

    #[tokio::test]
    async fn permission_context_is_reused_for_replays_in_same_session() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");
        let session_id = Uuid::new_v4();

        let first_run = bridge
            .register_run(
                "desktop-run-a".to_string(),
                session_id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("first run");
        first_run
            .permission_context
            .write()
            .await
            .session_approved
            .insert("bash".to_string());
        bridge.finish_run("desktop-run-a").await;

        let replay_run = bridge
            .register_run(
                "desktop-run-b".to_string(),
                session_id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("replay run");

        assert!(replay_run
            .permission_context
            .read()
            .await
            .session_approved
            .contains("bash"));
    }

    #[tokio::test]
    async fn promoted_interactive_event_can_advance_to_different_run() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");

        let (first_tx, _first_rx) = tokio::sync::oneshot::channel::<String>();
        let first = bridge
            .pending_question_reply
            .register_with_run_id(first_tx, Some("desktop-run-a".to_string()))
            .await;
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

        let resolved = bridge
            .pending_question_reply
            .resolve(Some(&first.request_id))
            .await
            .expect("first request should resolve");
        assert_eq!(resolved.handle.run_id.as_deref(), Some("desktop-run-a"));

        let promoted = bridge
            .take_promoted_interactive_request_event(
                InteractiveRequestKind::Question,
                Some("desktop-run-a"),
            )
            .await
            .expect("next queued question should promote globally");

        match promoted {
            AgentEvent::QuestionRequest { id, run_id, .. } => {
                assert_eq!(id, second.request_id);
                assert_eq!(run_id.as_deref(), Some("desktop-run-b"));
            }
            other => panic!("expected promoted question request, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn scoped_current_interactive_request_only_exposes_globally_actionable_prompt() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");

        let (first_tx, _first_rx) = tokio::sync::oneshot::channel::<String>();
        let first = bridge
            .pending_question_reply
            .register_with_run_id(first_tx, Some("desktop-run-a".to_string()))
            .await;
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

        assert!(matches!(
            bridge
                .current_interactive_request_event(InteractiveRequestKind::Question, Some("desktop-run-a"))
                .await,
            Some(AgentEvent::QuestionRequest { run_id, .. }) if run_id.as_deref() == Some("desktop-run-a")
        ));
        assert!(bridge
            .current_interactive_request_event(
                InteractiveRequestKind::Question,
                Some("desktop-run-b")
            )
            .await
            .is_none());

        let _ = bridge
            .pending_question_reply
            .resolve(Some(&first.request_id))
            .await
            .expect("first request should resolve");

        assert!(matches!(
            bridge
                .current_interactive_request_event(InteractiveRequestKind::Question, Some("desktop-run-b"))
                .await,
            Some(AgentEvent::QuestionRequest { run_id, .. }) if run_id.as_deref() == Some("desktop-run-b")
        ));
    }

    #[tokio::test]
    async fn initial_same_kind_prompt_visibility_is_gated_by_global_actionability() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");

        let (first_tx, _first_rx) = tokio::sync::oneshot::channel::<String>();
        let first = bridge
            .pending_question_reply
            .register_with_run_id(first_tx, Some("desktop-run-a".to_string()))
            .await;
        let (second_tx, _second_rx) = tokio::sync::oneshot::channel::<String>();
        let second = bridge
            .pending_question_reply
            .register_with_run_id(second_tx, Some("desktop-run-b".to_string()))
            .await;

        assert_eq!(
            bridge
                .current_actionable_request_id_for_kind_run(
                    InteractiveRequestKind::Question,
                    Some("desktop-run-a")
                )
                .await,
            Some(first.request_id)
        );
        assert_eq!(
            bridge
                .current_actionable_request_id_for_kind_run(
                    InteractiveRequestKind::Question,
                    Some("desktop-run-b")
                )
                .await,
            None
        );

        let _ = second;
    }

    #[tokio::test]
    async fn promotion_after_current_change_ignores_background_run_cancellation() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");

        let (first_tx, _first_rx) = tokio::sync::oneshot::channel::<String>();
        let first = bridge
            .pending_question_reply
            .register_with_run_id(first_tx, Some("desktop-run-a".to_string()))
            .await;
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

        let promoted = bridge
            .promoted_interactive_request_event_after_current_change(
                InteractiveRequestKind::Question,
                &second.request_id,
                Some(&first.request_id),
            )
            .await;

        assert!(promoted.is_none());
    }

    #[tokio::test]
    async fn discard_deferred_interactive_request_event_removes_cached_payload() {
        let dir = tempfile::tempdir().expect("tempdir");
        let bridge = DesktopBridge::init_for_tests(dir.path().to_path_buf())
            .await
            .expect("bridge");

        bridge.deferred_interactive_events.lock().await.insert(
            "question-1".to_string(),
            AgentEvent::QuestionRequest {
                id: "question-1".to_string(),
                question: "Need input?".to_string(),
                options: vec![],
                run_id: Some("desktop-run-a".to_string()),
            },
        );

        bridge
            .discard_deferred_interactive_request_event("question-1")
            .await;

        assert!(bridge
            .deferred_interactive_events
            .lock()
            .await
            .get("question-1")
            .is_none());
    }
}
