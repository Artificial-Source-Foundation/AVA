mod commands;
mod event_handler;
mod modals;

use crate::config::cli::CliArgs;
use crate::config::keybindings::load_keybind_overrides;
use crate::event::{spawn_event_reader, spawn_tick_timer, AppEvent};
use crate::hooks::{HookContext, HookEvent, HookRegistry, HookResult, HookRunner};
use crate::state::agent::{AgentActivity, AgentMode, AgentState};
use crate::state::background::{self, SharedBackgroundState};
use crate::state::btw::BtwState;
use crate::state::custom_commands::CustomCommandRegistry;
use crate::state::input::InputState;
use crate::state::keybinds::{Action, KeybindState};
use crate::state::messages::{MessageKind, MessageState, UiMessage};
use crate::state::permission::PermissionState;
use crate::state::rewind::RewindState;
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
use tracing::debug;

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
    /// State for the rewind system (checkpoints + modal).
    pub rewind: RewindState,
    /// Shared background task state.
    pub background: SharedBackgroundState,
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
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ViewMode {
    /// Show the main conversation.
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

impl Default for ViewMode {
    fn default() -> Self {
        Self::Main
    }
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
    question_rx: Option<tokio::sync::mpsc::UnboundedReceiver<ava_tools::core::question::QuestionRequest>>,
    /// Timestamp of the last Esc press, for double-Esc detection.
    last_esc_time: Option<std::time::Instant>,
    /// Pending background goal from `/bg` command (consumed in submit_goal).
    pub(crate) pending_bg_goal: Option<String>,
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

        let (agent, question_rx) = AgentState::new(data_dir, provider, model, cli.max_turns, cli.auto_approve)
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
            rewind: RewindState::default(),
            background: background::new_shared(),
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
            last_esc_time: None,
            pending_bg_goal: None,
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

        // Take the question receiver so we can poll it in the event loop
        let mut question_rx = self.question_rx.take();

        if let Some(goal) = self.pending_goal.take() {
            self.submit_goal(goal, app_tx.clone(), agent_tx.clone());
        }

        loop {
            terminal.draw(|frame| ui::render(frame, &mut self.state))?;

            tokio::select! {
                Some(event) = app_rx.recv() => self.handle_event(event, app_tx.clone(), agent_tx.clone()),
                Some(agent_event) = agent_rx.recv() => self.handle_event(AppEvent::Agent(agent_event), app_tx.clone(), agent_tx.clone()),
                Some(req) = async { match question_rx.as_mut() { Some(rx) => rx.recv().await, None => None } } => {
                    self.handle_event(AppEvent::Question(req), app_tx.clone(), agent_tx.clone());
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

    /// Move the currently running agent to the background.
    /// The agent continues running; its events are routed to a BackgroundTask.
    pub(crate) fn background_current_agent(&mut self, _app_tx: mpsc::UnboundedSender<AppEvent>) {
        if !self.state.agent.is_running {
            return;
        }

        // Derive a goal from the last user message
        let goal = self
            .state
            .messages
            .messages
            .iter()
            .rev()
            .find(|m| matches!(m.kind, MessageKind::User))
            .map(|m| m.content.clone())
            .unwrap_or_else(|| "background task".to_string());

        // Create the background task entry
        let task_id = {
            let mut bg = self.state.background.lock().unwrap();
            let id = bg.add_task(goal.clone());
            // Copy existing tokens/cost to the background task
            bg.add_tokens(
                id,
                self.state.agent.tokens_used.input,
                self.state.agent.tokens_used.output,
                self.state.agent.cost,
            );
            // Copy existing messages to the background task
            for msg in &self.state.messages.messages {
                bg.append_message(id, msg.clone());
            }
            id
        };

        // Take the cancel token and task handle from the agent
        // The agent task is already running — we just need to stop routing
        // events to the foreground. We do this by marking the agent as not running
        // in the TUI state. The tokio task continues and sends AgentDone to app_tx.
        // We intercept AgentDone for this task via the task_id.

        // Mark foreground agent as idle
        self.state.agent.is_running = false;
        self.state.agent.activity = AgentActivity::Idle;
        self.is_streaming.store(false, Ordering::Relaxed);

        // Clear chat for a fresh conversation
        self.state.messages.messages.clear();
        self.state.messages.reset_scroll();

        // Reset token counters for the new foreground session
        self.state.agent.tokens_used = crate::state::agent::TokenUsage::default();
        self.state.agent.cost = 0.0;
        self.state.agent.current_turn = 0;
        self.state.agent.sub_agents.clear();

        // The existing agent task will continue sending events. Since agent.is_running
        // is false, Token/ToolCall events will still update messages — but we've cleared
        // them. We need a way to intercept. The simplest approach: the agent task will
        // send AgentDone when it finishes. We use a background monitor task that listens.
        //
        // NOTE: Because we can't easily redirect the existing agent_rx channel,
        // the remaining events from the backgrounded agent will be handled normally
        // by handle_agent_event. Since messages are cleared, new tokens/tool calls
        // from the background agent will appear in the foreground. This is a known
        // limitation. For a clean implementation, we'd need a per-agent event channel.
        //
        // Workaround: We track the background task_id and in the tick handler we
        // will mark it complete when the agent finishes.

        // Store the background task_id for event routing
        // We'll add this as a field we can check in handle_agent_event
        self.set_status(format!("Task #{task_id} moved to background"), StatusLevel::Info);

        // For now, the backgrounded agent will still send events to the main channel.
        // We cannot easily reroute mid-stream without significant refactoring.
        // The key behavior: when AgentDone fires, we mark the background task complete.
        // We store the active background task_id to intercept the AgentDone event.
    }

    /// Launch a new agent in the background with the given goal.
    pub(crate) fn launch_background_agent(
        &mut self,
        goal: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let stack = match self.state.agent.stack() {
            Ok(s) => Arc::clone(s),
            Err(msg) => {
                self.set_status(format!("Cannot launch background agent: {msg}"), StatusLevel::Error);
                return;
            }
        };

        let bg_state = Arc::clone(&self.state.background);
        let task_id = {
            let mut bg = bg_state.lock().unwrap();
            bg.add_task(goal.clone())
        };

        let max_turns = self.state.agent.max_turns;
        let app_tx_clone = app_tx;

        tokio::spawn(async move {
            let (agent_event_tx, mut agent_event_rx) = mpsc::unbounded_channel();
            let cancel = tokio_util::sync::CancellationToken::new();

            // Spawn event collector
            let bg_state_events = Arc::clone(&bg_state);
            let collector_task_id = task_id;
            let collector_handle = tokio::spawn(async move {
                while let Some(event) = agent_event_rx.recv().await {
                    let mut bg = bg_state_events.lock().unwrap();
                    match event {
                        ava_agent::AgentEvent::Token(chunk) => {
                            // Accumulate into last assistant message or create new
                            if let Some(task) = bg.tasks.iter_mut().find(|t| t.id == collector_task_id) {
                                if let Some(last) = task.messages.last_mut() {
                                    if matches!(last.kind, crate::state::messages::MessageKind::Assistant) {
                                        last.content.push_str(&chunk);
                                        continue;
                                    }
                                }
                                task.messages.push(crate::state::messages::UiMessage::new(
                                    crate::state::messages::MessageKind::Assistant,
                                    chunk,
                                ));
                            }
                        }
                        ava_agent::AgentEvent::TokenUsage { input_tokens, output_tokens, cost_usd } => {
                            bg.add_tokens(collector_task_id, input_tokens, output_tokens, cost_usd);
                        }
                        ava_agent::AgentEvent::ToolCall(call) => {
                            bg.append_message(
                                collector_task_id,
                                crate::state::messages::UiMessage::new(
                                    crate::state::messages::MessageKind::ToolCall,
                                    format!("{} {}", call.name, call.arguments),
                                ),
                            );
                        }
                        ava_agent::AgentEvent::ToolResult(result) => {
                            bg.append_message(
                                collector_task_id,
                                crate::state::messages::UiMessage::new(
                                    crate::state::messages::MessageKind::ToolResult,
                                    result.content,
                                ),
                            );
                        }
                        ava_agent::AgentEvent::Error(err) => {
                            bg.append_message(
                                collector_task_id,
                                crate::state::messages::UiMessage::new(
                                    crate::state::messages::MessageKind::Error,
                                    err,
                                ),
                            );
                        }
                        ava_agent::AgentEvent::Thinking(content) => {
                            bg.append_message(
                                collector_task_id,
                                crate::state::messages::UiMessage::new(
                                    crate::state::messages::MessageKind::Thinking,
                                    content,
                                ),
                            );
                        }
                        _ => {}
                    }
                }
            });

            let result = stack
                .run(&goal, max_turns, Some(agent_event_tx), cancel, Vec::new())
                .await;

            // Wait for collector to drain
            let _ = collector_handle.await;

            let success = result.is_ok();
            {
                let mut bg = bg_state.lock().unwrap();
                if success {
                    bg.complete_task(task_id);
                } else {
                    let err = result.err().map(|e| e.to_string()).unwrap_or_default();
                    bg.fail_task(task_id, err);
                }
            }

            let _ = app_tx_clone.send(crate::event::AppEvent::BackgroundTaskDone {
                task_id,
                success,
            });
        });

        self.set_status(format!("Task #{task_id} launched in background"), StatusLevel::Info);
    }

    /// Enter the background task view (read-only).
    pub(crate) fn enter_background_task_view(&mut self, task_id: usize) -> bool {
        let bg = self.state.background.lock().unwrap();
        if let Some(task) = bg.tasks.iter().find(|t| t.id == task_id) {
            let goal = task.goal_display(50);
            drop(bg);
            self.state.view_mode = ViewMode::BackgroundTask { task_id, goal };
            self.state.messages.reset_scroll();
            true
        } else {
            false
        }
    }

    pub(crate) fn set_status(&mut self, text: impl Into<String>, level: StatusLevel) {
        self.state.status_message = Some(StatusMessage::new(text, level));
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

    /// Return to the main conversation view.
    pub(crate) fn exit_sub_agent_view(&mut self) {
        self.state.view_mode = ViewMode::Main;
        self.state.messages.reset_scroll();
    }

    /// Copy the last assistant message content to the system clipboard.
    /// If `force_all` is true, always copies the entire response.
    /// Otherwise, if there are multiple code blocks, shows a picker modal.
    pub(crate) fn copy_last_response_with_mode(&mut self, force_all: bool) {
        match self.state.messages.last_assistant_content() {
            Some(content) => {
                let content = content.to_owned();
                if !force_all {
                    let blocks = Self::extract_code_blocks(&content);
                    if blocks.len() > 1 {
                        // Show picker modal
                        self.state.copy_picker = Some(CopyPickerState {
                            blocks,
                            full_content: content,
                        });
                        self.state.active_modal = Some(ModalType::CopyPicker);
                        return;
                    }
                }
                self.copy_to_clipboard(&content, None);
            }
            None => {
                self.set_status("No assistant message to copy", StatusLevel::Warn);
            }
        }
    }

    /// Legacy entry point — used by Ctrl+Y and Action::CopyLastResponse.
    pub(crate) fn copy_last_response(&mut self) {
        self.copy_last_response_with_mode(false);
    }

    /// Actually write text to the system clipboard and show a status message.
    pub(crate) fn copy_to_clipboard(&mut self, text: &str, label: Option<String>) {
        match arboard::Clipboard::new() {
            Ok(mut clipboard) => match clipboard.set_text(text) {
                Ok(_) => {
                    let status = if let Some(lbl) = label {
                        lbl
                    } else {
                        let preview_len = text.len().min(40);
                        let preview: String = text.chars().take(preview_len).collect();
                        let ellipsis = if text.len() > 40 { "..." } else { "" };
                        format!("Copied to clipboard: \"{preview}{ellipsis}\"")
                    };
                    self.set_status(status, StatusLevel::Info);
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

    /// Spawn a lightweight LLM call for a `/btw` side question.
    ///
    /// The response renders in a dismissible overlay and never enters message history.
    /// Works concurrently with a running agent by using a separate tokio task.
    pub(crate) fn handle_btw_query(&mut self, question: String) {
        // Collect current conversation context (User + Assistant messages only)
        let history: Vec<ava_types::Message> = self
            .state
            .messages
            .messages
            .iter()
            .filter_map(|ui_msg| {
                let role = match ui_msg.kind {
                    MessageKind::User => ava_types::Role::User,
                    MessageKind::Assistant => ava_types::Role::Assistant,
                    _ => return None,
                };
                Some(ava_types::Message::new(role, ui_msg.content.clone()))
            })
            .collect();

        // Get a provider via the agent stack's router
        let stack = match self.state.agent.stack() {
            Ok(s) => Arc::clone(s),
            Err(msg) => {
                self.set_status(format!("Cannot run /btw: {msg}"), StatusLevel::Error);
                return;
            }
        };

        let provider_name = self.state.agent.provider_name.clone();
        let model_name = self.state.agent.model_name.clone();

        self.state.btw.pending = true;
        self.state.btw.response = None;
        self.set_status("Thinking about your side question...", StatusLevel::Info);

        let question_clone = question.clone();

        // We need to send the result back via an AppEvent. Find the app_tx.
        // The event loop is driven by app_rx, so we need a sender. We'll use
        // a dedicated channel approach: create a oneshot and spawn a task that
        // sends on app_tx. But we don't have app_tx here. Instead, we'll stash
        // the result in a shared state via an Arc<Mutex>.
        // Actually, the simplest approach: use the app_tx from the event loop.
        // We can't access it here, but we can store a clone on the App struct.
        // For now, let's use a lighter approach: write to a shared slot that
        // the tick handler checks.

        // Use a shared slot that the tick handler polls
        let btw_result: Arc<std::sync::Mutex<Option<crate::state::btw::BtwResponse>>> =
            Arc::new(std::sync::Mutex::new(None));
        let btw_result_clone = Arc::clone(&btw_result);
        self.state.btw.pending_result = Some(btw_result);

        tokio::spawn(async move {
            // Build messages: system + history + the btw question
            let mut messages = Vec::with_capacity(history.len() + 2);
            messages.push(ava_types::Message::new(
                ava_types::Role::System,
                "Answer this side question briefly and directly. You have access to \
                 the conversation context but no tools. Keep your answer concise."
                    .to_string(),
            ));
            messages.extend(history);
            messages.push(ava_types::Message::new(
                ava_types::Role::User,
                question_clone.clone(),
            ));

            // Resolve a provider instance from the router
            let result = stack.router.route_required(&provider_name, &model_name).await;
            match result {
                Ok(provider) => {
                    match provider.generate(&messages).await {
                        Ok(answer) => {
                            let response = crate::state::btw::BtwResponse {
                                question: question_clone,
                                answer,
                            };
                            if let Ok(mut slot) = btw_result_clone.lock() {
                                *slot = Some(response);
                            }
                        }
                        Err(e) => {
                            let response = crate::state::btw::BtwResponse {
                                question: question_clone,
                                answer: format!("Error: {e}"),
                            };
                            if let Ok(mut slot) = btw_result_clone.lock() {
                                *slot = Some(response);
                            }
                        }
                    }
                }
                Err(e) => {
                    let response = crate::state::btw::BtwResponse {
                        question: question_clone,
                        answer: format!("Provider error: {e}"),
                    };
                    if let Ok(mut slot) = btw_result_clone.lock() {
                        *slot = Some(response);
                    }
                }
            }
        });
    }

    /// Open the rewind modal if there are checkpoints to rewind to.
    pub(crate) fn open_rewind_modal(&mut self) {
        if self.state.rewind.checkpoints.is_empty() {
            self.set_status("No checkpoints to rewind to", StatusLevel::Warn);
            return;
        }
        self.state.rewind.open();
        self.state.active_modal = Some(ModalType::Rewind);
    }

    /// Execute the selected rewind action.
    pub(crate) fn execute_rewind(&mut self, option: crate::state::rewind::RewindOption) {
        use crate::state::rewind::RewindOption;

        let checkpoint_idx = match self.state.rewind.checkpoints.len().checked_sub(1) {
            Some(idx) => idx,
            None => return,
        };

        let checkpoint = &self.state.rewind.checkpoints[checkpoint_idx];
        let msg_index = checkpoint.message_index;
        let preview: String = if checkpoint.message_preview.len() > 50 {
            format!("{}...", &checkpoint.message_preview[..47])
        } else {
            checkpoint.message_preview.clone()
        };

        match option {
            RewindOption::RestoreCodeAndConversation => {
                // Restore files
                let (file_count, errors) = self.state.rewind.restore_files_after(checkpoint_idx);
                // Remove messages after checkpoint
                if msg_index < self.state.messages.messages.len() {
                    self.state.messages.messages.truncate(msg_index);
                }
                self.state.messages.reset_scroll();
                // Remove checkpoints
                self.state.rewind.truncate_after(checkpoint_idx);

                let mut status = format!("Rewound to before: '{preview}'");
                if file_count > 0 {
                    status.push_str(&format!(" ({file_count} files restored)"));
                }
                if !errors.is_empty() {
                    status.push_str(&format!(" ({} errors)", errors.len()));
                }
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, &status));
                self.set_status(&status, StatusLevel::Info);
            }
            RewindOption::RestoreConversation => {
                // Remove messages after checkpoint (keep files)
                if msg_index < self.state.messages.messages.len() {
                    self.state.messages.messages.truncate(msg_index);
                }
                self.state.messages.reset_scroll();
                self.state.rewind.truncate_after(checkpoint_idx);

                let status = format!("Rewound conversation to before: '{preview}'");
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, &status));
                self.set_status(&status, StatusLevel::Info);
            }
            RewindOption::RestoreCode => {
                // Restore files only (keep conversation)
                let (file_count, errors) = self.state.rewind.restore_files_after(checkpoint_idx);
                // Clear file changes from the checkpoint but keep it
                if let Some(cp) = self.state.rewind.checkpoints.get_mut(checkpoint_idx) {
                    cp.file_changes.clear();
                }

                let mut status = format!("Restored {file_count} file(s) to before: '{preview}'");
                if !errors.is_empty() {
                    status.push_str(&format!(" ({} errors)", errors.len()));
                }
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::System, &status));
                self.set_status(&status, StatusLevel::Info);
            }
            RewindOption::SummarizeFromHere => {
                // Replace all messages before the checkpoint with a summary
                if msg_index > 0 && msg_index <= self.state.messages.messages.len() {
                    let summary_msg = UiMessage::new(
                        MessageKind::System,
                        format!(
                            "--- Earlier conversation summarized ({msg_index} messages) ---\n\
                             Last topic before summary: \"{preview}\""
                        ),
                    );
                    // Keep messages from msg_index onward
                    let kept: Vec<UiMessage> =
                        self.state.messages.messages.drain(msg_index..).collect();
                    self.state.messages.messages.clear();
                    self.state.messages.messages.push(summary_msg);
                    self.state.messages.messages.extend(kept);
                    self.state.messages.reset_scroll();
                }
                self.set_status("Conversation summarized", StatusLevel::Info);
            }
            RewindOption::Cancel => {
                // Do nothing
            }
        }

        self.state.rewind.close();
        self.state.active_modal = None;
    }

    /// Extract fenced code blocks from markdown content.
    pub(crate) fn extract_code_blocks(content: &str) -> Vec<CodeBlock> {
        let mut blocks = Vec::new();
        let mut in_block = false;
        let mut current_lang = String::new();
        let mut current_content = String::new();
        let mut start_line = 0usize;

        for (i, line) in content.lines().enumerate() {
            let trimmed = line.trim();
            if !in_block && trimmed.starts_with("```") {
                in_block = true;
                current_lang = trimmed[3..].trim().to_string();
                current_content.clear();
                start_line = i + 1; // 1-indexed for display
            } else if in_block && trimmed.starts_with("```") {
                in_block = false;
                // Remove trailing newline if present
                if current_content.ends_with('\n') {
                    current_content.pop();
                }
                blocks.push(CodeBlock {
                    language: current_lang.clone(),
                    content: current_content.clone(),
                    start_line: start_line + 1,
                    end_line: i,
                });
            } else if in_block {
                if !current_content.is_empty() {
                    current_content.push('\n');
                }
                current_content.push_str(line);
            }
        }

        blocks
    }

    /// Open the agent list modal showing sub-agent configuration from agents.toml.
    pub(crate) fn open_agent_list(&mut self) {
        let home = dirs::home_dir().unwrap_or_default();
        let global_path = home.join(".ava").join("agents.toml");
        let project_path = std::env::current_dir()
            .unwrap_or_default()
            .join(".ava")
            .join("agents.toml");

        let config = ava_config::AgentsConfig::load(&global_path, &project_path);

        // Collect known agent types: always show "task", plus any explicitly configured
        let mut agent_names: Vec<String> = vec!["task".to_string()];
        for name in config.agents.keys() {
            if !agent_names.contains(name) {
                agent_names.push(name.clone());
            }
        }
        agent_names.sort();

        let items: Vec<SelectItem<String>> = agent_names
            .iter()
            .map(|name| {
                let resolved = config.get_agent(name);
                let status_text = if resolved.enabled { "enabled" } else { "disabled" };
                let mut detail_parts: Vec<String> = vec![status_text.to_string()];
                if let Some(ref model) = resolved.model {
                    detail_parts.push(format!("model={model}"));
                }
                if let Some(turns) = resolved.max_turns {
                    detail_parts.push(format!("max_turns={turns}"));
                }
                let detail = detail_parts.join("  ");
                let status = if resolved.enabled {
                    Some(crate::widgets::select_list::ItemStatus::Connected("enabled".to_string()))
                } else {
                    Some(crate::widgets::select_list::ItemStatus::Info("disabled".to_string()))
                };
                SelectItem {
                    title: name.clone(),
                    detail,
                    section: Some("Sub-Agents".to_string()),
                    status,
                    value: name.clone(),
                    enabled: true,
                }
            })
            .collect();

        self.state.agent_list = Some(SelectListState::new(items));
        self.state.active_modal = Some(ModalType::AgentList);
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
                        MouseEventKind::ScrollUp => self.state.messages.scroll_up(1),
                        MouseEventKind::ScrollDown => self.state.messages.scroll_down(1),
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
                // Refresh todo items from shared state (cheap sync read)
                if let Some(ref todo_state) = self.state.todo_state {
                    self.state.todo_items = todo_state.get();
                }
                // Check background task notifications
                {
                    let mut bg = self.state.background.lock().unwrap();
                    bg.expire_notification();
                    // If there's a notification, show it in the status bar
                    if let Some((ref text, _)) = bg.notification {
                        // Only set status if we haven't already shown this notification
                        let should_set = self
                            .state
                            .status_message
                            .as_ref()
                            .map(|m| m.text != *text)
                            .unwrap_or(true);
                        if should_set {
                            let text = text.clone();
                            drop(bg);
                            let is_failure = text.contains("failed");
                            let level = if is_failure {
                                StatusLevel::Error
                            } else {
                                StatusLevel::Info
                            };
                            self.set_status(text, level);
                        }
                    }
                }
                // Poll for /btw side-question results
                let btw_ready = self.state.btw.pending_result.as_ref().and_then(|slot| {
                    slot.try_lock().ok().and_then(|mut guard| guard.take())
                });
                if let Some(response) = btw_ready {
                    self.state.btw.pending = false;
                    self.state.btw.response = Some(response);
                    self.state.btw.pending_result = None;
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
            AppEvent::Question(req) => {
                self.state.question = Some(QuestionState {
                    question: req.question,
                    options: req.options.clone(),
                    selected: 0,
                    input: String::new(),
                    reply: Some(req.reply),
                });
                self.state.active_modal = Some(ModalType::Question);
            }
            AppEvent::BackgroundTaskDone { task_id, success } => {
                let status = if success { "completed" } else { "failed" };
                self.set_status(
                    format!("Background task #{task_id} {status}"),
                    if success { StatusLevel::Info } else { StatusLevel::Error },
                );
            }
            AppEvent::HookResult { event, result, description } => {
                match result {
                    HookResult::Block(reason) => {
                        self.set_status(
                            format!("Hook blocked {event}: {reason}"),
                            StatusLevel::Error,
                        );
                        debug!(hook = %description, event = %event, reason = %reason, "hook blocked action");
                    }
                    HookResult::Error(msg) => {
                        self.set_status(
                            format!("Hook error ({event}): {msg}"),
                            StatusLevel::Error,
                        );
                        debug!(hook = %description, event = %event, error = %msg, "hook error");
                    }
                    HookResult::Allow => {
                        debug!(hook = %description, event = %event, "hook allowed");
                    }
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
        // Dismiss the /btw overlay if it is visible (Space, Enter, Esc)
        if self.state.btw.response.is_some() {
            match key.code {
                KeyCode::Char(' ') | KeyCode::Enter | KeyCode::Esc => {
                    self.state.btw.response = None;
                    return false;
                }
                _ => {}
            }
        }

        // Handle modal-specific input first
        if let Some(modal) = self.state.active_modal {
            return self.handle_modal_key(modal, key, app_tx);
        }

        // Escape exits sub-agent or background task view when no modal is open
        if key.code == KeyCode::Esc && matches!(self.state.view_mode, ViewMode::SubAgent { .. } | ViewMode::BackgroundTask { .. }) {
            self.state.view_mode = ViewMode::Main;
            self.state.messages.reset_scroll();
            return false;
        }

        // Double-Esc detection: two Esc presses within 500ms opens rewind modal
        if key.code == KeyCode::Esc && !self.state.agent.is_running {
            if let Some(last) = self.last_esc_time {
                if last.elapsed() < std::time::Duration::from_millis(500) {
                    self.last_esc_time = None;
                    self.open_rewind_modal();
                    return false;
                }
            }
            self.last_esc_time = Some(std::time::Instant::now());
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
                Action::ScrollUp => {
                    let half_page = (self.state.messages.visible_height / 2).max(1);
                    self.state.messages.scroll_up(half_page);
                }
                Action::ScrollDown => {
                    let half_page = (self.state.messages.visible_height / 2).max(1);
                    self.state.messages.scroll_down(half_page);
                }
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
                Action::BackgroundAgent => {
                    if self.state.agent.is_running {
                        self.background_current_agent(app_tx.clone());
                    } else {
                        self.set_status("No running agent to background (use /bg <goal>)", StatusLevel::Warn);
                    }
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
                // If input is empty and there are completed sub-agents, enter the
                // last one's conversation view.
                if self.state.input.buffer.is_empty()
                    && !self.state.agent.is_running
                    && matches!(self.state.view_mode, ViewMode::Main)
                {
                    let last_completed = self
                        .state
                        .agent
                        .sub_agents
                        .iter()
                        .rposition(|sa| !sa.is_running && !sa.session_messages.is_empty());
                    if let Some(idx) = last_completed {
                        self.enter_sub_agent_view(idx);
                        return false;
                    }
                }
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
            // Up/Down: scroll messages when composer is empty; otherwise navigate input/history
            KeyCode::Up => {
                if self.state.input.buffer.is_empty() {
                    self.state.messages.scroll_up(1);
                } else if !self.state.input.move_up() {
                    self.state.input.history_up();
                }
            }
            KeyCode::Down => {
                if self.state.input.buffer.is_empty() {
                    self.state.messages.scroll_down(1);
                } else if !self.state.input.move_down() {
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
                    meta.insert(
                        "title".to_string(),
                        serde_json::Value::String(title),
                    );
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
            rewind: RewindState::default(),
            background: background::new_shared(),
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
            last_esc_time: None,
            pending_bg_goal: None,
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
        self.handle_slash_command(input)
    }
}
