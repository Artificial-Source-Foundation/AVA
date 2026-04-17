//! Bridge between the Tauri desktop frontend and the Rust agent backend.
//!
//! `DesktopBridge` owns an `AgentStack` (the same struct the TUI uses) and
//! exposes it to Tauri commands via `tauri::State`. It also holds the
//! cancellation token, message queue sender, and question/approval receivers
//! so that the desktop frontend can participate in the interactive agent loop.

use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;
use std::sync::{atomic::AtomicBool, Arc};

use ava_agent::control_plane::interactive::{InteractiveRequestKind, InteractiveRequestStore};
use ava_agent::control_plane::queue::resolve_deferred_queue_session;
use ava_agent::message_queue::{MessageQueue, MessageQueueControl};
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_tools::core::plan::PlanRequest;
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::{MessageTier, QueuedMessage};
use tokio::sync::{mpsc, Mutex, RwLock};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

pub type PendingApprovalReply = InteractiveRequestStore<ToolApproval>;

pub type PendingQuestionReply = InteractiveRequestStore<String>;

pub type PendingPlanReply = InteractiveRequestStore<ava_types::PlanDecision>;

#[derive(Clone, Default)]
pub struct QueueDispatchSnapshot {
    pub accepting: bool,
    pub active_session_id: Option<Uuid>,
    pub tx: Option<mpsc::UnboundedSender<QueuedMessage>>,
}

/// Tracks a file edit so that undo can restore the previous content.
#[derive(Debug, Clone)]
pub struct FileEditRecord {
    pub file_path: String,
    pub previous_content: String,
}

/// Maximum number of file edit records to keep in the undo stack.
/// Used in `agent_commands.rs` forwarder task.
pub(crate) const MAX_EDIT_HISTORY: usize = 100;

/// Shared state managed by Tauri. Provides access to the `AgentStack` and
/// the cancellation token for the currently-running agent task.
pub struct DesktopBridge {
    pub stack: Arc<AgentStack>,
    /// Cancel the currently-running agent. Replaced on each `submit_goal`.
    pub cancel: RwLock<CancellationToken>,
    /// Sender for mid-stream messages (3-tier: steering, follow-up, post-complete).
    /// Replaced on each `submit_goal` when a new `MessageQueue` is created.
    pub message_tx: RwLock<Option<mpsc::UnboundedSender<QueuedMessage>>>,
    /// Receiver for interactive question requests from the agent.
    pub question_rx: Mutex<mpsc::UnboundedReceiver<QuestionRequest>>,
    /// Receiver for tool-approval requests from the permission middleware.
    pub approval_rx: Mutex<mpsc::UnboundedReceiver<ApprovalRequest>>,
    /// Receiver for plan approval requests from the plan tool.
    pub plan_rx: Mutex<mpsc::UnboundedReceiver<PlanRequest>>,
    /// Whether an agent task is currently running.
    pub running: RwLock<bool>,
    /// Run ID for the currently-running agent, when present.
    pub active_run_id: RwLock<Option<String>>,
    /// Serializes run startup so only one run can claim ownership at a time.
    pub startup_lock: Mutex<()>,
    /// Serializes queue enqueue and revocation boundaries.
    pub queue_lifecycle_lock: Mutex<()>,
    /// Serializes interactive prompt registration with cancel drainage.
    pub interactive_lifecycle_lock: Arc<Mutex<()>>,
    /// Pending approval reply channel. Set when an approval request is forwarded
    /// to the frontend; consumed when the frontend calls `resolve_approval`.
    pub pending_approval_reply: PendingApprovalReply,
    /// Pending question reply channel. Set when a question request is forwarded
    /// to the frontend; consumed when the frontend calls `resolve_question`.
    pub pending_question_reply: PendingQuestionReply,
    /// Pending plan reply channel. Set when a plan_created event is forwarded
    /// to the frontend; consumed when the frontend calls `resolve_plan`.
    pub pending_plan_reply: PendingPlanReply,
    /// The session ID of the last completed (or checkpointed) agent run.
    /// Used for retry/regenerate and to load history for the next run.
    pub last_session_id: Arc<RwLock<Option<Uuid>>>,
    /// Session ID for the currently-running agent.
    pub active_session_id: Arc<RwLock<Option<Uuid>>>,
    /// Stack of file edits made by the agent, most recent last.
    /// Used by `undo_last_edit` to restore the previous content.
    pub edit_history: Arc<RwLock<VecDeque<FileEditRecord>>>,
    /// Follow-up and post-complete items preserved across cancellation, by session.
    pub deferred_queue: Arc<RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>>,
    /// Deferred items that started execution and must be restored on cancel, by session.
    pub in_flight_deferred: Arc<RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>>,
    /// Atomic queue acceptance and ownership snapshot for queue commands.
    pub queue_dispatch: Mutex<QueueDispatchSnapshot>,
    /// Live queue control used to clear pending steering before cancel returns.
    pub queue_control: Mutex<Option<MessageQueueControl>>,
    /// Blocks new interactive prompts from being forwarded once cancellation starts.
    pub interactive_revoked: Arc<AtomicBool>,
}

