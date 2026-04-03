use crate::state::agent::TokenUsage;
use crate::state::messages::MessageKind;
use crate::ui::status_bar::StatusLevel;
use crate::widgets::model_selector::ModelSelectorState;
use crate::widgets::provider_connect::ProviderConnectState;
use crate::widgets::tool_list::ToolListItem;
use ava_agent::stack::MCPServerInfo;
use ava_agent::AgentEvent;

use ava_types::Session;
use crossterm::event::{Event as CEvent, EventStream, KeyEvent, MouseEvent, MouseEventKind};
use futures::StreamExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;

#[derive(Debug)]
pub enum ModelSwitchContext {
    Selector,
    SessionRestore,
    SlashCommand,
}

#[derive(Debug)]
pub struct ModelSwitchResult {
    pub provider: String,
    pub model: String,
    pub display: String,
    pub result: Result<(), String>,
    pub context: ModelSwitchContext,
}

#[derive(Debug)]
pub struct SessionLoadResult {
    pub session: Session,
    pub restore_model: Option<(String, String)>,
}

#[derive(Debug)]
pub struct CommandMessageResult {
    pub kind: MessageKind,
    pub content: String,
    pub status: Option<(StatusLevel, String)>,
    /// When true, the message will be removed when the user sends their next message.
    pub transient: bool,
}

#[derive(Debug)]
pub enum ProviderConnectResult {
    Loaded(ProviderConnectState),
    Refreshed {
        state: ProviderConnectState,
        status: String,
    },
    Tested(Result<String, String>),
    Saved(Result<String, String>),
    OAuthStored {
        provider: String,
        result: Result<(), String>,
    },
    ConfigureLoaded {
        provider_id: String,
        base_url: String,
    },
    DeviceCodeReady {
        provider_id: String,
        device: ava_auth::device_code::DeviceCodeResponse,
    },
    InlineError(String),
}

pub enum AppEvent {
    Key(KeyEvent),
    Paste(String),
    Resize(u16, u16),
    Mouse(MouseEvent),
    Tick,
    AgentRunEvent {
        run_id: u64,
        event: AgentEvent,
    },
    AgentRunDone {
        run_id: u64,
        result: Result<ava_agent::stack::AgentRunResult, String>,
    },
    BackgroundCleanupResult {
        task_id: usize,
        result: Result<(), String>,
    },
    LspStatusLoaded(Result<ava_lsp::LspSnapshot, String>),
    TokenUsage(TokenUsage),
    ModelSelectorLoaded(Result<ModelSelectorState, String>),
    ModelSwitchFinished(ModelSwitchResult),
    ToolListLoaded(Result<Vec<ToolListItem>, String>),
    McpServersLoaded(Result<Vec<MCPServerInfo>, String>),
    CommandMessage(CommandMessageResult),
    SessionListLoaded(Result<Vec<Session>, String>),
    SessionLoaded(Result<SessionLoadResult, String>),
    ProviderConnectLoaded(Result<ProviderConnectState, String>),
    ProviderConnectFinished(ProviderConnectResult),
    ShellResult(crate::state::messages::MessageKind, String),
    /// Transcription complete — text ready to insert.
    VoiceReady(String),
    /// Voice pipeline error.
    VoiceError(String),
    /// Real-time microphone amplitude (0.0–1.0).
    VoiceAmplitude(f32),
    /// Silence detected — auto-stop recording.
    VoiceSilenceDetected,
    /// OAuth flow completed successfully.
    OAuthSuccess {
        provider: String,
        tokens: ava_auth::tokens::OAuthTokens,
    },
    /// OAuth flow failed.
    OAuthError {
        provider: String,
        error: String,
    },
    /// Agent is asking the user a question via the question tool.
    Question(ava_tools::core::question::QuestionRequest),
    /// Agent is requesting interactive approval for a tool call.
    ToolApproval(ava_tools::permission_middleware::ApprovalRequest),
    /// Agent is proposing a plan for user review via the plan tool.
    PlanProposal(ava_tools::core::plan::PlanRequest),
    /// A hook execution completed (fired asynchronously).
    HookResult {
        event: crate::hooks::HookEvent,
        result: crate::hooks::HookResult,
        description: String,
    },
    /// Code review finished. None = no changes to review.
    /// Some(Ok((formatted, has_actionable))) = review complete.
    /// Some(Err(msg)) = review failed.
    ReviewFinished(Option<Result<(String, bool), String>>),
    Quit,
}

