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
use crate::ui;
use crate::widgets::command_palette::CommandPaletteState;
use crate::widgets::session_list::SessionListState;
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
    pub active_modal: Option<ModalType>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModalType {
    CommandPalette,
    SessionList,
    ToolApproval,
}

pub struct App {
    pub state: AppState,
    should_quit: bool,
    pending_goal: Option<String>,
    is_streaming: Arc<AtomicBool>,
    token_buffer: TokenBuffer,
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
            active_modal: None,
        };

        Ok(Self {
            state,
            should_quit: false,
            pending_goal: cli.goal,
            is_streaming: Arc::new(AtomicBool::new(false)),
            token_buffer: TokenBuffer::new(60),
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
            }
            AppEvent::Agent(agent_event) => {
                match agent_event {
                    ava_agent::AgentEvent::Token(chunk) => {
                        self.state.agent.activity = AgentActivity::Thinking;
                        self.token_buffer.push(&chunk);
                        // Ensure the streaming indicator is set on the current message
                        if let Some(last) = self.state.messages.messages.last_mut() {
                            if matches!(last.kind, MessageKind::Assistant) {
                                last.is_streaming = true;
                            }
                        }
                    }
                    ava_agent::AgentEvent::ToolCall(call) => {
                        // Force flush any buffered tokens before tool call
                        self.force_flush_token_buffer();
                        // Mark last assistant message as done streaming
                        if let Some(last) = self.state.messages.messages.last_mut() {
                            if matches!(last.kind, MessageKind::Assistant) {
                                last.is_streaming = false;
                            }
                        }
                        self.state.agent.activity = AgentActivity::ExecutingTool(call.name.clone());

                        // Check if we need approval
                        if !self.state.permission.yolo_mode
                            && !self.state.permission.session_approved.contains(&call.name)
                        {
                            let (tx, _rx) = tokio::sync::oneshot::channel();
                            let request = crate::state::permission::ApprovalRequest {
                                call: call.clone(),
                                approve_tx: tx,
                            };
                            self.state.permission.enqueue(request);
                            self.state.active_modal = Some(ModalType::ToolApproval);
                        }
                        self.state.messages.push(UiMessage::new(
                            MessageKind::ToolCall,
                            format!("{} {}", call.name, call.arguments),
                        ));
                    }
                    ava_agent::AgentEvent::ToolResult(result) => {
                        self.state.agent.activity = AgentActivity::Thinking;
                        self.state
                            .messages
                            .push(UiMessage::new(MessageKind::ToolResult, result.content));
                    }
                    ava_agent::AgentEvent::Progress(progress) => {
                        // Parse turn info from progress messages
                        if let Some(turn_str) = progress.strip_prefix("turn ") {
                            if let Ok(turn) = turn_str.trim().parse::<usize>() {
                                self.state.agent.current_turn = turn;
                            }
                        }
                        self.state
                            .messages
                            .push(UiMessage::new(MessageKind::System, progress));
                    }
                    ava_agent::AgentEvent::Complete(_) => {
                        // Force flush any remaining buffered tokens
                        self.force_flush_token_buffer();
                        // Mark last assistant message as done streaming
                        if let Some(last) = self.state.messages.messages.last_mut() {
                            last.is_streaming = false;
                        }
                        self.is_streaming.store(false, Ordering::Relaxed);
                        self.state.agent.is_running = false;
                        self.state.agent.activity = AgentActivity::Idle;
                    }
                    ava_agent::AgentEvent::ToolStats(_) => {}
                    ava_agent::AgentEvent::Error(err) => {
                        // Force flush any remaining buffered tokens
                        self.force_flush_token_buffer();
                        // Mark last assistant message as done streaming
                        if let Some(last) = self.state.messages.messages.last_mut() {
                            last.is_streaming = false;
                        }
                        self.state.agent.activity = AgentActivity::Idle;
                        self.state
                            .messages
                            .push(UiMessage::new(MessageKind::Error, err));
                    }
                }
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
            return self.handle_modal_key(modal, key);
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
                }
                Action::CommandPalette => {
                    self.state.command_palette.open = true;
                    self.state.active_modal = Some(ModalType::CommandPalette);
                }
                Action::NewSession => {
                    let _ = self.state.session.create_session();
                    self.state.messages.messages.clear();
                }
                Action::SessionList => {
                    let _ = self.state.session.list_recent(50);
                    self.state.session_list.open = true;
                    self.state.active_modal = Some(ModalType::SessionList);
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

    fn handle_modal_key(
        &mut self,
        modal: ModalType,
        key: crossterm::event::KeyEvent,
    ) -> bool {
        if key.kind != KeyEventKind::Press {
            return false;
        }

        match modal {
            ModalType::CommandPalette => self.handle_command_palette_key(key),
            ModalType::SessionList => self.handle_session_list_key(key),
            ModalType::ToolApproval => self.handle_tool_approval_key(key),
        }
    }

    fn handle_command_palette_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        match key.code {
            KeyCode::Esc => {
                self.state.command_palette.open = false;
                self.state.active_modal = None;
            }
            KeyCode::Down => {
                let filtered = self.state.command_palette.filtered();
                if !filtered.is_empty() {
                    self.state.command_palette.selected =
                        (self.state.command_palette.selected + 1) % filtered.len();
                }
            }
            KeyCode::Up => {
                self.state.command_palette.selected =
                    self.state.command_palette.selected.saturating_sub(1);
            }
            KeyCode::Enter => {
                let filtered = self.state.command_palette.filtered();
                if let Some(item) = filtered.get(self.state.command_palette.selected) {
                    self.execute_command_action(item.action);
                }
                self.state.command_palette.open = false;
                self.state.active_modal = None;
            }
            KeyCode::Char(ch) => {
                self.state.command_palette.query.push(ch);
                self.state.command_palette.selected = 0;
            }
            KeyCode::Backspace => {
                self.state.command_palette.query.pop();
                self.state.command_palette.selected = 0;
            }
            _ => {}
        }
        false
    }

    fn handle_session_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        match key.code {
            KeyCode::Esc => {
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            KeyCode::Down => {
                let sessions = &self.state.session.sessions;
                if !sessions.is_empty() {
                    self.state.session_list.selected =
                        (self.state.session_list.selected + 1) % sessions.len();
                }
            }
            KeyCode::Up => {
                self.state.session_list.selected =
                    self.state.session_list.selected.saturating_sub(1);
            }
            KeyCode::Enter => {
                if let Some(session) = self.state.session.sessions.get(self.state.session_list.selected) {
                    let session_id = session.id;
                    if self.state.session.switch_to(session_id).is_ok() {
                        // Restore messages from the loaded session
                        self.state.messages.messages.clear();
                        self.state.messages.reset_scroll();
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
                    }
                }
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            KeyCode::Char(ch) => {
                self.state.session_list.query.push(ch);
                self.state.session_list.selected = 0;
            }
            KeyCode::Backspace => {
                self.state.session_list.query.pop();
                self.state.session_list.selected = 0;
            }
            _ => {}
        }
        false
    }

    fn handle_tool_approval_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        use crate::state::permission::ApprovalStage;

        if self.state.permission.queue.is_empty() {
            self.state.active_modal = None;
            return false;
        }

        match self.state.permission.current_stage {
            ApprovalStage::Preview => {
                // Any key moves to action selection
                self.state.permission.current_stage = ApprovalStage::ActionSelect;
            }
            ApprovalStage::ActionSelect => match key.code {
                KeyCode::Char('a') => {
                    self.state.permission.approve_current_once();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                KeyCode::Char('s') => {
                    self.state.permission.approve_current_for_session();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                KeyCode::Char('r') => {
                    self.state.permission.current_stage = ApprovalStage::RejectionReason;
                }
                KeyCode::Char('y') => {
                    self.state.permission.yolo_mode = true;
                    // Approve all pending
                    while !self.state.permission.queue.is_empty() {
                        self.state.permission.approve_current_once();
                    }
                    self.state.active_modal = None;
                }
                KeyCode::Esc => {
                    // Cancel/reject without reason
                    self.state.permission.reject_current();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                _ => {}
            },
            ApprovalStage::RejectionReason => match key.code {
                KeyCode::Enter => {
                    self.state.permission.reject_current();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                KeyCode::Esc => {
                    // Cancel rejection, go back
                    self.state.permission.current_stage = ApprovalStage::ActionSelect;
                    self.state.permission.rejection_input.clear();
                }
                KeyCode::Char(ch) => {
                    self.state.permission.rejection_input.push(ch);
                }
                KeyCode::Backspace => {
                    self.state.permission.rejection_input.pop();
                }
                _ => {}
            },
        }
        false
    }

    fn execute_command_action(&mut self, action: Action) {
        match action {
            Action::NewSession => {
                let _ = self.state.session.create_session();
                self.state.messages.messages.clear();
            }
            Action::SessionList => {
                let _ = self.state.session.list_recent(50);
                self.state.session_list.open = true;
                self.state.active_modal = Some(ModalType::SessionList);
            }
            Action::YoloToggle => {
                self.state.permission.yolo_mode = !self.state.permission.yolo_mode;
            }
            Action::ToggleSidebar => {
                self.state.show_sidebar = !self.state.show_sidebar;
            }
            Action::ScrollUp => self.state.messages.scroll_up(10),
            Action::ScrollDown => self.state.messages.scroll_down(10),
            Action::ScrollTop => self.state.messages.scroll_to_top(),
            Action::ScrollBottom => self.state.messages.scroll_to_bottom(),
            Action::Cancel => {
                if self.state.agent.is_running {
                    self.state.agent.abort();
                }
            }
            _ => {}
        }
    }

    fn submit_goal(
        &mut self,
        goal: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        // Handle slash commands
        if let Some((kind, msg)) = self.handle_slash_command(&goal) {
            self.state
                .messages
                .push(UiMessage::new(MessageKind::User, goal));
            self.state.messages.push(UiMessage::new(kind, msg));
            return;
        }

        self.state
            .messages
            .push(UiMessage::new(MessageKind::User, goal.clone()));
        self.is_streaming.store(true, Ordering::Relaxed);
        self.state.agent.activity = AgentActivity::Thinking;
        self.state
            .agent
            .start(goal, self.state.agent.max_turns, app_tx, agent_tx);
    }

    /// Handle slash commands. Returns Some((kind, message)) if handled, None if not a slash command.
    fn handle_slash_command(&mut self, input: &str) -> Option<(MessageKind, String)> {
        let trimmed = input.trim();
        if !trimmed.starts_with('/') {
            return None;
        }

        let parts: Vec<&str> = trimmed.splitn(2, ' ').collect();
        let cmd = parts[0];
        let arg = parts.get(1).map(|s| s.trim());

        match cmd {
            "/model" => {
                if let Some(model_str) = arg {
                    // Parse "provider/model" format — split on first '/' only
                    if let Some((provider, model)) = model_str.split_once('/') {
                        let provider = provider.to_string();
                        let model = model.to_string();
                        let result = tokio::task::block_in_place(|| {
                            tokio::runtime::Handle::current().block_on(
                                self.state.agent.switch_model(&provider, &model)
                            )
                        });
                        match result {
                            Ok(desc) => Some((MessageKind::System, format!("Switched to {desc}"))),
                            Err(err) => Some((MessageKind::Error, format!("Failed to switch model: {err}"))),
                        }
                    } else {
                        Some((MessageKind::Error,
                            "Invalid format. Use: /model provider/model (e.g., /model openrouter/anthropic/claude-sonnet-4)".to_string()
                        ))
                    }
                } else {
                    let display = self.state.agent.current_model_display();
                    Some((MessageKind::System, format!("Current model: {display}")))
                }
            }
            _ => Some((MessageKind::Error, format!("Unknown command: {cmd}"))),
        }
    }

    /// Flush buffered tokens to the message list (called on tick).
    fn flush_token_buffer(&mut self) {
        if let Some(buffered) = self.token_buffer.flush() {
            self.append_buffered_tokens(buffered);
        }
    }

    /// Force flush all buffered tokens (called on stream end, tool call, error).
    fn force_flush_token_buffer(&mut self) {
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
