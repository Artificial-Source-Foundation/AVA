use crate::state::agent::TokenUsage;
use ava_agent::AgentEvent;
use crossterm::event::{Event as CEvent, EventStream, KeyEvent, MouseEvent, MouseEventKind};
use futures::StreamExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;

#[derive(Debug)]
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
    Quit,
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
