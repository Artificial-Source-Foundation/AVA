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
use crate::state::lsp::LspSidebarEntry;
use crate::state::messages::{MessageKind, MessageState, UiMessage};
use crate::state::permission::PermissionState;
use crate::state::rewind::RewindState;
use crate::state::session::SessionState;
use crate::state::theme::Theme;
use crate::state::toast::ToastState;
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

pub(crate) use commands::format_skill_list;

#[derive(Debug, Clone)]
pub(crate) struct PendingBackgroundGoal {
    pub goal: String,
    pub isolated_branch: bool,
}

#[derive(Debug, Clone)]
struct BackgroundIsolation {
    worktree_path: PathBuf,
    branch_name: String,
}

#[derive(Debug, Clone)]
pub enum SidebarClickAction {
    ToggleMcpServer { name: String, enabled: bool },
    RefreshLsp,
}

#[derive(Debug, Clone)]
pub struct SidebarClickTarget {
    pub x: std::ops::Range<u16>,
    pub y: std::ops::Range<u16>,
    pub action: SidebarClickAction,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct MousePosition {
    pub column: u16,
    pub row: u16,
}

pub struct AppState {
    // ── Theme & Layout ──────────────────────────────────────────────────
    pub theme: Theme,
    pub show_sidebar: bool,
    pub view_mode: ViewMode,

    // ── Chat ────────────────────────────────────────────────────────────
    pub messages: MessageState,
    pub input: InputState,
    /// State for the /btw side-conversation overlay.
    pub btw: BtwState,

    // ── Agent ───────────────────────────────────────────────────────────
    pub agent: AgentState,
    pub agent_mode: AgentMode,
    /// Message index where the current agent turn started (BUG-41).
    /// Used to scope `mark_interrupted_messages` to only the current turn.
    pub turn_start_index: usize,

    // ── Session & Persistence ───────────────────────────────────────────
    pub session: SessionState,
    /// State for the rewind system (checkpoints + modal).
    pub rewind: RewindState,
    /// Shared snapshot manager for project-state undo/rollback.
    pub snapshot_manager: Option<ava_tools::core::file_snapshot::SharedSnapshotManager>,

    // ── Permissions & Keybindings ───────────────────────────────────────
    pub permission: PermissionState,
    pub keybinds: KeybindState,
    /// Active plan approval request from the agent's plan tool.
    pub plan_approval: Option<crate::state::plan_approval::PlanApprovalState>,

    // ── Model & Provider ────────────────────────────────────────────────
    pub model_catalog: ava_config::CatalogState,
    pub voice: VoiceState,

    // ── Modals ──────────────────────────────────────────────────────────
    pub active_modal: Option<ModalType>,
    pub command_palette: CommandPaletteState,
    pub session_list: SessionListState,
    pub model_selector: Option<ModelSelectorState>,
    pub tool_list: ToolListState,
    pub provider_connect: Option<ProviderConnectState>,
    pub theme_selector: Option<SelectListState<String>>,
    pub agent_list: Option<SelectListState<String>>,
    /// Saved theme before opening theme selector (for live preview revert on Esc).
    pub theme_before_preview: Option<Theme>,
    /// Active question from the agent (question tool).
    pub question: Option<QuestionState>,
    /// Active copy picker state (shown when multiple code blocks in last response).
    pub copy_picker: Option<CopyPickerState>,
    /// State for the diff preview modal (per-hunk accept/reject).
    pub diff_preview: Option<DiffPreviewState>,
    /// State for the generic info panel modal (/help, /mcp list, etc.).
    pub info_panel: Option<InfoPanelState>,

    // ── Notifications ───────────────────────────────────────────────────
    /// Toast notification state (top-right overlay, auto-dismiss).
    pub toast: ToastState,
    pub status_message: Option<StatusMessage>,

    // ── Background ─────────────────────────────────────────────────────
    /// Shared background task state.
    pub background: SharedBackgroundState,

