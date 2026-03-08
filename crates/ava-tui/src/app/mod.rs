mod commands;
mod event_handler;
mod modals;

use crate::config::cli::CliArgs;
use crate::config::keybindings::load_keybind_overrides;
use crate::event::{spawn_event_reader, spawn_tick_timer, AppEvent};
use crate::state::agent::{AgentActivity, AgentState};
use crate::state::input::InputState;
use crate::state::keybinds::{Action, KeybindState};
use crate::state::messages::{MessageKind, MessageState, UiMessage};
use crate::state::permission::PermissionState;
use crate::state::session::SessionState;
use crate::state::theme::Theme;
use crate::state::voice::{VoicePhase, VoiceState};
use crate::ui;
use crate::ui::status_bar::{StatusLevel, StatusMessage};
use crate::widgets::command_palette::CommandPaletteState;
use crate::widgets::model_selector::ModelSelectorState;
use crate::widgets::session_list::SessionListState;
use crate::widgets::tool_list::ToolListState;
use crate::widgets::token_buffer::TokenBuffer;
use color_eyre::eyre::Result;
use crossterm::event::{KeyCode, KeyEventKind, KeyModifiers};
use crossterm::execute;
use crossterm::terminal::{
    disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen,
};
use ratatui::backend::CrosstermBackend;
use ratatui::Terminal;
use std::io::stdout;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use tokio::sync::mpsc;

pub struct AppState {
    pub theme: Theme,
    pub messages: MessageState,
    pub input: InputState,
    pub session: SessionState,
    pub permission: PermissionState,
    pub keybinds: KeybindState,
    pub agent: AgentState,
    pub show_sidebar: bool,
    pub command_palette: CommandPaletteState,
    pub session_list: SessionListState,
    pub model_selector: Option<ModelSelectorState>,
    pub tool_list: ToolListState,
    pub active_modal: Option<ModalType>,
    pub status_message: Option<StatusMessage>,
    pub voice: VoiceState,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModalType {
    CommandPalette,
    SessionList,
    ToolApproval,
    ModelSelector,
    ToolList,
}

pub struct App {
    pub state: AppState,
    should_quit: bool,
    pending_goal: Option<String>,
    is_streaming: Arc<AtomicBool>,
    token_buffer: TokenBuffer,
    #[cfg(feature = "voice")]
    audio_recorder: Option<crate::audio::AudioRecorder>,
    #[cfg(feature = "voice")]
    transcriber: Option<Box<dyn crate::transcribe::Transcriber>>,
    #[cfg(feature = "voice")]
    voice_config: ava_config::VoiceConfig,
}

impl App {
    pub async fn new(cli: CliArgs) -> Result<Self> {
        let data_dir = dirs::home_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join(".ava");
        std::fs::create_dir_all(&data_dir)?;
        let db_path = data_dir.join("data.db");

        let mut keybinds = KeybindState::default();
        if let Ok(overrides) = load_keybind_overrides() {
            keybinds.merge_overrides(overrides);
        }

        let mut session = SessionState::new(&db_path)?;
        if cli.resume {
            session.current_session = session.list_recent(1)?.first().cloned();
        } else {
            let _ = session.create_session()?;
        }

        let permission = PermissionState {
            yolo_mode: cli.yolo,
            ..PermissionState::default()
        };

        let (provider, model) = cli.resolve_provider_model().await?;

        let state = AppState {
            theme: Theme::from_name(&cli.theme),
            messages: MessageState::default(),
            input: InputState::default(),
            session,
            permission,
            keybinds,
            agent: AgentState::new(data_dir, provider, model, cli.max_turns, cli.yolo)
                .await?,
            show_sidebar: true,
            command_palette: CommandPaletteState::with_defaults(),
            session_list: SessionListState::default(),
            model_selector: None,
            tool_list: ToolListState::default(),
            active_modal: None,
            status_message: None,
            voice: VoiceState {
                auto_submit: cli.voice,
                continuous: cli.voice,
                ..VoiceState::default()
            },
        };

        Ok(Self {
            state,
            should_quit: false,
            pending_goal: cli.goal.clone(),
            is_streaming: Arc::new(AtomicBool::new(false)),
            token_buffer: TokenBuffer::new(60),
            #[cfg(feature = "voice")]
            audio_recorder: None,
            #[cfg(feature = "voice")]
            transcriber: None,
            #[cfg(feature = "voice")]
            voice_config: ava_config::VoiceConfig::default(),
        })
    }

