//! Shared web server state — owns an `AgentStack` and coordination primitives.
//!
//! Analogous to `DesktopBridge` in the Tauri backend, but without Tauri-specific
//! types. Agent events are broadcast to all connected WebSocket clients via a
//! `tokio::sync::broadcast` channel.

use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;
use std::sync::{atomic::AtomicBool, Arc};

use ava_agent::control_plane::interactive::{InteractiveRequestKind, InteractiveRequestStore};
use ava_agent::control_plane::queue::resolve_deferred_queue_session;
use ava_agent::message_queue::MessageQueueControl;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_tools::core::plan::PlanRequest;
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::{PlanDecision, QueuedMessage};
use color_eyre::Result;
use serde_json::Value;
use tokio::sync::{broadcast, mpsc, Mutex, RwLock};
use tokio_util::sync::CancellationToken;

#[derive(Clone, Default)]
pub struct QueueDispatchSnapshot {
    pub accepting: bool,
    pub active_session_id: Option<uuid::Uuid>,
    pub tx: Option<mpsc::UnboundedSender<QueuedMessage>>,
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
    /// Cancellation token for the current agent run.
    pub cancel: RwLock<CancellationToken>,
    /// Whether the agent is currently running.
    pub running: RwLock<bool>,
    /// Serializes run startup so only one run can claim ownership at a time.
    pub startup_lock: Mutex<()>,
    /// Serializes queue enqueue and revocation boundaries.
    pub queue_lifecycle_lock: Mutex<()>,
    /// Serializes interactive prompt registration with cancel drainage.
    pub interactive_lifecycle_lock: Mutex<()>,
    /// Broadcast channel for agent events — all WebSocket clients subscribe.
    pub event_tx: broadcast::Sender<WebEvent>,
    /// Question receiver — drained each run to forward question_request WS events.
    pub question_rx: Mutex<mpsc::UnboundedReceiver<QuestionRequest>>,
    /// Approval receiver — drained each run to forward approval_request WS events.
    pub approval_rx: Mutex<mpsc::UnboundedReceiver<ApprovalRequest>>,
    /// Plan receiver — drained each run to forward plan_created WS events.
    pub plan_rx: Mutex<mpsc::UnboundedReceiver<PlanRequest>>,
    /// Message queue sender for mid-stream messaging (3-tier).
    /// `None` when no agent is running; set before each run.
    pub message_queue: RwLock<Option<mpsc::UnboundedSender<QueuedMessage>>>,
    /// Pending approval reply; set by the approval forwarder, consumed by resolve_approval.
    pub pending_approval_reply: PendingApprovalReply,
    /// Pending question reply; set by the question forwarder, consumed by resolve_question.
    pub pending_question_reply: PendingQuestionReply,
    /// Pending plan reply; set by the plan forwarder, consumed by resolve_plan.
    pub pending_plan_reply: PendingPlanReply,
    /// Session ID from the last completed run, used for retry/regenerate/undo.
    pub last_session_id: RwLock<Option<uuid::Uuid>>,
    /// Session ID for the currently-running agent.
    pub active_session_id: RwLock<Option<uuid::Uuid>>,
    /// Stack of file edits for undo support.
    pub edit_history: Arc<RwLock<VecDeque<FileEditRecord>>>,
    /// Follow-up and post-complete items preserved across cancellation, by session.
    pub deferred_queue: Arc<RwLock<HashMap<uuid::Uuid, VecDeque<QueuedMessage>>>>,
    /// Deferred items that started execution and must be restored on cancel, by session.
    pub in_flight_deferred: Arc<RwLock<HashMap<uuid::Uuid, VecDeque<QueuedMessage>>>>,
    /// Atomic queue acceptance and ownership snapshot for queue endpoints.
    pub queue_dispatch: Mutex<QueueDispatchSnapshot>,
    /// Live queue control used to clear pending steering before cancel returns.
    pub queue_control: Mutex<Option<MessageQueueControl>>,
    /// Blocks new interactive prompts from being forwarded once cancellation starts.
    pub interactive_revoked: Arc<AtomicBool>,
}

