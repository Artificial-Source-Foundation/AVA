//! Shared web server state — owns an `AgentStack` and coordination primitives.
//!
//! Analogous to `DesktopBridge` in the Tauri backend, but without Tauri-specific
//! types. Agent events are broadcast to all connected WebSocket clients via a
//! `tokio::sync::broadcast` channel.

use std::path::PathBuf;
use std::sync::Arc;

use ava_agent::agent_loop::AgentEvent;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::ApprovalRequest;
use ava_types::QueuedMessage;
use color_eyre::Result;
use tokio::sync::{broadcast, mpsc, RwLock};
use tokio_util::sync::CancellationToken;

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
    pub event_tx: broadcast::Sender<AgentEvent>,
    /// Question receiver (auto-answer for now in web mode).
    #[allow(dead_code)]
    pub question_rx: tokio::sync::Mutex<mpsc::UnboundedReceiver<QuestionRequest>>,
    /// Approval receiver (auto-approve for now in web mode).
    #[allow(dead_code)]
    pub approval_rx: tokio::sync::Mutex<mpsc::UnboundedReceiver<ApprovalRequest>>,
    /// Message queue sender for mid-stream messaging (3-tier).
    /// `None` when no agent is running; set before each run.
    pub message_queue: RwLock<Option<mpsc::UnboundedSender<QueuedMessage>>>,
}

impl WebState {
    /// Initialise the web state with a fresh `AgentStack`.
    pub async fn init(data_dir: PathBuf) -> Result<Self> {
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

        let (stack, question_rx, approval_rx) = AgentStack::new(config).await?;

        // Broadcast channel: 256-event buffer. Slow readers drop old events.
        let (event_tx, _) = broadcast::channel(256);

        Ok(Self {
            inner: Arc::new(WebStateInner {
                stack: Arc::new(stack),
                cancel: RwLock::new(CancellationToken::new()),
                running: RwLock::new(false),
                event_tx,
                question_rx: tokio::sync::Mutex::new(question_rx),
                approval_rx: tokio::sync::Mutex::new(approval_rx),
                message_queue: RwLock::new(None),
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
