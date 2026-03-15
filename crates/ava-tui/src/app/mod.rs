mod actions;
mod background;
mod command_support;
mod commands;
mod event_dispatch;
mod event_handler;
mod exporting;
mod git_commit;
mod input_handling;
mod modals;
mod praxis;
mod spawners;

use crate::config::cli::CliArgs;
use crate::config::keybindings::load_keybind_overrides;
use crate::event::{spawn_event_reader, spawn_tick_timer, AppEvent};
use crate::hooks::{HookContext, HookEvent, HookRegistry, HookResult, HookRunner};
use crate::state::agent::{AgentActivity, AgentMode, AgentState};
use crate::state::background::{new_shared, SharedBackgroundState};
use crate::state::btw::BtwState;
use crate::state::custom_commands::CustomCommandRegistry;
use crate::state::input::InputState;
use crate::state::keybinds::{Action, KeybindState};
use crate::state::messages::{MessageKind, MessageState, UiMessage};
use crate::state::permission::PermissionState;
use crate::state::praxis::PraxisState;
use crate::state::rewind::RewindState;
use crate::state::session::SessionState;
use crate::state::theme::Theme;
use crate::state::voice::{VoicePhase, VoiceState};
use crate::ui;
use crate::ui::status_bar::{StatusLevel, StatusMessage};
use crate::widgets::command_palette::CommandPaletteState;
use crate::widgets::diff_preview::DiffPreviewState;
use crate::widgets::model_selector::ModelSelectorState;
use crate::widgets::provider_connect::ProviderConnectState;
use crate::widgets::select_list::{SelectItem, SelectListState};
use crate::widgets::session_list::SessionListState;
use crate::widgets::token_buffer::TokenBuffer;
use crate::widgets::tool_list::ToolListState;
use color_eyre::eyre::Result;
use crossterm::event::{KeyCode, KeyEventKind, KeyModifiers};
use crossterm::execute;
use crossterm::terminal::{
    disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen,
};
use ratatui::backend::CrosstermBackend;
use ratatui::Terminal;
use std::collections::HashMap;
use std::io::stdout;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};
use tokio::sync::mpsc;
use tracing::debug;

#[derive(Debug, Clone)]
pub(crate) struct PendingBackgroundGoal {
    pub goal: String,
    pub isolated_branch: bool,
}

#[derive(Debug, Clone)]
pub(crate) struct PendingPraxisGoal {
    pub goal: String,
}

#[derive(Debug, Clone)]
struct BackgroundIsolation {
    worktree_path: PathBuf,
    branch_name: String,
}

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
    pub agent_list: Option<SelectListState<String>>,
    /// Saved theme before opening theme selector (for live preview revert on Esc).
    pub theme_before_preview: Option<Theme>,
    pub active_modal: Option<ModalType>,
    pub view_mode: ViewMode,
    pub status_message: Option<StatusMessage>,
    pub voice: VoiceState,
    pub model_catalog: ava_config::CatalogState,
    /// Cached snapshot of the agent's todo list for sidebar rendering.
    pub todo_items: Vec<ava_types::TodoItem>,
    /// Shared todo state handle (for async refresh).
    pub todo_state: Option<ava_types::TodoState>,
    /// Active question from the agent (question tool).
    pub question: Option<QuestionState>,
    /// Active copy picker state (shown when multiple code blocks in last response).
    pub copy_picker: Option<CopyPickerState>,
    /// State for the /btw side-conversation overlay.
    pub btw: BtwState,
    /// Registry of user-defined slash commands from TOML files.
    pub custom_commands: CustomCommandRegistry,
    /// State for the diff preview modal (per-hunk accept/reject).
    pub diff_preview: Option<DiffPreviewState>,
    /// State for the rewind system (checkpoints + modal).
    pub rewind: RewindState,
    /// Shared background task state.
    pub background: SharedBackgroundState,
    /// Praxis multi-agent task state.
    pub praxis: PraxisState,
    /// Registry of user-defined lifecycle hooks.
    pub hooks: HookRegistry,
}

