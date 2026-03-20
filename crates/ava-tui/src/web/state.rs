//! Shared web server state — owns an `AgentStack` and coordination primitives.
//!
//! Analogous to `DesktopBridge` in the Tauri backend, but without Tauri-specific
//! types. Agent events are broadcast to all connected WebSocket clients via a
//! `tokio::sync::broadcast` channel.

use std::collections::VecDeque;
use std::path::PathBuf;
use std::sync::Arc;

use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_tools::core::plan::PlanRequest;
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::{PlanDecision, QueuedMessage};
use color_eyre::Result;
use serde_json::Value;
use tokio::sync::{broadcast, mpsc, oneshot, Mutex, RwLock};
use tokio_util::sync::CancellationToken;

/// Pending oneshot sender for an approval request.
pub type PendingApprovalReply = Arc<Mutex<Option<oneshot::Sender<ToolApproval>>>>;
/// Pending oneshot sender for a question request.
pub type PendingQuestionReply = Arc<Mutex<Option<oneshot::Sender<String>>>>;
/// Pending oneshot sender for a plan decision.
pub type PendingPlanReply = Arc<Mutex<Option<oneshot::Sender<PlanDecision>>>>;

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
    Agent(ava_agent::agent_loop::AgentEvent),
    /// An interactive approval request.
    ApprovalRequest {
        id: String,
        tool_name: String,
        args: Value,
        risk_level: String,
        reason: String,
        warnings: Vec<String>,
    },
    /// An interactive question request.
    QuestionRequest {
        id: String,
        question: String,
        options: Vec<String>,
    },
    /// A plan proposed by the agent for user review.
    PlanCreated {
        summary: String,
        steps: Vec<PlanStepPayload>,
        estimated_turns: usize,
    },
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
    /// Cancellation token for the current agent run.
    pub cancel: RwLock<CancellationToken>,
    /// Whether the agent is currently running.
    pub running: RwLock<bool>,
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
    /// Stack of file edits for undo support.
    pub edit_history: Arc<RwLock<VecDeque<FileEditRecord>>>,
}

impl WebState {
    /// Initialise the web state with a fresh `AgentStack`.
    pub async fn init(data_dir: PathBuf) -> Result<Self> {
        let config = AgentStackConfig {
            data_dir,
            ..Default::default()
        };

        let (stack, question_rx, approval_rx, plan_rx) = AgentStack::new(config).await?;

        // Broadcast channel: 256-event buffer. Slow readers drop old events.
        let (event_tx, _) = broadcast::channel(256);

        Ok(Self {
            inner: Arc::new(WebStateInner {
                stack: Arc::new(stack),
                cancel: RwLock::new(CancellationToken::new()),
                running: RwLock::new(false),
                event_tx,
                question_rx: Mutex::new(question_rx),
                approval_rx: Mutex::new(approval_rx),
                plan_rx: Mutex::new(plan_rx),
                message_queue: RwLock::new(None),
                pending_approval_reply: Arc::new(Mutex::new(None)),
                pending_question_reply: Arc::new(Mutex::new(None)),
                pending_plan_reply: Arc::new(Mutex::new(None)),
                last_session_id: RwLock::new(None),
                edit_history: Arc::new(RwLock::new(VecDeque::new())),
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
}
