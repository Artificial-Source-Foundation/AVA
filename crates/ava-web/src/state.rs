//! Shared web server state — owns an `AgentStack` and coordination primitives.
//!
//! Analogous to `DesktopBridge` in the Tauri backend, but without Tauri-specific
//! types. Agent events are broadcast to all connected WebSocket clients via a
//! `tokio::sync::broadcast` channel.

use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;
use std::sync::{atomic::AtomicBool, Arc};

use ava_agent::message_queue::MessageQueueControl;
use ava_agent_orchestration::stack::{AgentStack, AgentStackConfig};
use ava_control_plane::interactive::{InteractiveRequestKind, InteractiveRequestStore};
use ava_control_plane::queue::resolve_deferred_queue_session;
use ava_tools::permission_middleware::ToolApproval;
use ava_types::{PlanDecision, QueuedMessage};
use color_eyre::Result;
use serde_json::Value;
use tokio::sync::{broadcast, mpsc, Mutex, RwLock};
use tokio_util::sync::CancellationToken;

#[derive(Clone)]
pub struct QueueDispatchSnapshot {
    pub accepting: bool,
    pub run_id: String,
    pub session_id: uuid::Uuid,
    pub tx: Option<mpsc::UnboundedSender<QueuedMessage>>,
}

pub struct WebRunState {
    pub run_id: String,
    pub session_id: uuid::Uuid,
    pub provider: String,
    pub model: String,
    pub cancel: CancellationToken,
    pub queue_dispatch: Mutex<Option<QueueDispatchSnapshot>>,
    pub queue_control: Mutex<Option<MessageQueueControl>>,
    pub interactive_revoked: AtomicBool,
}

impl WebRunState {
    fn new(run_id: String, session_id: uuid::Uuid, provider: String, model: String) -> Self {
        Self {
            run_id,
            session_id,
            provider,
            model,
            cancel: CancellationToken::new(),
            queue_dispatch: Mutex::new(None),
            queue_control: Mutex::new(None),
            interactive_revoked: AtomicBool::new(false),
        }
    }
}

pub type PendingApprovalReply = InteractiveRequestStore<ToolApproval>;
pub type PendingQuestionReply = InteractiveRequestStore<String>;
pub type PendingPlanReply = InteractiveRequestStore<PlanDecision>;

/// A recorded file edit for undo support.
#[derive(Debug, Clone)]
pub struct FileEditRecord {
    pub file_path: String,
    pub previous_content: String,
}

/// A broadcast-able web event — wraps both regular agent events and the
/// interactive approval/question/plan events that need to be forwarded over WS.
#[derive(Clone, Debug)]
pub enum WebEvent {
    /// A regular agent event from the backend loop.
    Agent {
        event: ava_agent::agent_loop::AgentEvent,
        run_id: Option<String>,
    },
    /// A plugin-owned event emitted through the plugin host seam.
    Plugin {
        plugin: String,
        event: String,
        payload: Value,
    },
    /// An interactive approval request.
    ApprovalRequest {
        id: String,
        tool_call_id: String,
        tool_name: String,
        args: Value,
        risk_level: String,
        reason: String,
        warnings: Vec<String>,
        run_id: Option<String>,
    },
    /// An interactive question request.
    QuestionRequest {
        id: String,
        question: String,
        options: Vec<String>,
        run_id: Option<String>,
    },
    InteractiveRequestCleared {
        request_id: String,
        request_kind: String,
        timed_out: bool,
        run_id: Option<String>,
    },
    /// A plan proposed by the agent for user review.
    PlanCreated {
        id: String,
        summary: String,
        steps: Vec<PlanStepPayload>,
        estimated_turns: usize,
        run_id: Option<String>,
    },
    /// Updated todo list after a `todo_write` tool call.
    TodoUpdate {
        todos: Vec<TodoItemPayload>,
        run_id: Option<String>,
    },
    /// A plan step was completed by the agent.
    PlanStepComplete {
        step_id: String,
        run_id: Option<String>,
    },
}

/// A single todo item for the frontend.
#[derive(Clone, Debug, serde::Serialize)]
pub struct TodoItemPayload {
    pub content: String,
    pub status: String,
    pub priority: String,
}

/// A single step in a plan payload for the frontend.
#[derive(Clone, Debug, serde::Serialize)]
pub struct PlanStepPayload {
    pub id: String,
    pub description: String,
    pub files: Vec<String>,
    pub action: String,
    pub depends_on: Vec<String>,
}

/// Shared state for the web server, wrapped in `Arc` for axum's `State` extractor.
#[derive(Clone)]
pub struct WebState {
    pub inner: Arc<WebStateInner>,
}