impl std::fmt::Debug for AppEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Key(k) => f.debug_tuple("Key").field(k).finish(),
            Self::Paste(v) => f.debug_tuple("Paste").field(v).finish(),
            Self::Resize(w, h) => f.debug_tuple("Resize").field(w).field(h).finish(),
            Self::Mouse(m) => f.debug_tuple("Mouse").field(m).finish(),
            Self::Tick => write!(f, "Tick"),
            Self::AgentRunEvent { run_id, event } => f
                .debug_struct("AgentRunEvent")
                .field("run_id", run_id)
                .field("event", event)
                .finish(),
            Self::AgentRunDone { run_id, result } => f
                .debug_struct("AgentRunDone")
                .field("run_id", run_id)
                .field("result", result)
                .finish(),
            Self::BackgroundCleanupResult { task_id, result } => f
                .debug_struct("BackgroundCleanupResult")
                .field("task_id", task_id)
                .field("result", result)
                .finish(),
            Self::LspStatusLoaded(result) => {
                f.debug_tuple("LspStatusLoaded").field(result).finish()
            }
            Self::TokenUsage(u) => f.debug_tuple("TokenUsage").field(u).finish(),
            Self::ModelSelectorLoaded(r) => f.debug_tuple("ModelSelectorLoaded").field(r).finish(),
            Self::ModelSwitchFinished(r) => f.debug_tuple("ModelSwitchFinished").field(r).finish(),
            Self::ToolListLoaded(r) => f.debug_tuple("ToolListLoaded").field(r).finish(),
            Self::McpServersLoaded(r) => f.debug_tuple("McpServersLoaded").field(r).finish(),
            Self::CommandMessage(r) => f.debug_tuple("CommandMessage").field(r).finish(),
            Self::SessionListLoaded(r) => f.debug_tuple("SessionListLoaded").field(r).finish(),
            Self::SessionLoaded(r) => f.debug_tuple("SessionLoaded").field(r).finish(),
            Self::ProviderConnectLoaded(r) => {
                f.debug_tuple("ProviderConnectLoaded").field(r).finish()
            }
            Self::ProviderConnectFinished(r) => {
                f.debug_tuple("ProviderConnectFinished").field(r).finish()
            }
            Self::ShellResult(k, c) => f.debug_tuple("ShellResult").field(k).field(c).finish(),
            Self::VoiceReady(t) => f.debug_tuple("VoiceReady").field(t).finish(),
            Self::VoiceError(e) => f.debug_tuple("VoiceError").field(e).finish(),
            Self::VoiceAmplitude(a) => f.debug_tuple("VoiceAmplitude").field(a).finish(),
            Self::VoiceSilenceDetected => write!(f, "VoiceSilenceDetected"),
            Self::OAuthSuccess { provider, .. } => f
                .debug_struct("OAuthSuccess")
                .field("provider", provider)
                .finish(),
            Self::OAuthError { provider, error } => f
                .debug_struct("OAuthError")
                .field("provider", provider)
                .field("error", error)
                .finish(),
            Self::Question(req) => f
                .debug_struct("Question")
                .field("question", &req.question)
                .finish(),
            Self::ToolApproval(req) => f
                .debug_struct("ToolApproval")
                .field("tool", &req.call.name)
                .finish(),
            Self::PlanProposal(req) => f
                .debug_struct("PlanProposal")
                .field("summary", &req.plan.summary)
                .finish(),
            Self::HookResult {
                event, description, ..
            } => f
                .debug_struct("HookResult")
                .field("event", event)
                .field("description", description)
                .finish(),
            Self::ReviewFinished(_) => write!(f, "ReviewFinished"),
            Self::Quit => write!(f, "Quit"),
        }
    }
}

pub fn spawn_event_reader(tx: mpsc::UnboundedSender<AppEvent>) {
    tokio::spawn(async move {
        let mut events = EventStream::new();
        while let Some(next) = events.next().await {
            match next {
                Ok(CEvent::Key(key)) => {
                    let _ = tx.send(AppEvent::Key(key));
                }
                Ok(CEvent::Paste(value)) => {
                    let _ = tx.send(AppEvent::Paste(value));
                }
                Ok(CEvent::Resize(w, h)) => {
                    let _ = tx.send(AppEvent::Resize(w, h));
                }
                Ok(CEvent::Mouse(mouse)) => {
                    // Forward scroll, click, and movement events for modal
                    // hover/click support and message area interactions.
                    match mouse.kind {
                        MouseEventKind::ScrollUp
                        | MouseEventKind::ScrollDown
                        | MouseEventKind::Down(_)
                        | MouseEventKind::Moved
                        | MouseEventKind::Drag(_) => {
                            let _ = tx.send(AppEvent::Mouse(mouse));
                        }
                        _ => {}
                    }
                }
                Ok(_) => {}
                Err(_) => {
                    let _ = tx.send(AppEvent::Quit);
                    break;
                }
            }
        }
    });
}

pub fn spawn_tick_timer(tx: mpsc::UnboundedSender<AppEvent>, is_streaming: Arc<AtomicBool>) {
    tokio::spawn(async move {
        loop {
            let delay = tick_interval(is_streaming.load(Ordering::Relaxed));
            tokio::time::sleep(delay).await;
            if tx.send(AppEvent::Tick).is_err() {
                break;
            }
        }
    });
}

pub fn tick_interval(is_streaming: bool) -> Duration {
    if is_streaming {
        Duration::from_millis(16)
    } else {
        Duration::from_millis(250)
    }
}