    pub async fn run(&mut self) -> Result<()> {
        enable_raw_mode()?;
        execute!(stdout(), EnterAlternateScreen, crossterm::event::EnableBracketedPaste)?;

        let backend = CrosstermBackend::new(stdout());
        let mut terminal = Terminal::new(backend)?;
        terminal.clear()?;

        let (app_tx, mut app_rx) = mpsc::unbounded_channel();
        let (agent_tx, mut agent_rx) = mpsc::unbounded_channel();

        spawn_event_reader(app_tx.clone());
        spawn_tick_timer(app_tx.clone(), Arc::clone(&self.is_streaming));

        // Load session messages if resuming
        if let Some(ref session) = self.state.session.current_session {
            for msg in &session.messages {
                let kind = match msg.role {
                    ava_types::Role::User => MessageKind::User,
                    ava_types::Role::Assistant => MessageKind::Assistant,
                    ava_types::Role::Tool => MessageKind::ToolResult,
                    ava_types::Role::System => MessageKind::System,
                };
                self.state.messages.push(UiMessage::new(kind, msg.content.clone()));
            }
        }

        if let Some(goal) = self.pending_goal.take() {
            self.submit_goal(goal, app_tx.clone(), agent_tx.clone());
        }

        loop {
            terminal.draw(|frame| ui::render(frame, &mut self.state))?;

            tokio::select! {
                Some(event) = app_rx.recv() => self.handle_event(event, app_tx.clone(), agent_tx.clone()),
                Some(agent_event) = agent_rx.recv() => self.handle_event(AppEvent::Agent(agent_event), app_tx.clone(), agent_tx.clone()),
                else => break,
            }

            if self.should_quit {
                break;
            }
        }

        disable_raw_mode()?;
        execute!(
            terminal.backend_mut(),
            LeaveAlternateScreen,
            crossterm::event::DisableBracketedPaste
        )?;
        terminal.show_cursor()?;

        Ok(())
    }

    pub(crate) fn set_status(&mut self, text: impl Into<String>, level: StatusLevel) {
        self.state.status_message = Some(StatusMessage::new(text, level));
    }

    fn handle_event(
        &mut self,
        event: AppEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        match event {
            AppEvent::Quit => self.should_quit = true,
            AppEvent::Key(key) if key.kind == KeyEventKind::Press => {
                if self.handle_key(key, app_tx, agent_tx) {
                    self.should_quit = true;
                }
            }
            AppEvent::Key(_) => {}
            AppEvent::Paste(value) => self.state.input.insert_str(&value),
            AppEvent::Resize(_, _) => {}
            AppEvent::Tick => {
                self.flush_token_buffer();
                // Expire TTL status messages
                if let Some(ref msg) = self.state.status_message {
                    if msg.is_expired() {
                        self.state.status_message = None;
                    }
                }
            }
            AppEvent::Agent(agent_event) => {
                self.handle_agent_event(agent_event, app_tx, agent_tx);
            }
            AppEvent::AgentDone(result) => match result {
                Ok(run) => self.finish_run(run),
                Err(err) => {
                    self.is_streaming.store(false, Ordering::Relaxed);
                    self.state.agent.is_running = false;
                    self.state.agent.activity = AgentActivity::Idle;
                    self.state
                        .messages
                        .push(UiMessage::new(MessageKind::Error, err));
                }
            },
            AppEvent::TokenUsage(usage) => {
                self.state.agent.tokens_used = usage;
            }
            AppEvent::ShellResult(kind, content) => {
                self.state.messages.push(UiMessage::new(kind, content));
            }
            AppEvent::VoiceReady(text) => {
                self.state.voice.phase = VoicePhase::Idle;
                self.state.voice.recording_start = None;
                self.state.voice.amplitude = 0.0;
                if !text.trim().is_empty() {
                    self.state.input.insert_str(text.trim());
                    if self.state.voice.auto_submit {
                        if let Some(goal) = self.state.input.submit() {
                            self.submit_goal(goal, app_tx, agent_tx);
                        }
                    }
                }
            }
            AppEvent::VoiceError(err) => {
                self.state.voice.phase = VoicePhase::Idle;
                self.state.voice.recording_start = None;
                self.state.voice.amplitude = 0.0;
                self.state.voice.error = Some(err.clone());
                self.set_status(format!("Voice: {err}"), StatusLevel::Error);
            }
            AppEvent::VoiceAmplitude(amp) => {
                self.state.voice.amplitude = amp;
            }
            AppEvent::VoiceSilenceDetected => {
                #[cfg(feature = "voice")]
                if self.state.voice.phase == VoicePhase::Recording {
                    self.stop_and_transcribe(app_tx);
                }
            }
        }
    }