pub struct WebStateInner {
    pub stack: Arc<AgentStack>,
    pub db: Arc<ava_db::Database>,
    /// Serializes run startup so session/run ownership checks stay atomic.
    pub startup_lock: Mutex<()>,
    /// Serializes queue enqueue and revocation boundaries.
    pub queue_lifecycle_lock: Mutex<()>,
    /// Serializes interactive prompt registration with cancel drainage.
    pub interactive_lifecycle_lock: Arc<Mutex<()>>,
    /// Active runs keyed by web-owned run ID.
    pub runs: RwLock<HashMap<String, Arc<WebRunState>>>,
    /// Reverse lookup from session ID to active run ID.
    pub session_runs: RwLock<HashMap<uuid::Uuid, String>>,
    /// Broadcast channel for agent events — all WebSocket clients subscribe.
    pub event_tx: broadcast::Sender<WebEvent>,
    /// Pending approval reply; set by the approval forwarder, consumed by resolve_approval.
    pub pending_approval_reply: PendingApprovalReply,
    /// Pending question reply; set by the question forwarder, consumed by resolve_question.
    pub pending_question_reply: PendingQuestionReply,
    /// Pending plan reply; set by the plan forwarder, consumed by resolve_plan.
    pub pending_plan_reply: PendingPlanReply,
    /// Cached pending interactive request events keyed by request_id.
    ///
    /// Same-kind interactive prompts are only actionable in FIFO order. We keep
    /// every pending payload here so queued promotions and session rehydration
    /// can reconstruct the current actionable event for a run.
    pub deferred_interactive_events: Mutex<HashMap<String, WebEvent>>,
    /// Session ID from the last completed run, used for retry/regenerate/undo.
    pub last_session_id: RwLock<Option<uuid::Uuid>>,
    /// File-edit undo history, keyed by session.
    pub edit_history: Arc<RwLock<HashMap<uuid::Uuid, VecDeque<FileEditRecord>>>>,
    /// Follow-up and post-complete items preserved across cancellation, by session.
    pub deferred_queue: Arc<RwLock<HashMap<uuid::Uuid, VecDeque<QueuedMessage>>>>,
    /// Deferred items that started execution and must be restored on cancel, by session.
    pub in_flight_deferred: Arc<RwLock<HashMap<uuid::Uuid, VecDeque<QueuedMessage>>>>,
}

impl WebState {
    /// Initialise the web state with a fresh `AgentStack`.
    pub async fn init(data_dir: PathBuf) -> Result<Self> {
        let db_path = data_dir.join("ava.db");
        let db = ava_db::Database::create_at(db_path).await?;
        db.run_migrations().await?;
        let config = AgentStackConfig::for_web(data_dir);

        let (stack, question_rx, approval_rx, plan_rx) = AgentStack::new(config).await?;

        // Broadcast channel: 256-event buffer. Slow readers drop old events.
        let (event_tx, _) = broadcast::channel(256);

        let inner = Arc::new(WebStateInner {
            stack: Arc::new(stack),
            db: Arc::new(db),
            startup_lock: Mutex::new(()),
            queue_lifecycle_lock: Mutex::new(()),
            interactive_lifecycle_lock: Arc::new(Mutex::new(())),
            runs: RwLock::new(HashMap::new()),
            session_runs: RwLock::new(HashMap::new()),
            event_tx,
            pending_approval_reply: InteractiveRequestStore::new(InteractiveRequestKind::Approval),
            pending_question_reply: InteractiveRequestStore::new(InteractiveRequestKind::Question),
            pending_plan_reply: InteractiveRequestStore::new(InteractiveRequestKind::Plan),
            deferred_interactive_events: Mutex::new(HashMap::new()),
            last_session_id: RwLock::new(None),
            edit_history: Arc::new(RwLock::new(HashMap::new())),
            deferred_queue: Arc::new(RwLock::new(HashMap::new())),
            in_flight_deferred: Arc::new(RwLock::new(HashMap::new())),
        });

        super::api_agent::spawn_interactive_forwarders(
            inner.clone(),
            approval_rx,
            question_rx,
            plan_rx,
        );

        Ok(Self { inner })
    }

    pub async fn register_run(
        &self,
        run_id: String,
        session_id: uuid::Uuid,
        provider: String,
        model: String,
    ) -> Result<Arc<WebRunState>, String> {
        {
            let runs = self.inner.runs.read().await;
            if runs.contains_key(&run_id) {
                return Err(format!("Run {run_id} is already active"));
            }
        }

        {
            let session_runs = self.inner.session_runs.read().await;
            if let Some(existing_run_id) = session_runs.get(&session_id) {
                return Err(format!(
                    "Session {session_id} already has an active run ({existing_run_id})"
                ));
            }
        }

        let run = Arc::new(WebRunState::new(
            run_id.clone(),
            session_id,
            provider,
            model,
        ));
        self.inner
            .runs
            .write()
            .await
            .insert(run_id.clone(), run.clone());
        self.inner
            .session_runs
            .write()
            .await
            .insert(session_id, run_id);
        Ok(run)
    }