/// A fenced code block extracted from markdown content.
#[derive(Debug, Clone)]
pub struct CodeBlock {
    /// Language tag (e.g., "rust", "bash"), or empty string if none.
    pub language: String,
    /// The code content (without the fence lines).
    pub content: String,
    /// Approximate starting line number in the original message.
    pub start_line: usize,
    /// Approximate ending line number in the original message.
    pub end_line: usize,
}

/// State for the code block copy picker modal.
#[derive(Debug, Clone)]
pub struct CopyPickerState {
    /// The extracted code blocks.
    pub blocks: Vec<CodeBlock>,
    /// The full response content (for "copy all").
    pub full_content: String,
}

/// State for the question modal shown when the agent uses the question tool.
pub struct QuestionState {
    /// The question text from the agent.
    pub question: String,
    /// Optional selectable choices.
    pub options: Vec<String>,
    /// Index of the currently selected option (when options are present).
    pub selected: usize,
    /// Free-text input buffer (when no options are present).
    pub input: String,
    /// Channel to send the user's answer back to the tool.
    pub reply: Option<tokio::sync::oneshot::Sender<String>>,
}

/// Determines which conversation is displayed in the message list.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub enum ViewMode {
    /// Show the main conversation.
    #[default]
    Main,
    /// Show a sub-agent's conversation.
    SubAgent {
        /// Index into `AgentState::sub_agents`.
        agent_index: usize,
        /// The sub-agent's description for the header breadcrumb.
        description: String,
    },
    /// View a background task's output (read-only).
    BackgroundTask {
        /// The background task ID.
        task_id: usize,
        /// Goal description for the header breadcrumb.
        goal: String,
    },
    /// View a Praxis task's output and worker state.
    PraxisTask { task_id: usize, goal: String },
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
    AgentList,
    Question,
    CopyPicker,
    Rewind,
    TaskList,
    DiffPreview,
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
    /// Receiver for question requests from the agent's question tool.
    question_rx:
        Option<tokio::sync::mpsc::UnboundedReceiver<ava_tools::core::question::QuestionRequest>>,
    /// Receiver for interactive tool approval requests.
    approval_rx: Option<
        tokio::sync::mpsc::UnboundedReceiver<ava_tools::permission_middleware::ApprovalRequest>,
    >,
    /// Timestamp of the last Esc press, for double-Esc detection.
    last_esc_time: Option<std::time::Instant>,
    /// Pending background goal from `/bg` command (consumed in submit_goal).
    pub(crate) pending_bg_goal: Option<PendingBackgroundGoal>,
    /// Pending Praxis goal from `/praxis` command (consumed in submit_goal).
    pub(crate) pending_praxis_goal: Option<PendingPraxisGoal>,
    /// Images pending attachment to the next user message.
    pub(crate) pending_images: Vec<ava_types::ImageContent>,
    next_run_id: u64,
    foreground_run_id: Option<u64>,
    background_run_routes: HashMap<u64, usize>,
    data_dir: PathBuf,
}