    // ── Sidebar & Todo ──────────────────────────────────────────────────
    /// Cached snapshot of the agent's todo list for sidebar rendering.
    pub todo_items: Vec<ava_types::TodoItem>,
    /// Shared todo state handle (for async refresh).
    pub todo_state: Option<ava_types::TodoState>,
    /// Cached MCP server details for sidebar rendering.
    pub mcp_servers: Vec<ava_agent::stack::MCPServerInfo>,
    /// Cached LSP/project-tool rows for sidebar rendering.
    pub lsp_entries: Vec<LspSidebarEntry>,
    /// Clickable sidebar targets populated during render.
    pub sidebar_click_targets: Vec<SidebarClickTarget>,
    /// Last observed mouse position for hover affordances.
    pub mouse_position: Option<MousePosition>,
    /// Whether MCP integration is enabled in config.
    pub feature_mcp_enabled: bool,
    /// Whether LSP integration is enabled in config.
    pub feature_lsp_enabled: bool,

    // ── Extensions & Hooks ──────────────────────────────────────────────
    /// Registry of user-defined slash commands from TOML files.
    pub custom_commands: CustomCommandRegistry,
    /// Registry of user-defined lifecycle hooks.
    pub hooks: HookRegistry,

    // ── Misc ────────────────────────────────────────────────────────────
    /// Number of images pending attachment to the next user message.
    pub pending_image_count: usize,
    /// Configured provider names for the welcome screen display.
    pub configured_providers: Vec<String>,
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
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModalType {
    CommandPalette,
    SessionList,
    ToolApproval,
    PlanApproval,
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
    InfoPanel,
}

/// State for a generic scrollable info panel modal (used by /help, /mcp list, etc.).
#[derive(Debug, Clone)]
pub struct InfoPanelState {
    /// Title shown at the top of the modal.
    pub title: String,
    /// Pre-formatted text content.
    pub content: String,
    /// Current scroll offset (in lines).
    pub scroll: u16,
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
    /// Receiver for plan proposal requests from the agent's plan tool.
    plan_rx: Option<tokio::sync::mpsc::UnboundedReceiver<ava_tools::core::plan::PlanRequest>>,
    /// Timestamp of the last Esc press, for double-Esc detection.
    last_esc_time: Option<std::time::Instant>,
    /// Pending background goal from `/bg` command (consumed in submit_goal).
    pub(crate) pending_bg_goal: Option<PendingBackgroundGoal>,
    /// Images pending attachment to the next user message.
    pub(crate) pending_images: Vec<ava_types::ImageContent>,
    next_run_id: u64,
    foreground_run_id: Option<u64>,
    background_run_routes: HashMap<u64, usize>,
    data_dir: PathBuf,
    /// Set on terminal resize to force a full clear before next draw.
    needs_clear: bool,
    lsp_refresh_inflight: bool,
    last_lsp_refresh_at: Option<std::time::Instant>,
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

        // Load configured providers for welcome screen display
        let configured_providers = ava_config::CredentialStore::load_default()
            .await
            .map(|creds| {
                creds
                    .configured_providers()
                    .into_iter()
                    .map(String::from)
                    .collect()
            })
            .unwrap_or_default();

        let feature_config = if let Ok(manager) = ava_config::ConfigManager::load().await {
            Some(manager.get().await.features)
        } else {
            None
        };

        let (agent, question_rx, approval_rx, plan_rx) = AgentState::new(
            data_dir.clone(),
            provider,
            model,
            cli.max_turns,
            cli.max_budget_usd,
            cli.auto_approve,
            cli.fast,
        )
        .await?;
        let mcp_servers = agent.mcp_server_info().await.unwrap_or_default();
        let todo_state = agent.todo_state();
        let workspace = std::env::current_dir().unwrap_or_default();
        let lsp_entries = crate::state::lsp::refresh_lsp_entries(&workspace, &[]);

