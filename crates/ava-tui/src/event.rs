use crate::state::agent::TokenUsage;
use ava_agent::AgentEvent;
use crossterm::event::{Event as CEvent, EventStream, KeyEvent, MouseEvent, MouseEventKind};
use futures::StreamExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;

pub enum AppEvent {
    Key(KeyEvent),
    Paste(String),
    Resize(u16, u16),
    Mouse(MouseEvent),
    Tick,
    Agent(AgentEvent),
    AgentDone(Result<ava_agent::stack::AgentRunResult, String>),
    TokenUsage(TokenUsage),
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
    OAuthSuccess { provider: String, tokens: ava_auth::tokens::OAuthTokens },
    /// OAuth flow failed.
    OAuthError { provider: String, error: String },
    /// Agent is asking the user a question via the question tool.
    Question(ava_tools::core::question::QuestionRequest),
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
            Self::Agent(e) => f.debug_tuple("Agent").field(e).finish(),
            Self::AgentDone(r) => f.debug_tuple("AgentDone").field(r).finish(),
            Self::TokenUsage(u) => f.debug_tuple("TokenUsage").field(u).finish(),
            Self::ShellResult(k, c) => f.debug_tuple("ShellResult").field(k).field(c).finish(),
            Self::VoiceReady(t) => f.debug_tuple("VoiceReady").field(t).finish(),
            Self::VoiceError(e) => f.debug_tuple("VoiceError").field(e).finish(),
            Self::VoiceAmplitude(a) => f.debug_tuple("VoiceAmplitude").field(a).finish(),
            Self::VoiceSilenceDetected => write!(f, "VoiceSilenceDetected"),
            Self::OAuthSuccess { provider, .. } => {
                f.debug_struct("OAuthSuccess").field("provider", provider).finish()
            }
            Self::OAuthError { provider, error } => {
                f.debug_struct("OAuthError").field("provider", provider).field("error", error).finish()
            }
            Self::Question(req) => {
                f.debug_struct("Question").field("question", &req.question).finish()
            }
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
                    // Only forward scroll events (not movement, clicks, etc.)
                    if matches!(
                        mouse.kind,
                        MouseEventKind::ScrollUp | MouseEventKind::ScrollDown
                    ) {
                        let _ = tx.send(AppEvent::Mouse(mouse));
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