// StatusSummary removed — /status command was removed

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

        let (agent, question_rx, approval_rx) = AgentState::new(
            data_dir.clone(),
            provider,
            model,
            cli.max_turns,
            cli.max_budget_usd,
            cli.auto_approve,
        )
        .await?;
        let todo_state = agent.todo_state();

        let state = AppState {
            theme: Theme::from_name(&cli.theme),
            messages: MessageState::default(),
            input: InputState::default(),
            session,
            permission,
            keybinds,
            agent,
            agent_mode: AgentMode::default(),
            show_sidebar: false,
            command_palette: CommandPaletteState::with_defaults(),
            session_list: SessionListState::default(),
            model_selector: None,
            tool_list: ToolListState::default(),
            provider_connect: None,
            theme_selector: None,
            agent_list: None,
            theme_before_preview: None,
            active_modal: None,
            view_mode: ViewMode::default(),
            status_message: None,
            voice: VoiceState {
                auto_submit: cli.voice,
                continuous: cli.voice,
                ..VoiceState::default()
            },
            model_catalog,
            todo_items: Vec::new(),
            todo_state,
            question: None,
            copy_picker: None,
            btw: BtwState::default(),
            custom_commands: CustomCommandRegistry::load(),
            diff_preview: None,
            rewind: RewindState::default(),
            background: new_shared(),
            praxis: PraxisState::default(),
            hooks: HookRegistry::load(),
        };

        let mut app = Self {
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
            question_rx: Some(question_rx),
            approval_rx: Some(approval_rx),
            last_esc_time: None,
            pending_bg_goal: None,
            pending_praxis_goal: None,
            pending_images: Vec::new(),
            next_run_id: 1,
            foreground_run_id: None,
            background_run_routes: HashMap::new(),
            data_dir,
        };
        app.sync_custom_command_autocomplete();
        Ok(app)
    }

    pub async fn run(&mut self) -> Result<()> {
        enable_raw_mode()?;
        execute!(
            stdout(),
            EnterAlternateScreen,
            crossterm::event::EnableBracketedPaste,
            crossterm::event::EnableMouseCapture,
            crossterm::event::PushKeyboardEnhancementFlags(
                crossterm::event::KeyboardEnhancementFlags::REPORT_EVENT_TYPES
                    | crossterm::event::KeyboardEnhancementFlags::DISAMBIGUATE_ESCAPE_CODES
            )
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
                crossterm::event::PopKeyboardEnhancementFlags,
                crossterm::cursor::Show
            );
            original_hook(info);
        }));

        let backend = CrosstermBackend::new(stdout());
        let mut terminal = Terminal::new(backend)?;
        terminal.clear()?;

        let (app_tx, mut app_rx) = mpsc::unbounded_channel();
        let (agent_tx, _agent_rx) = mpsc::unbounded_channel();

        spawn_event_reader(app_tx.clone());
        spawn_tick_timer(app_tx.clone(), Arc::clone(&self.is_streaming));

        // Load session messages if resuming
        if let Some(ref session) = self.state.session.current_session {
            self.state.agent.apply_session_summary(session);
            for msg in &session.messages {
                let kind = match msg.role {
                    ava_types::Role::User => MessageKind::User,
                    ava_types::Role::Assistant => MessageKind::Assistant,
                    ava_types::Role::Tool => MessageKind::ToolResult,
                    ava_types::Role::System => MessageKind::System,
                };
                self.state
                    .messages
                    .push(UiMessage::new(kind, msg.content.clone()));
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

        // Take the question receiver so we can poll it in the event loop
        let mut question_rx = self.question_rx.take();
        let mut approval_rx = self.approval_rx.take();

        if let Some(goal) = self.pending_goal.take() {
            self.submit_goal(goal, app_tx.clone(), agent_tx.clone());
        }

        loop {
            let draw_start = std::time::Instant::now();
            terminal.draw(|frame| ui::render(frame, &mut self.state))?;
            let draw_elapsed = draw_start.elapsed();
            if draw_elapsed.as_millis() > 32 {
                // Only log when a frame exceeds ~2× the 60fps budget (16ms)
                tracing::warn!(
                    duration_ms = draw_elapsed.as_millis() as u64,
                    pending_tokens = self.token_buffer.pending_len(),
                    "slow frame render"
                );
            } else {
                tracing::trace!(
                    duration_ms = draw_elapsed.as_millis() as u64,
                    "frame render"
                );
            }

            tokio::select! {
                Some(event) = app_rx.recv() => self.handle_event(event, app_tx.clone(), agent_tx.clone()),
                Some(req) = async { match question_rx.as_mut() { Some(rx) => rx.recv().await, None => None } } => {
                    self.handle_event(AppEvent::Question(req), app_tx.clone(), agent_tx.clone());
                },
                Some(req) = async { match approval_rx.as_mut() { Some(rx) => rx.recv().await, None => None } } => {
                    self.handle_event(AppEvent::ToolApproval(req), app_tx.clone(), agent_tx.clone());
                },
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
            crossterm::event::DisableMouseCapture,
            crossterm::event::PopKeyboardEnhancementFlags
        )?;
        terminal.show_cursor()?;

        Ok(())
    }

    pub(crate) fn set_status(&mut self, text: impl Into<String>, level: StatusLevel) {
        self.state.status_message = Some(StatusMessage::new(text, level));
    }

    pub(crate) fn allocate_run_id(&mut self) -> u64 {
        let run_id = self.next_run_id;
        self.next_run_id += 1;
        run_id
    }

    pub(crate) fn route_agent_event(
        &mut self,
        run_id: u64,
        agent_event: ava_agent::AgentEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        if self.foreground_run_id == Some(run_id) {
            self.handle_agent_event(agent_event, app_tx, agent_tx);
            return;
        }

        if let Some(task_id) = self.background_run_routes.get(&run_id).copied() {
            self.handle_background_agent_event(task_id, agent_event);
        }
    }

    pub(crate) fn finish_routed_run(
        &mut self,
        run_id: u64,
        result: std::result::Result<ava_agent::stack::AgentRunResult, String>,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        if self.foreground_run_id == Some(run_id) {
            self.foreground_run_id = None;
            match result {
                Ok(run) => self.finish_run(run),
                Err(err) => {
                    self.is_streaming.store(false, Ordering::Relaxed);
                    self.state.agent.is_running = false;
                    self.state.agent.activity = AgentActivity::Idle;
                    self.state
                        .messages
                        .push(UiMessage::new(MessageKind::Error, err));
                }
            }
            return;
        }

        let Some(task_id) = self.background_run_routes.remove(&run_id) else {
            return;
        };

        let (isolation_suffix, isolation_target) = {
            let bg = self.state.background.lock().unwrap();
            bg.tasks
                .iter()
                .find(|task| task.id == task_id)
                .and_then(|task| {
                    task.branch_name
                        .as_ref()
                        .zip(task.worktree_path.as_ref())
                        .map(|(branch, path)| {
                            (
                                format!(" [{branch} @ {path}]"),
                                Some((branch.clone(), path.clone())),
                            )
                        })
                })
                .unwrap_or_else(|| (String::new(), None))
        };

        match result {
            Ok(_) => {
                self.state.background.lock().unwrap().complete_task(task_id);

                if let Some((branch_name, worktree_path)) = isolation_target {
                    Self::spawn_background_worktree_cleanup(
                        task_id,
                        branch_name,
                        worktree_path,
                        app_tx,
                    );
                }

                self.set_status(
                    format!("Background task #{task_id} completed{isolation_suffix}"),
                    StatusLevel::Info,
                );
            }
            Err(err) => {
                self.state
                    .background
                    .lock()
                    .unwrap()
                    .fail_task(task_id, err.clone());

                if let Some((branch_name, worktree_path)) = isolation_target {
                    Self::spawn_background_worktree_cleanup(
                        task_id,
                        branch_name,
                        worktree_path,
                        app_tx,
                    );
                }

                self.set_status(
                    format!("Background task #{task_id} failed{isolation_suffix}"),
                    StatusLevel::Error,
                );
            }
        }
    }

    /// Switch to viewing a sub-agent's conversation by its index in
    /// `agent.sub_agents`. Returns `true` if the switch succeeded.
    pub(crate) fn enter_sub_agent_view(&mut self, index: usize) -> bool {
        if let Some(sa) = self.state.agent.sub_agents.get(index) {
            if sa.session_messages.is_empty() {
                self.set_status("Sub-agent has no messages yet", StatusLevel::Warn);
                return false;
            }
            self.state.view_mode = ViewMode::SubAgent {
                agent_index: index,
                description: sa.description.clone(),
            };
            self.state.messages.reset_scroll();
            true
        } else {
            false
        }
    }

    /// Open the theme selector modal with live preview.
    pub(crate) fn open_theme_selector(&mut self) {
        let current = &self.state.theme.name;
        let items: Vec<SelectItem<String>> = Theme::all_names()
            .into_iter()
            .map(|name| {
                let status = if &name == current {
                    Some(crate::widgets::select_list::ItemStatus::Active)
                } else {
                    None
                };
                SelectItem {
                    title: name.clone(),
                    detail: String::new(),
                    section: None,
                    status,
                    value: name,
                    enabled: true,
                }
            })
            .collect();
        // Save current theme so we can revert on Esc
        self.state.theme_before_preview = Some(self.state.theme.clone());
        self.state.theme_selector = Some(SelectListState::new(items));
        self.state.active_modal = Some(ModalType::ThemeSelector);
    }

    /// Flush buffered tokens to the message list (called on tick).
    fn flush_token_buffer(&mut self) {
        let metrics_before = self.token_buffer.metrics().clone();
        if let Some(buffered) = self.token_buffer.flush() {
            let m = self.token_buffer.metrics();
            if m.backlog_flush_count > metrics_before.backlog_flush_count {
                debug!(
                    bytes = buffered.len(),
                    total_backlog_flushes = m.backlog_flush_count,
                    peak_pending = m.peak_pending_bytes,
                    "adaptive backlog flush"
                );
            }
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
                msg.started_at = Some(std::time::Instant::now());
                msg.agent_mode = Some(self.state.agent_mode.label().to_string());
                self.state.messages.push(msg);
            }
        } else {
            let mut msg = UiMessage::new(MessageKind::Assistant, content);
            msg.is_streaming = true;
            msg.started_at = Some(std::time::Instant::now());
            msg.agent_mode = Some(self.state.agent_mode.label().to_string());
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
                // Finalize response time from started_at
                if last.response_time.is_none() {
                    if let Some(started) = last.started_at {
                        last.response_time = Some(started.elapsed().as_secs_f64());
                    }
                }
            }
        }

        // Store model info and generate title in session metadata before saving
        if let Some(meta) = result.session.metadata.as_object_mut() {
            meta.insert(
                "provider".to_string(),
                serde_json::Value::String(self.state.agent.provider_name.clone()),
            );
            meta.insert(
                "model".to_string(),
                serde_json::Value::String(self.state.agent.model_name.clone()),
            );
            meta.insert(
                "costSummary".to_string(),
                serde_json::json!({
                    "totalUsd": self.state.agent.cost,
                    "budgetUsd": (self.state.agent.max_budget_usd > 0.0)
                        .then_some(self.state.agent.max_budget_usd),
                    "inputTokens": self.state.agent.tokens_used.input,
                    "outputTokens": self.state.agent.tokens_used.output,
                    "lastAlertThresholdPercent": self
                        .state
                        .agent
                        .latest_budget_alert
                        .map(|alert| alert.threshold_percent),
                }),
            );

            // Generate a title from the first user message if not already set
            if !meta.contains_key("title") {
                let first_user_msg = result
                    .session
                    .messages
                    .iter()
                    .find(|m| m.role == ava_types::Role::User)
                    .map(|m| m.content.as_str());
                if let Some(msg) = first_user_msg {
                    let title = ava_session::generate_title(msg);
                    meta.insert("title".to_string(), serde_json::Value::String(title));
                }
            }
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
            agent_list: None,
            theme_before_preview: None,
            active_modal: None,
            view_mode: ViewMode::default(),
            status_message: None,
            voice: VoiceState::default(),
            model_catalog: ava_config::CatalogState::default(),
            todo_items: Vec::new(),
            todo_state: None,
            question: None,
            copy_picker: None,
            btw: BtwState::default(),
            custom_commands: CustomCommandRegistry::default(),
            diff_preview: None,
            rewind: RewindState::default(),
            background: new_shared(),
            praxis: PraxisState::default(),
            hooks: HookRegistry::load(),
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
            question_rx: None,
            approval_rx: None,
            last_esc_time: None,
            pending_bg_goal: None,
            pending_praxis_goal: None,
            pending_images: Vec::new(),
            next_run_id: 1,
            foreground_run_id: None,
            background_run_routes: HashMap::new(),
            data_dir: PathBuf::from(".ava-test"),
        }
    }

    /// Send a key event through `handle_key` for testing.
    pub fn process_key_for_test(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let (app_tx, _) = mpsc::unbounded_channel();
        let (agent_tx, _) = mpsc::unbounded_channel();
        self.handle_key(key, app_tx, agent_tx)
    }

    /// Public wrapper around `handle_slash_command` for integration tests.
    pub fn test_slash_command(&mut self, input: &str) -> Option<(MessageKind, String)> {
        self.handle_slash_command(input, None)
    }
}

#[cfg(test)]
mod tests;