        let state = AppState {
            // Theme & Layout
            theme: Theme::from_name(&cli.theme),
            show_sidebar: false,
            view_mode: ViewMode::default(),
            // Chat
            messages: MessageState::default(),
            input: InputState::default(),
            btw: BtwState::default(),
            // Agent
            agent,
            agent_mode: AgentMode::default(),
            turn_start_index: 0,
            // Session & Persistence
            session,
            rewind: RewindState::default(),
            snapshot_manager: None,
            // Permissions & Keybindings
            permission,
            keybinds,
            plan_approval: None,
            // Model & Provider
            model_catalog,
            voice: VoiceState {
                auto_submit: cli.voice,
                continuous: cli.voice,
                ..VoiceState::default()
            },
            // Modals
            active_modal: None,
            command_palette: CommandPaletteState::with_defaults(),
            session_list: SessionListState::default(),
            model_selector: None,
            tool_list: ToolListState::default(),
            provider_connect: None,
            theme_selector: None,
            agent_list: None,
            theme_before_preview: None,
            question: None,
            copy_picker: None,
            diff_preview: None,
            info_panel: None,
            // Notifications
            toast: ToastState::default(),
            status_message: None,
            // Background
            background: new_shared(),
            // Sidebar & Todo
            todo_items: Vec::new(),
            todo_state,
            mcp_servers,
            lsp_entries,
            sidebar_click_targets: Vec::new(),
            mouse_position: None,
            feature_mcp_enabled: feature_config
                .as_ref()
                .map(|f| f.enable_mcp)
                .unwrap_or(true),
            feature_lsp_enabled: feature_config
                .as_ref()
                .map(|f| f.enable_lsp)
                .unwrap_or(true),
            // Extensions & Hooks
            custom_commands: CustomCommandRegistry::load(),
            hooks: HookRegistry::load(),
            // Misc
            pending_image_count: 0,
            configured_providers,
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
            plan_rx: Some(plan_rx),
            last_esc_time: None,
            pending_bg_goal: None,
            pending_images: Vec::new(),
            next_run_id: 1,
            foreground_run_id: None,
            background_run_routes: HashMap::new(),
            data_dir,
            needs_clear: false,
            lsp_refresh_inflight: false,
            last_lsp_refresh_at: None,
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
        )?;
        if crossterm::terminal::supports_keyboard_enhancement().unwrap_or(false) {
            execute!(
                stdout(),
                crossterm::event::PushKeyboardEnhancementFlags(
                    crossterm::event::KeyboardEnhancementFlags::REPORT_EVENT_TYPES
                        | crossterm::event::KeyboardEnhancementFlags::DISAMBIGUATE_ESCAPE_CODES
                )
            )?;
        }

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
            if crossterm::terminal::supports_keyboard_enhancement().unwrap_or(false) {
                let _ = execute!(
                    std::io::stdout(),
                    crossterm::event::PopKeyboardEnhancementFlags
                );
            }

            // Write crash log with full backtrace to ~/.ava/logs/crash-<timestamp>.log
            if let Some(home) = dirs::home_dir() {
                let crash_dir = home.join(".ava").join("logs");
                let _ = std::fs::create_dir_all(&crash_dir);
                let now = chrono::Utc::now();
                let timestamp = now.format("%Y-%m-%dT%H-%M-%S");
                let crash_path = crash_dir.join(format!("crash-{timestamp}.log"));
                if let Ok(mut f) = std::fs::File::create(&crash_path) {
                    use std::io::Write;
                    let _ = writeln!(f, "AVA Crash Report");
                    let _ = writeln!(f, "Time: {now}");
                    let _ = writeln!(f, "Panic: {info}");
                    let _ = writeln!(
                        f,
                        "\nBacktrace:\n{}",
                        std::backtrace::Backtrace::force_capture()
                    );
                    eprintln!("Crash log written to: {}", crash_path.display());
                }
            }

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
                    ava_types::Role::Assistant => {
                        // Assistant messages with non-empty tool_calls are tool invocations,
                        // but we display them as Assistant (the tool call details are in
                        // separate ToolCall UI messages created during the live run).
                        MessageKind::Assistant
                    }
                    ava_types::Role::Tool => {
                        // Distinguish tool calls from tool results using stored metadata.
                        // Messages with a tool_call_id are results returned by a tool.
                        // Messages with non-empty tool_calls are the call itself.
                        if msg.tool_call_id.is_some() || !msg.tool_results.is_empty() {
                            MessageKind::ToolResult
                        } else if !msg.tool_calls.is_empty() {
                            MessageKind::ToolCall
                        } else {
                            // Fallback heuristic: if content looks like a tool result
                            // (typically shorter, no JSON tool structure), treat as ToolResult.
                            MessageKind::ToolResult
                        }
                    }
                    ava_types::Role::System => MessageKind::System,
                };
                let mut ui_msg = UiMessage::new(kind.clone(), msg.content.clone());
                // Populate tool name for ToolCall/ToolResult messages so they render
                // with the correct tool badge in the message list.
                if matches!(kind, MessageKind::ToolCall | MessageKind::ToolResult) {
                    if let Some(tc) = msg.tool_calls.first() {
                        ui_msg.tool_name = Some(tc.name.clone());
                    }
                }
                self.state.messages.push(ui_msg);
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
        let mut plan_rx = self.plan_rx.take();