impl WebState {
    /// Initialise the web state with a fresh `AgentStack`.
    pub async fn init(data_dir: PathBuf) -> Result<Self> {
        let db = ava_db::Database::create_at(data_dir.join("ava.db")).await?;
        db.run_migrations().await?;
        let config = AgentStackConfig::for_web(data_dir);

        let (stack, question_rx, approval_rx, plan_rx) = AgentStack::new(config).await?;

        // Broadcast channel: 256-event buffer. Slow readers drop old events.
        let (event_tx, _) = broadcast::channel(256);

        Ok(Self {
            inner: Arc::new(WebStateInner {
                stack: Arc::new(stack),
                db: Arc::new(db),
                cancel: RwLock::new(CancellationToken::new()),
                running: RwLock::new(false),
                startup_lock: Mutex::new(()),
                queue_lifecycle_lock: Mutex::new(()),
                interactive_lifecycle_lock: Mutex::new(()),
                event_tx,
                question_rx: Mutex::new(question_rx),
                approval_rx: Mutex::new(approval_rx),
                plan_rx: Mutex::new(plan_rx),
                message_queue: RwLock::new(None),
                pending_approval_reply: InteractiveRequestStore::new(
                    InteractiveRequestKind::Approval,
                ),
                pending_question_reply: InteractiveRequestStore::new(
                    InteractiveRequestKind::Question,
                ),
                pending_plan_reply: InteractiveRequestStore::new(InteractiveRequestKind::Plan),
                last_session_id: RwLock::new(None),
                active_session_id: RwLock::new(None),
                edit_history: Arc::new(RwLock::new(VecDeque::new())),
                deferred_queue: Arc::new(RwLock::new(HashMap::new())),
                in_flight_deferred: Arc::new(RwLock::new(HashMap::new())),
                queue_dispatch: Mutex::new(QueueDispatchSnapshot::default()),
                queue_control: Mutex::new(None),
                interactive_revoked: Arc::new(AtomicBool::new(false)),
            }),
        })
    }

    /// Replace the cancellation token and return the new one.
    pub async fn new_cancel_token(&self) -> CancellationToken {
        let token = CancellationToken::new();
        *self.inner.cancel.write().await = token.clone();
        token
    }

    /// Cancel the currently-running agent.
    pub async fn cancel(&self) {
        self.inner.cancel.read().await.cancel();
    }

    pub async fn activate_message_queue(
        &self,
        session_id: uuid::Uuid,
        tx: mpsc::UnboundedSender<QueuedMessage>,
        control: MessageQueueControl,
    ) {
        let mut dispatch = self.inner.queue_dispatch.lock().await;
        dispatch.accepting = true;
        dispatch.active_session_id = Some(session_id);
        dispatch.tx = Some(tx.clone());
        drop(dispatch);

        *self.inner.queue_control.lock().await = Some(control);
        *self.inner.active_session_id.write().await = Some(session_id);
        *self.inner.message_queue.write().await = Some(tx);
    }

    pub async fn clear_message_queue_dispatch(&self) {
        *self.inner.queue_control.lock().await = None;
        let mut dispatch = self.inner.queue_dispatch.lock().await;
        dispatch.accepting = false;
        dispatch.active_session_id = None;
        dispatch.tx = None;
        drop(dispatch);

        *self.inner.active_session_id.write().await = None;
        *self.inner.message_queue.write().await = None;
    }

    pub async fn queue_dispatch_snapshot(&self) -> QueueDispatchSnapshot {
        self.inner.queue_dispatch.lock().await.clone()
    }

    pub async fn enqueue_message(
        &self,
        message: QueuedMessage,
        requested_session_id: Option<uuid::Uuid>,
        persist_deferred: bool,
    ) -> Result<(), String> {
        let _queue_guard = self.inner.queue_lifecycle_lock.lock().await;
        let dispatch = self.inner.queue_dispatch.lock().await;
        if !dispatch.accepting {
            return Err("Agent queue is unavailable".to_string());
        }

        let deferred_owner = if persist_deferred {
            Some(
                resolve_deferred_queue_session(requested_session_id, dispatch.active_session_id)
                    .map_err(|error| error.to_string())?,
            )
        } else {
            None
        };

        let tx = dispatch
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

    pub async fn revoke_queue_dispatch(&self, clear_steering: bool) {
        let _queue_guard = self.inner.queue_lifecycle_lock.lock().await;
        let control = self.inner.queue_control.lock().await.take();
        {
            let mut dispatch = self.inner.queue_dispatch.lock().await;
            dispatch.accepting = false;
            dispatch.active_session_id = None;
            dispatch.tx = None;
        }
        *self.inner.active_session_id.write().await = None;
        *self.inner.message_queue.write().await = None;

        if clear_steering {
            if let Some(control) = control {
                control.clear_steering();
            }
        }
    }
}
