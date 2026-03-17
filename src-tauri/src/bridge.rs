//! Bridge between the Tauri desktop frontend and the Rust agent backend.
//!
//! `DesktopBridge` owns an `AgentStack` (the same struct the TUI uses) and
//! exposes it to Tauri commands via `tauri::State`. It also holds the
//! cancellation token, message queue sender, and question/approval receivers
//! so that the desktop frontend can participate in the interactive agent loop.

use std::collections::VecDeque;
use std::path::PathBuf;
use std::sync::Arc;

use ava_agent::message_queue::MessageQueue;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::{MessageTier, QueuedMessage};
use tokio::sync::{mpsc, oneshot, Mutex, RwLock};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

/// Pending reply channel for approval requests, shared between the forwarder
/// task and the `resolve_approval` command.
pub type PendingApprovalReply = Arc<Mutex<Option<oneshot::Sender<ToolApproval>>>>;

/// Pending reply channel for question requests, shared between the forwarder
/// task and the `resolve_question` command.
pub type PendingQuestionReply = Arc<Mutex<Option<oneshot::Sender<String>>>>;

/// Tracks a file edit so that undo can restore the previous content.
#[derive(Debug, Clone)]
pub struct FileEditRecord {
    pub file_path: String,
    pub previous_content: String,
    pub timestamp: chrono::DateTime<chrono::Utc>,
}

/// Maximum number of file edit records to keep in the undo stack.
const MAX_EDIT_HISTORY: usize = 100;

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
    /// Whether an agent task is currently running.
    pub running: RwLock<bool>,
    /// Pending approval reply channel. Set when an approval request is forwarded
    /// to the frontend; consumed when the frontend calls `resolve_approval`.
    pub pending_approval_reply: PendingApprovalReply,
    /// Pending question reply channel. Set when a question request is forwarded
    /// to the frontend; consumed when the frontend calls `resolve_question`.
    pub pending_question_reply: PendingQuestionReply,
    /// The session ID of the last completed agent run, used for retry/regenerate.
    pub last_session_id: RwLock<Option<Uuid>>,
    /// Stack of file edits made by the agent, most recent last.
    /// Used by `undo_last_edit` to restore the previous content.
    pub edit_history: Arc<RwLock<VecDeque<FileEditRecord>>>,
}

impl DesktopBridge {
    /// Initialise the bridge.
    ///
    /// `data_dir` is the Tauri app-data directory (e.g. `~/.local/share/ava`).
    /// The `AgentStack` will store sessions, memory and config there.
    pub async fn init(data_dir: PathBuf) -> Result<Self, String> {
        let config = AgentStackConfig {
            data_dir,
            provider: None,
            model: None,
            max_turns: 0,
            max_budget_usd: 0.0,
            yolo: false,
            injected_provider: None,
            working_dir: None,
        };

        let (stack, question_rx, approval_rx) =
            AgentStack::new(config).await.map_err(|e| e.to_string())?;

        Ok(Self {
            stack: Arc::new(stack),
            cancel: RwLock::new(CancellationToken::new()),
            message_tx: RwLock::new(None),
            question_rx: Mutex::new(question_rx),
            approval_rx: Mutex::new(approval_rx),
            running: RwLock::new(false),
            pending_approval_reply: Arc::new(Mutex::new(None)),
            pending_question_reply: Arc::new(Mutex::new(None)),
            last_session_id: RwLock::new(None),
            edit_history: Arc::new(RwLock::new(VecDeque::new())),
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
    pub async fn new_message_queue(&self) -> MessageQueue {
        let (queue, tx) = MessageQueue::new();
        *self.message_tx.write().await = Some(tx);
        queue
    }

    /// Send a message to the agent's message queue.
    /// Returns `Err` if the agent is not running or the channel is closed.
    pub async fn send_message(&self, text: String, tier: MessageTier) -> Result<(), String> {
        let guard = self.message_tx.read().await;
        let tx = guard
            .as_ref()
            .ok_or_else(|| "Agent is not running. No message queue available.".to_string())?;
        tx.send(QueuedMessage { text, tier })
            .map_err(|_| "Message queue channel closed. Agent may have finished.".to_string())
    }

    /// Clear the message sender when the agent finishes.
    pub async fn clear_message_tx(&self) {
        *self.message_tx.write().await = None;
    }

    /// Record a file edit for undo support.
    pub async fn record_edit(&self, file_path: String, previous_content: String) {
        let mut history = self.edit_history.write().await;
        if history.len() >= MAX_EDIT_HISTORY {
            history.pop_front();
        }
        history.push_back(FileEditRecord {
            file_path,
            previous_content,
            timestamp: chrono::Utc::now(),
        });
    }

    /// Pop the most recent file edit record from the undo stack.
    pub async fn pop_last_edit(&self) -> Option<FileEditRecord> {
        self.edit_history.write().await.pop_back()
    }
}
