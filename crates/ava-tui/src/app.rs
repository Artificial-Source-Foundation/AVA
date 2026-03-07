use crate::config::cli::CliArgs;
use crate::config::keybindings::load_keybind_overrides;
use crate::event::{spawn_event_reader, spawn_tick_timer, AppEvent};
use crate::state::agent::AgentState;
use crate::state::input::InputState;
use crate::state::keybinds::{Action, KeybindState};
use crate::state::messages::{MessageKind, MessageState, UiMessage};
use crate::state::permission::PermissionState;
use crate::state::session::SessionState;
use crate::state::theme::Theme;
use crate::ui;
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
}

pub struct App {
    pub state: AppState,
    should_quit: bool,
    pending_goal: Option<String>,
    is_streaming: Arc<AtomicBool>,
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

        let state = AppState {
            theme: Theme::from_name(&cli.theme),
            messages: MessageState::default(),
            input: InputState::default(),
            session,
            permission,
            keybinds,
            agent: AgentState::new(data_dir, cli.provider, cli.model, cli.max_turns, cli.yolo)
                .await?,
            show_sidebar: true,
        };

        Ok(Self {
            state,
            should_quit: false,
            pending_goal: cli.goal,
            is_streaming: Arc::new(AtomicBool::new(false)),
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

        let local = tokio::task::LocalSet::new();
        local
            .run_until(async {
                if let Some(goal) = self.pending_goal.take() {
                    self.submit_goal(goal, app_tx.clone(), agent_tx.clone());
                }

                loop {
                    terminal.draw(|frame| ui::render(frame, &self.state))?;

                    tokio::select! {
                        Some(event) = app_rx.recv() => self.handle_event(event, app_tx.clone(), agent_tx.clone()),
                        Some(agent_event) = agent_rx.recv() => self.handle_event(AppEvent::Agent(agent_event), app_tx.clone(), agent_tx.clone()),
                        else => break,
                    }

                    if self.should_quit {
                        break;
                    }
                }

                Ok::<(), color_eyre::Report>(())
            })
            .await?;

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
            AppEvent::Resize(_, _) | AppEvent::Tick => {}
            AppEvent::Agent(agent_event) => {
                match agent_event {
                    ava_agent::AgentEvent::Token(chunk) => self
                        .state
                        .messages
                        .push(UiMessage::new(MessageKind::Assistant, chunk)),
                    ava_agent::AgentEvent::ToolCall(call) => self.state.messages.push(UiMessage::new(
                        MessageKind::ToolCall,
                        format!("{} {}", call.name, call.arguments),
                    )),
                    ava_agent::AgentEvent::ToolResult(result) => self
                        .state
                        .messages
                        .push(UiMessage::new(MessageKind::ToolResult, result.content)),
                    ava_agent::AgentEvent::Progress(progress) => self
                        .state
                        .messages
                        .push(UiMessage::new(MessageKind::System, progress)),
                    ava_agent::AgentEvent::Complete(_) => {
                        self.is_streaming.store(false, Ordering::Relaxed);
                        self.state.agent.is_running = false;
                    }
                    ava_agent::AgentEvent::Error(err) => self
                        .state
                        .messages
                        .push(UiMessage::new(MessageKind::Error, err)),
                }
            }
            AppEvent::AgentDone(result) => match result {
                Ok(run) => self.finish_run(run),
                Err(err) => {
                    self.is_streaming.store(false, Ordering::Relaxed);
                    self.state.agent.is_running = false;
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
                _ => {}
            }
            return false;
        }

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

    fn submit_goal(
        &mut self,
        goal: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        self.state
            .messages
            .push(UiMessage::new(MessageKind::User, goal.clone()));
        self.is_streaming.store(true, Ordering::Relaxed);
        self.state
            .agent
            .start(goal, self.state.agent.max_turns, app_tx, agent_tx);
    }

    fn finish_run(&mut self, result: ava_agent::stack::AgentRunResult) {
        self.is_streaming.store(false, Ordering::Relaxed);
        self.state.agent.finish(&result);
        self.state.messages.push(UiMessage::new(
            MessageKind::System,
            format!("Run complete: success={}, turns={}", result.success, result.turns),
        ));
    }
}