    pub async fn finish_run(&self, run_id: &str) {
        let removed = self.inner.runs.write().await.remove(run_id);
        if let Some(run) = removed {
            self.inner
                .session_runs
                .write()
                .await
                .remove(&run.session_id);
        }
    }

    pub async fn active_run_count(&self) -> usize {
        self.inner.runs.read().await.len()
    }

    pub async fn resolve_run(
        &self,
        run_id: Option<&str>,
        session_id: Option<uuid::Uuid>,
    ) -> Result<Arc<WebRunState>, String> {
        let runs = self.inner.runs.read().await;
        let session_runs = self.inner.session_runs.read().await;

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
                0 => Err("No active web runs".to_string()),
                1 => runs
                    .values()
                    .next()
                    .cloned()
                    .ok_or_else(|| "No active web runs".to_string()),
                _ => Err("Multiple web runs are active; provide run_id or session_id".to_string()),
            },
        }
    }

    pub async fn single_active_run_id(&self) -> Option<String> {
        let runs = self.inner.runs.read().await;
        (runs.len() == 1)
            .then(|| runs.values().next().map(|run| run.run_id.clone()))
            .flatten()
    }

    pub async fn cancel(&self) {
        let runs = self.inner.runs.read().await;
        for run in runs.values() {
            run.cancel.cancel();
        }
    }

    pub async fn is_run_interactive_revoked(&self, run_id: Option<&str>) -> bool {
        let Some(run_id) = run_id else {
            return false;
        };
        self.inner.runs.read().await.get(run_id).is_some_and(|run| {
            run.interactive_revoked
                .load(std::sync::atomic::Ordering::SeqCst)
        })
    }

    pub async fn has_active_runs(&self) -> bool {
        self.active_run_count().await > 0
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
            run_id: run.run_id.clone(),
            session_id: run.session_id,
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
        session_id: Option<uuid::Uuid>,
    ) -> Result<Option<QueueDispatchSnapshot>, String> {
        let run = self.resolve_run(run_id, session_id).await?;
        let snapshot = run.queue_dispatch.lock().await.clone();
        Ok(snapshot)
    }

    pub async fn enqueue_message(
        &self,
        message: QueuedMessage,
        run_id: Option<&str>,
        requested_session_id: Option<uuid::Uuid>,
        persist_deferred: bool,
    ) -> Result<(), String> {
        let _queue_guard = self.inner.queue_lifecycle_lock.lock().await;
        let run = self.resolve_run(run_id, requested_session_id).await?;
        let dispatch = run.queue_dispatch.lock().await;
        let snapshot = dispatch
            .clone()
            .ok_or_else(|| "Agent queue is unavailable".to_string())?;
        if !snapshot.accepting {
            return Err("Agent queue is unavailable".to_string());
        }

        let deferred_owner = if persist_deferred {
            Some(
                resolve_deferred_queue_session(requested_session_id, Some(run.session_id))
                    .map_err(|error| error.to_string())?,
            )
        } else {
            None
        };

        let tx = snapshot
            .tx
            .as_ref()
            .ok_or_else(|| "Agent queue is unavailable".to_string())?;
        tx.send(message.clone())
            .map_err(|_| "Agent queue is unavailable".to_string())?;
        drop(dispatch);

        if let Some(session_id) = deferred_owner {
            self.inner
                .deferred_queue
                .write()
                .await
                .entry(session_id)
                .or_default()
                .push_back(message);
        }

        Ok(())
    }

    pub async fn revoke_queue_dispatch(&self, run_id: &str, clear_steering: bool) {
        let _queue_guard = self.inner.queue_lifecycle_lock.lock().await;
        let Ok(run) = self.resolve_run(Some(run_id), None).await else {
            return;
        };
        let control = run.queue_control.lock().await.take();
        *run.queue_dispatch.lock().await = None;

        if clear_steering {
            if let Some(control) = control {
                control.clear_steering();
            }
        }
    }

    pub async fn push_edit(&self, session_id: uuid::Uuid, record: FileEditRecord) {
        let mut history = self.inner.edit_history.write().await;
        let session_history = history.entry(session_id).or_default();
        if session_history.len() >= 100 {
            session_history.pop_front();
        }
        session_history.push_back(record);
    }

    pub async fn pop_last_edit(&self, session_id: uuid::Uuid) -> Option<FileEditRecord> {
        let mut history = self.inner.edit_history.write().await;
        let session_history = history.get_mut(&session_id)?;
        let record = session_history.pop_back();
        if session_history.is_empty() {
            history.remove(&session_id);
        }
        record
    }
}