        // First-time onboarding: if no provider has credentials, show welcome
        // message and auto-open the provider connect modal.
        if self.pending_goal.is_none() {
            let credentials = ava_config::CredentialStore::load_default()
                .await
                .unwrap_or_default();
            if credentials.configured_providers().is_empty() {
                self.state.messages.push(UiMessage::new(
                    MessageKind::System,
                    "Welcome to AVA! No provider is configured yet. \
                     Set up a provider below to get started, or press Esc and use \
                     --provider/--model flags."
                        .to_string(),
                ));
                self.state.provider_connect =
                    Some(ProviderConnectState::from_credentials(&credentials));
                self.spawn_provider_connect_load(None, app_tx.clone());
                self.state.active_modal = Some(ModalType::ProviderConnect);
            }
        }

        if let Some(goal) = self.pending_goal.take() {
            self.submit_goal(goal, app_tx.clone(), agent_tx.clone());
        }

        loop {
            // On resize, clear the entire terminal buffer to prevent stale artifacts
            if self.needs_clear {
                terminal.clear()?;
                self.needs_clear = false;
            }

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
                Some(req) = async { match plan_rx.as_mut() { Some(rx) => rx.recv().await, None => None } } => {
                    self.handle_event(AppEvent::PlanProposal(req), app_tx.clone(), agent_tx.clone());
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
        )?;
        if crossterm::terminal::supports_keyboard_enhancement().unwrap_or(false) {
            execute!(
                terminal.backend_mut(),
                crossterm::event::PopKeyboardEnhancementFlags
            )?;
        }
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
                Ok(run) => {
                    self.finish_run(run);
                }
                Err(err) => {
                    self.is_streaming.store(false, Ordering::Relaxed);
                    self.state.agent.is_running = false;
                    self.state.agent.activity = AgentActivity::Idle;
                    // Only push the error if the agent loop didn't already emit
                    // an AgentEvent::Error for the same failure (avoid duplicates).
                    let already_shown = self
                        .state
                        .messages
                        .messages
                        .last()
                        .is_some_and(|m| matches!(m.kind, MessageKind::Error));
                    if !already_shown {
                        self.state
                            .messages
                            .push(UiMessage::new(MessageKind::Error, err));
                    }
                }
            }
            return;
        }

        let Some(task_id) = self.background_run_routes.remove(&run_id) else {
            return;
        };