    fn handle_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) -> bool {
        // Handle modal-specific input first
        if let Some(modal) = self.state.active_modal {
            return self.handle_modal_key(modal, key, app_tx);
        }

        // Handle global keybindings
        if let Some(action) = self.state.keybinds.action_for(key) {
            match action {
                Action::Quit => return true,
                Action::Cancel => {
                    if self.state.agent.is_running {
                        self.state.agent.abort();
                    } else {
                        self.state.input.clear();
                    }
                }
                Action::ScrollUp => self.state.messages.scroll_up(10),
                Action::ScrollDown => self.state.messages.scroll_down(10),
                Action::ScrollTop => self.state.messages.scroll_to_top(),
                Action::ScrollBottom => self.state.messages.scroll_to_bottom(),
                Action::ToggleSidebar => self.state.show_sidebar = !self.state.show_sidebar,
                Action::YoloToggle => {
                    self.state.permission.yolo_mode = !self.state.permission.yolo_mode;
                    let msg = if self.state.permission.yolo_mode {
                        "YOLO mode enabled"
                    } else {
                        "YOLO mode disabled"
                    };
                    self.set_status(msg, StatusLevel::Info);
                }
                Action::CommandPalette => {
                    self.state.command_palette.open = true;
                    self.state.command_palette.query.clear();
                    self.state.command_palette.selected = 0;
                    self.state.active_modal = Some(ModalType::CommandPalette);
                }
                Action::ModelSwitch => {
                    self.state.model_selector = Some(ModelSelectorState::default());
                    self.state.active_modal = Some(ModalType::ModelSelector);
                }
                Action::NewSession => {
                    let _ = self.state.session.create_session();
                    self.state.messages.messages.clear();
                    self.set_status("New session created", StatusLevel::Info);
                }
                Action::SessionList => {
                    let _ = self.state.session.list_recent(50);
                    self.state.session_list.open = true;
                    self.state.active_modal = Some(ModalType::SessionList);
                }
                Action::VoiceToggle => {
                    #[cfg(feature = "voice")]
                    self.toggle_voice(app_tx);
                }
                _ => {}
            }
            return false;
        }

        // Handle normal input
        match key.code {
            KeyCode::Enter if key.modifiers == KeyModifiers::NONE => {
                if let Some(goal) = self.state.input.submit() {
                    self.submit_goal(goal, app_tx, agent_tx);
                }
            }
            KeyCode::Enter => self.state.input.insert_char('\n'),
            KeyCode::Char(ch)
                if key.modifiers == KeyModifiers::NONE
                    || key.modifiers == KeyModifiers::SHIFT =>
            {
                self.state.input.insert_char(ch)
            }
            KeyCode::Backspace => self.state.input.delete_backward(),
            KeyCode::Left => self.state.input.move_left(),
            KeyCode::Right => self.state.input.move_right(),
            KeyCode::Up => self.state.input.history_up(),
            KeyCode::Down => self.state.input.history_down(),
            _ => {}
        }

        false
    }

    /// Flush buffered tokens to the message list (called on tick).
    fn flush_token_buffer(&mut self) {
        if let Some(buffered) = self.token_buffer.flush() {
            self.append_buffered_tokens(buffered);
        }
    }

    /// Force flush all buffered tokens (called on stream end, tool call, error).
    pub(crate) fn force_flush_token_buffer(&mut self) {
        if let Some(buffered) = self.token_buffer.force_flush() {
            self.append_buffered_tokens(buffered);
        }
    }

    /// Append buffered token content to the last assistant message or create a new one.
    fn append_buffered_tokens(&mut self, content: String) {
        if let Some(last) = self.state.messages.messages.last_mut() {
            if matches!(last.kind, MessageKind::Assistant) {
                last.content.push_str(&content);
                last.is_streaming = true;
            } else {
                let mut msg = UiMessage::new(MessageKind::Assistant, content);
                msg.is_streaming = true;
                self.state.messages.push(msg);
            }
        } else {
            let mut msg = UiMessage::new(MessageKind::Assistant, content);
            msg.is_streaming = true;
            self.state.messages.push(msg);
        }
        if self.state.messages.auto_scroll {
            self.state.messages.scroll_to_bottom();
        }
    }

    fn finish_run(&mut self, result: ava_agent::stack::AgentRunResult) {
        self.is_streaming.store(false, Ordering::Relaxed);
        self.state.agent.finish(&result);

        // Save session to SQLite
        self.state.session.save_session(&result.session);

        self.state.messages.push(UiMessage::new(
            MessageKind::System,
            format!(
                "Complete — {} turns, {}",
                result.turns,
                if result.success { "success" } else { "failed" }
            ),
        ));
    }
}

#[doc(hidden)]
impl App {
    /// Create a lightweight `App` for testing — no AgentStack, no real terminal.
    pub fn test_new(db_path: &std::path::Path) -> Self {
        let session = SessionState::new(db_path).expect("SessionState");

        let state = AppState {
            theme: Theme::default_theme(),
            messages: MessageState::default(),
            input: InputState::default(),
            session,
            permission: PermissionState::default(),
            keybinds: KeybindState::default(),
            agent: AgentState::test_new("test-provider", "test-model"),
            show_sidebar: true,
            command_palette: CommandPaletteState::with_defaults(),
            session_list: SessionListState::default(),
            model_selector: None,
            tool_list: ToolListState::default(),
            active_modal: None,
            status_message: None,
            voice: VoiceState::default(),
        };

        Self {
            state,
            should_quit: false,
            pending_goal: None,
            is_streaming: Arc::new(AtomicBool::new(false)),
            token_buffer: TokenBuffer::new(60),
            #[cfg(feature = "voice")]
            audio_recorder: None,
            #[cfg(feature = "voice")]
            transcriber: None,
            #[cfg(feature = "voice")]
            voice_config: ava_config::VoiceConfig::default(),
        }
    }

    /// Send a key event through `handle_key` for testing.
    pub fn process_key_for_test(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let (app_tx, _) = mpsc::unbounded_channel();
        let (agent_tx, _) = mpsc::unbounded_channel();
        self.handle_key(key, app_tx, agent_tx)
    }
}
