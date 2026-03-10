mod commands;
mod event_handler;
mod modals;

use crate::config::cli::CliArgs;
use crate::config::keybindings::load_keybind_overrides;
use crate::event::{spawn_event_reader, spawn_tick_timer, AppEvent};
use crate::state::agent::{AgentActivity, AgentMode, AgentState};
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
use crate::widgets::provider_connect::ProviderConnectState;
use crate::widgets::session_list::SessionListState;
use crate::widgets::select_list::{SelectItem, SelectListState};
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
    pub agent_mode: AgentMode,
    pub show_sidebar: bool,
    pub command_palette: CommandPaletteState,
    pub session_list: SessionListState,
    pub model_selector: Option<ModelSelectorState>,
    pub tool_list: ToolListState,
    pub provider_connect: Option<ProviderConnectState>,
    pub theme_selector: Option<SelectListState<String>>,
    pub active_modal: Option<ModalType>,
    pub status_message: Option<StatusMessage>,
    pub voice: VoiceState,
    pub model_catalog: ava_config::CatalogState,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModalType {
    CommandPalette,
    SessionList,
    ToolApproval,
    ModelSelector,
    ToolList,
    ProviderConnect,
    ThemeSelector,
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
            permission_level: if cli.auto_approve {
                crate::state::permission::PermissionLevel::AutoApprove
            } else {
                crate::state::permission::PermissionLevel::Standard
            },
            ..PermissionState::default()
        };

        let (provider, model) = cli.resolve_provider_model().await?;

        // Load model catalog (cache-first, async fetch)
        let model_catalog = ava_config::CatalogState::load().await;
        // Start background refresh every 60 min
        model_catalog.spawn_background_refresh();

        let state = AppState {
            theme: Theme::from_name(&cli.theme),
            messages: MessageState::default(),
            input: InputState::default(),
            session,
            permission,
            keybinds,
            agent: AgentState::new(data_dir, provider, model, cli.max_turns, cli.auto_approve)
                .await?,
            agent_mode: AgentMode::default(),
            show_sidebar: false,
            command_palette: CommandPaletteState::with_defaults(),
            session_list: SessionListState::default(),
            model_selector: None,
            tool_list: ToolListState::default(),
            provider_connect: None,
            theme_selector: None,
            active_modal: None,
            status_message: None,
            voice: VoiceState {
                auto_submit: cli.voice,
                continuous: cli.voice,
                ..VoiceState::default()
            },
            model_catalog,
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
        execute!(
            stdout(),
            EnterAlternateScreen,
            crossterm::event::EnableBracketedPaste,
            crossterm::event::EnableMouseCapture
        )?;

        // Install panic hook that restores the terminal before printing the panic.
        // Without this, a panic leaves the terminal in raw/alternate-screen mode,
        // making the error unreadable and the shell unusable.
        let original_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |info| {
            let _ = disable_raw_mode();
            let _ = execute!(
                std::io::stdout(),
                LeaveAlternateScreen,
                crossterm::event::DisableBracketedPaste,
                crossterm::event::DisableMouseCapture,
                crossterm::cursor::Show
            );
            original_hook(info);
        }));

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
            // Restore model from session metadata
            if let Some(meta) = session.metadata.as_object() {
                let provider = meta.get("provider").and_then(|v| v.as_str());
                let model = meta.get("model").and_then(|v| v.as_str());
                if let (Some(p), Some(m)) = (provider, model) {
                    let p = p.to_string();
                    let m = m.to_string();
                    let _ = self.state.agent.switch_model(&p, &m).await;
                }
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
            crossterm::event::DisableBracketedPaste,
            crossterm::event::DisableMouseCapture
        )?;
        terminal.show_cursor()?;

        Ok(())
    }

    pub(crate) fn set_status(&mut self, text: impl Into<String>, level: StatusLevel) {
        self.state.status_message = Some(StatusMessage::new(text, level));
    }

    /// Copy the last assistant message content to the system clipboard.
    pub(crate) fn copy_last_response(&mut self) {
        match self.state.messages.last_assistant_content() {
            Some(content) => {
                let content = content.to_owned();
                match arboard::Clipboard::new() {
                    Ok(mut clipboard) => match clipboard.set_text(&content) {
                        Ok(_) => {
                            let preview_len = content.len().min(40);
                            let preview: String = content.chars().take(preview_len).collect();
                            let ellipsis = if content.len() > 40 { "..." } else { "" };
                            self.set_status(
                                format!("Copied to clipboard: \"{preview}{ellipsis}\""),
                                StatusLevel::Info,
                            );
                        }
                        Err(e) => {
                            self.set_status(
                                format!("Clipboard write failed: {e}"),
                                StatusLevel::Error,
                            );
                        }
                    },
                    Err(e) => {
                        self.set_status(
                            format!("Clipboard unavailable: {e}"),
                            StatusLevel::Error,
                        );
                    }
                }
            }
            None => {
                self.set_status("No assistant message to copy", StatusLevel::Warn);
            }
        }
    }

    /// Open the theme selector modal.
    pub(crate) fn open_theme_selector(&mut self) {
        let current = self.state.theme.name;
        let items: Vec<SelectItem<String>> = Theme::all_names()
            .iter()
            .map(|&name| {
                let status = if name == current {
                    Some(crate::widgets::select_list::ItemStatus::Active)
                } else {
                    None
                };
                SelectItem {
                    title: name.to_string(),
                    detail: String::new(),
                    section: None,
                    status,
                    value: name.to_string(),
                    enabled: true,
                }
            })
            .collect();
        self.state.theme_selector = Some(SelectListState::new(items));
        self.state.active_modal = Some(ModalType::ThemeSelector);
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
            AppEvent::Paste(value) => {
                if self.state.active_modal.is_some() {
                    self.handle_modal_paste(&value);
                } else {
                    self.state.input.handle_paste(value);
                }
            }
            AppEvent::Resize(_, _) => {}
            AppEvent::Mouse(mouse) => {
                use crossterm::event::MouseEventKind;
                // Scroll the message list (not input history)
                if self.state.active_modal.is_none() {
                    match mouse.kind {
                        MouseEventKind::ScrollUp => self.state.messages.scroll_up(3),
                        MouseEventKind::ScrollDown => self.state.messages.scroll_down(3),
                        _ => {}
                    }
                }
            }
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
            AppEvent::OAuthSuccess { provider, tokens } => {
                // Store OAuth tokens in credentials
                let result = tokio::task::block_in_place(|| {
                    tokio::runtime::Handle::current().block_on(async {
                        let mut store = ava_config::CredentialStore::load_default()
                            .await
                            .unwrap_or_default();
                        store.set_oauth(
                            &provider,
                            &tokens.access_token,
                            tokens.refresh_token.as_deref(),
                            tokens.expires_at,
                        );
                        store.save_default().await
                    })
                });
                match result {
                    Ok(_) => {
                        self.set_status(
                            format!("Connected to {}", ava_config::provider_name(&provider)),
                            StatusLevel::Info,
                        );
                    }
                    Err(e) => {
                        self.set_status(
                            format!("Failed to save credentials: {e}"),
                            StatusLevel::Error,
                        );
                    }
                }
                self.state.provider_connect = None;
                self.state.active_modal = None;
            }
            AppEvent::OAuthError { provider, error } => {
                self.set_status(
                    format!("{}: {error}", ava_config::provider_name(&provider)),
                    StatusLevel::Error,
                );
                if let Some(ref mut pc) = self.state.provider_connect {
                    pc.screen = crate::widgets::provider_connect::ConnectScreen::List;
                    pc.message = Some(format!("Failed: {error}"));
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
                    // OpenCode-style: Ctrl+C cancels agent → clears input → quits if empty
                    if self.state.agent.is_running {
                        self.state.agent.abort();
                    } else if !self.state.input.buffer.is_empty() {
                        self.state.input.clear();
                    } else {
                        return true;
                    }
                }
                Action::ScrollUp => self.state.messages.scroll_up(10),
                Action::ScrollDown => self.state.messages.scroll_down(10),
                // Home/End: if input has content, move within line; otherwise scroll messages
                Action::ScrollTop => {
                    if !self.state.input.buffer.is_empty() {
                        self.state.input.move_home();
                    } else {
                        self.state.messages.scroll_to_top();
                    }
                }
                Action::ScrollBottom => {
                    if !self.state.input.buffer.is_empty() {
                        self.state.input.move_end();
                    } else {
                        self.state.messages.scroll_to_bottom();
                    }
                }
                Action::ToggleSidebar => self.state.show_sidebar = !self.state.show_sidebar,
                Action::ModeNext => {
                    self.state.agent_mode = self.state.agent_mode.cycle_next();
                    self.state.agent.set_mode(self.state.agent_mode);
                    self.set_status(format!("Mode: {}", self.state.agent_mode.label()), StatusLevel::Info);
                }
                Action::ModePrev => {
                    self.state.agent_mode = self.state.agent_mode.cycle_prev();
                    self.state.agent.set_mode(self.state.agent_mode);
                    self.set_status(format!("Mode: {}", self.state.agent_mode.label()), StatusLevel::Info);
                }
                Action::PermissionToggle => {
                    self.state.permission.permission_level = self.state.permission.permission_level.toggle();
                    self.set_status(
                        format!("Permissions: {}", self.state.permission.permission_level.label()),
                        StatusLevel::Info,
                    );
                }
                Action::CommandPalette => {
                    self.state.command_palette.open = true;
                    self.state.command_palette.list.query.clear();
                    self.state.command_palette.list.selected = 0;
                    self.state.active_modal = Some(ModalType::CommandPalette);
                }
                Action::ModelSwitch => {
                    // Try async catalog path if tokio runtime available,
                    // otherwise open with empty model list (will be populated on next open)
                    if let Ok(handle) = tokio::runtime::Handle::try_current() {
                        let (credentials, catalog) = tokio::task::block_in_place(|| {
                            handle.block_on(async {
                                let creds = ava_config::CredentialStore::load_default()
                                    .await
                                    .unwrap_or_default();
                                let cat = self.state.model_catalog.get().await;
                                (creds, cat)
                            })
                        });
                        let mut effective = if catalog.is_empty() {
                            ava_config::fallback_catalog()
                        } else {
                            catalog
                        };
                        effective.merge_fallback();
                        self.state.model_selector = Some(ModelSelectorState::from_catalog(
                            &effective,
                            &credentials,
                            &self.state.agent.recent_models,
                            &self.state.agent.model_name,
                            &self.state.agent.provider_name,
                        ));
                    } else {
                        self.state.model_selector = Some(ModelSelectorState::default());
                    }
                    self.state.active_modal = Some(ModalType::ModelSelector);
                }
                Action::NewSession => {
                    let _ = self.state.session.create_session();
                    self.state.messages.messages.clear();
                    self.set_status("New session created", StatusLevel::Info);
                }
                Action::SessionList => {
                    self.execute_command_action(Action::SessionList);
                }
                Action::ToggleThinking => {
                    self.state.agent.cycle_thinking();
                }
                Action::VoiceToggle => {
                    #[cfg(feature = "voice")]
                    self.toggle_voice(app_tx);
                }
                Action::CopyLastResponse => {
                    self.copy_last_response();
                }
                _ => {}
            }
            return false;
        }

        // Handle slash menu input when autocomplete is visible
        if self.state.input.has_slash_autocomplete() {
            match key.code {
                KeyCode::Esc => {
                    self.state.input.dismiss_autocomplete();
                    return false;
                }
                KeyCode::Up => {
                    self.state.input.autocomplete_prev();
                    return false;
                }
                KeyCode::Down => {
                    self.state.input.autocomplete_next();
                    return false;
                }
                KeyCode::Tab => {
                    // Tab-complete the command name into the buffer
                    if let Some(value) = self.state.input.autocomplete_selected_value() {
                        let completed = format!("/{}", value);
                        self.state.input.buffer = completed.clone();
                        self.state.input.cursor = completed.len();
                        self.state.input.autocomplete = None;
                    }
                    return false;
                }
                KeyCode::Enter if key.modifiers == KeyModifiers::NONE => {
                    // Execute the selected slash command
                    if let Some(value) = self.state.input.autocomplete_selected_value() {
                        let cmd = format!("/{}", value);
                        self.state.input.clear();
                        if let Some((kind, msg)) = self.handle_slash_command(&cmd) {
                            self.state
                                .messages
                                .push(UiMessage::new(kind, msg));
                        }
                    }
                    return false;
                }
                _ => {
                    // Fall through to normal input handling — typing filters the menu
                }
            }
        }

        // Tab/Shift+Tab cycle agent modes when no autocomplete is active
        if key.code == KeyCode::Tab && key.modifiers == KeyModifiers::NONE {
            self.state.agent_mode = self.state.agent_mode.cycle_next();
            self.state.agent.set_mode(self.state.agent_mode);
            self.set_status(format!("Mode: {}", self.state.agent_mode.label()), StatusLevel::Info);
            return false;
        }
        if key.code == KeyCode::BackTab {
            self.state.agent_mode = self.state.agent_mode.cycle_prev();
            self.state.agent.set_mode(self.state.agent_mode);
            self.set_status(format!("Mode: {}", self.state.agent_mode.label()), StatusLevel::Info);
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
            // Ctrl+O: expand paste placeholder at cursor
            KeyCode::Char('o') if key.modifiers == KeyModifiers::CONTROL => {
                if self.state.input.toggle_paste_expansion() {
                    self.set_status("Paste expanded inline", StatusLevel::Info);
                }
            }
            KeyCode::Char(ch)
                if key.modifiers == KeyModifiers::NONE
                    || key.modifiers == KeyModifiers::SHIFT =>
            {
                self.state.input.insert_char(ch)
            }
            KeyCode::Backspace => self.state.input.delete_backward_with_paste(),
            KeyCode::Delete => self.state.input.delete_forward(),
            KeyCode::Left => self.state.input.move_left(),
            KeyCode::Right => self.state.input.move_right(),
            // Up/Down: navigate within multi-line input first; fall through to history
            KeyCode::Up => {
                if !self.state.input.move_up() {
                    self.state.input.history_up();
                }
            }
            KeyCode::Down => {
                if !self.state.input.move_down() {
                    self.state.input.history_down();
                }
            }
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

    fn finish_run(&mut self, mut result: ava_agent::stack::AgentRunResult) {
        self.is_streaming.store(false, Ordering::Relaxed);
        self.state.agent.finish(&result);

        // Mark last assistant message as done streaming
        if let Some(last) = self.state.messages.messages.last_mut() {
            if matches!(last.kind, MessageKind::Assistant) {
                last.is_streaming = false;
                if last.model_name.is_none() {
                    last.model_name = Some(self.state.agent.model_name.clone());
                }
            }
        }

        // Store model info in session metadata before saving
        if let Some(meta) = result.session.metadata.as_object_mut() {
            meta.insert(
                "provider".to_string(),
                serde_json::Value::String(self.state.agent.provider_name.clone()),
            );
            meta.insert(
                "model".to_string(),
                serde_json::Value::String(self.state.agent.model_name.clone()),
            );
        }

        // Save session to SQLite
        self.state.session.save_session(&result.session);
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
            agent_mode: AgentMode::default(),
            show_sidebar: false,
            command_palette: CommandPaletteState::with_defaults(),
            session_list: SessionListState::default(),
            model_selector: None,
            tool_list: ToolListState::default(),
            provider_connect: None,
            theme_selector: None,
            active_modal: None,
            status_message: None,
            voice: VoiceState::default(),
            model_catalog: ava_config::CatalogState::default(),
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