impl DesktopBridge {
    /// Initialise the bridge.
    ///
    /// `data_dir` is the Tauri app-data directory (e.g. `~/.local/share/ava`).
    /// The `AgentStack` will store sessions, memory and config there.
    pub async fn init(data_dir: PathBuf) -> Result<Self, String> {
        let config = AgentStackConfig::for_desktop(data_dir);

        let (stack, question_rx, approval_rx, plan_rx) =
            AgentStack::new(config).await.map_err(|e| e.to_string())?;

        Ok(Self {
            stack: Arc::new(stack),
            cancel: RwLock::new(CancellationToken::new()),
            message_tx: RwLock::new(None),
            question_rx: Mutex::new(question_rx),
            approval_rx: Mutex::new(approval_rx),
            plan_rx: Mutex::new(plan_rx),
            running: RwLock::new(false),
            active_run_id: RwLock::new(None),
            startup_lock: Mutex::new(()),
            queue_lifecycle_lock: Mutex::new(()),
            interactive_lifecycle_lock: Arc::new(Mutex::new(())),
            pending_approval_reply: InteractiveRequestStore::new(InteractiveRequestKind::Approval),
            pending_question_reply: InteractiveRequestStore::new(InteractiveRequestKind::Question),
            pending_plan_reply: InteractiveRequestStore::new(InteractiveRequestKind::Plan),
            last_session_id: Arc::new(RwLock::new(None)),
            active_session_id: Arc::new(RwLock::new(None)),
            edit_history: Arc::new(RwLock::new(VecDeque::new())),
            deferred_queue: Arc::new(RwLock::new(HashMap::new())),
            in_flight_deferred: Arc::new(RwLock::new(HashMap::new())),
            queue_dispatch: Mutex::new(QueueDispatchSnapshot::default()),
            queue_control: Mutex::new(None),
            interactive_revoked: Arc::new(AtomicBool::new(false)),
        })
    }

    /// Replace the cancellation token. Returns the new token.
    pub async fn new_cancel_token(&self) -> CancellationToken {
        let token = CancellationToken::new();
        *self.cancel.write().await = token.clone();
        token
    }

    /// Cancel the currently-running agent.
    pub async fn cancel(&self) {
        self.cancel.read().await.cancel();
    }

    /// Create a new `MessageQueue` and store the sender half.
    /// Returns the `MessageQueue` to be passed to `AgentStack::run()`.
    pub async fn new_message_queue(&self, session_id: Option<Uuid>) -> MessageQueue {
        let (queue, tx, control) = MessageQueue::new_with_control();
        if let Some(session_id) = session_id {
            if let Some(messages) = self.deferred_queue.read().await.get(&session_id) {
                for message in messages.iter().cloned() {
                    let _ = tx.send(message);
                }
            }
        }
        {
            let mut dispatch = self.queue_dispatch.lock().await;
            dispatch.accepting = true;
            dispatch.active_session_id = session_id;
            dispatch.tx = Some(tx.clone());
        }
        *self.queue_control.lock().await = Some(control);
        *self.message_tx.write().await = Some(tx);
        *self.active_session_id.write().await = session_id;
        queue
    }

    /// Send a message to the agent's message queue.
    /// Returns `Err` if the agent is not running or the channel is closed.
    pub async fn send_message(
        &self,
        text: String,
        tier: MessageTier,
        requested_session_id: Option<Uuid>,
    ) -> Result<(), String> {
        let _queue_guard = self.queue_lifecycle_lock.lock().await;
        let dispatch = self.queue_dispatch.lock().await;
        if !dispatch.accepting {
            return Err("Agent queue is unavailable".to_string());
        }

        let deferred_owner = if matches!(tier, MessageTier::Steering) {
            None
        } else {
            Some(
                resolve_deferred_queue_session(requested_session_id, dispatch.active_session_id)
                    .map_err(|error| error.to_string())?,
            )
        };

        let tx = dispatch
            .tx
            .as_ref()
            .ok_or_else(|| "Agent queue is unavailable".to_string())?;
        let queued = QueuedMessage {
            text: text.clone(),
            tier: tier.clone(),
        };
        tx.send(queued.clone())
            .map_err(|_| "Message queue channel closed. Agent may have finished.".to_string())?;
        drop(dispatch);
        if let Some(session_id) = deferred_owner {
            self.deferred_queue
                .write()
                .await
                .entry(session_id)
                .or_default()
                .push_back(queued);
        }
        Ok(())
    }

    /// Clear the message sender when the agent finishes.
    pub async fn clear_message_tx(&self) {
        *self.queue_control.lock().await = None;
        let mut dispatch = self.queue_dispatch.lock().await;
        dispatch.accepting = false;
        dispatch.active_session_id = None;
        dispatch.tx = None;
        drop(dispatch);
        *self.message_tx.write().await = None;
        *self.active_session_id.write().await = None;
    }

    pub async fn queue_dispatch_snapshot(&self) -> QueueDispatchSnapshot {
        self.queue_dispatch.lock().await.clone()
    }

    pub async fn revoke_queue_dispatch(&self, clear_steering: bool) {
        let _queue_guard = self.queue_lifecycle_lock.lock().await;
        let control = self.queue_control.lock().await.take();
        {
            let mut dispatch = self.queue_dispatch.lock().await;
            dispatch.accepting = false;
            dispatch.active_session_id = None;
            dispatch.tx = None;
        }
        *self.message_tx.write().await = None;
        *self.active_session_id.write().await = None;

        if clear_steering {
            if let Some(control) = control {
                control.clear_steering();
            }
        }
    }

    /// Pop the most recent file edit record from the undo stack.
    pub async fn pop_last_edit(&self) -> Option<FileEditRecord> {
        self.edit_history.write().await.pop_back()
    }
}