        let (isolation_suffix, isolation_target) = {
            let bg = self
                .state
                .background
                .lock()
                .unwrap_or_else(|e| e.into_inner());
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
                self.state
                    .background
                    .lock()
                    .unwrap_or_else(|e| e.into_inner())
                    .complete_task(task_id);

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
                if let Ok(mut bg) = self.state.background.lock() {
                    bg.fail_task(task_id, err.clone());
                }

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
                let section = if [
                    "github_light",
                    "solarized_light",
                    "catppuccin_latte",
                    "one_light",
                    "rose_pine_dawn",
                    "terminal_paper",
                ]
                .contains(&name.as_str())
                {
                    Some("Light".to_string())
                } else if ["graphite", "aurora", "terminal_paper"].contains(&name.as_str()) {
                    Some("Modern".to_string())
                } else {
                    Some("Dark".to_string())
                };
                let detail = match name.as_str() {
                    "graphite" => "neutral dark",
                    "aurora" => "glow dark",
                    "terminal_paper" => "soft light",
                    "tokyo_night" => "city neon",
                    "vesper" => "inky contrast",
                    "poimandres" => "cool vivid",
                    "rose_pine" => "muted rose",
                    "catppuccin" => "soft pastel",
                    "github_light" => "clean light",
                    "one_light" => "editor light",
                    _ => "classic",
                }
                .to_string();
                let status = if &name == current {
                    Some(crate::widgets::select_list::ItemStatus::Active)
                } else if ["graphite", "aurora", "terminal_paper"].contains(&name.as_str()) {
                    Some(crate::widgets::select_list::ItemStatus::Info(
                        "new".to_string(),
                    ))
                } else {
                    None
                };
                SelectItem {
                    title: name.clone(),
                    detail,
                    section,
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
                msg.agent_mode = Some(self.state.agent_mode.label().to_string());
                self.state.messages.push(msg);
            }
        } else {
            let mut msg = UiMessage::new(MessageKind::Assistant, content);
            msg.is_streaming = true;
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
                // Set total loop duration from loop_started_at
                if let Some(started) = self.state.agent.loop_started_at {
                    last.response_time = Some(started.elapsed().as_secs_f64());
                }
            }
        }
        self.state.agent.loop_started_at = None;

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
                    "inputTokens": self.state.agent.tokens_used.cumulative_input,
                    "outputTokens": self.state.agent.tokens_used.cumulative_output,
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
            // Theme & Layout
            theme: Theme::default_theme(),
            show_sidebar: false,
            view_mode: ViewMode::default(),
            // Chat
            messages: MessageState::default(),
            input: InputState::default(),
            btw: BtwState::default(),
            // Agent
            agent: AgentState::test_new("test-provider", "test-model"),
            agent_mode: AgentMode::default(),
            turn_start_index: 0,
            // Session & Persistence
            session,
            rewind: RewindState::default(),
            snapshot_manager: None,
            // Permissions & Keybindings
            permission: PermissionState::default(),
            keybinds: KeybindState::default(),
            plan_approval: None,
            // Model & Provider
            model_catalog: ava_config::CatalogState::default(),
            voice: VoiceState::default(),
            // Modals
            active_modal: None,
            command_palette: CommandPaletteState::with_defaults(),
            session_list: SessionListState::default(),
            model_selector: None,
            tool_list: ToolListState::default(),
            provider_connect: None,
            theme_selector: None,
            agent_list: None,
            theme_before_preview: None,
            question: None,
            copy_picker: None,
            diff_preview: None,
            info_panel: None,
            // Notifications
            toast: ToastState::default(),
            status_message: None,
            // Background
            background: new_shared(),
            // Sidebar & Todo
            todo_items: Vec::new(),
            todo_state: None,
            mcp_servers: Vec::new(),
            lsp_entries: Vec::new(),
            sidebar_click_targets: Vec::new(),
            mouse_position: None,
            feature_mcp_enabled: true,
            feature_lsp_enabled: true,
            // Extensions & Hooks
            custom_commands: CustomCommandRegistry::default(),
            hooks: HookRegistry::load(),
            // Misc
            pending_image_count: 0,
            configured_providers: Vec::new(),
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
            plan_rx: None,
            last_esc_time: None,
            pending_bg_goal: None,
            pending_images: Vec::new(),
            next_run_id: 1,
            foreground_run_id: None,
            background_run_routes: HashMap::new(),
            data_dir: PathBuf::from(".ava-test"),
            needs_clear: false,
            lsp_refresh_inflight: false,
            last_lsp_refresh_at: None,
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
