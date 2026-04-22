pub mod attachment_state;
pub mod cache_diagnostics;
mod completion;
mod context_recovery;
mod repetition;
mod response;
pub mod sidechain;
mod steering;
mod tool_execution;

use std::collections::HashSet;
use std::pin::Pin;
use std::sync::Arc;
use std::time::Instant;

use ava_context::ContextManager;
use ava_plugin::PluginManager;
use ava_tools::registry::ToolRegistry;
use ava_types::{
    ImageContent, Message, Role, Session, ThinkingLevel, TokenUsage, ToolCall, ToolResult,
};
use futures::Stream;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tracing::{debug, instrument, warn};

use crate::llm_trait::LLMProvider;
use crate::message_queue::MessageQueue;
use crate::stuck::{StuckAction, StuckDetector};
use crate::system_prompt::BenchmarkPromptOverride;
use crate::trace::{append_trace_event, RunEvent, RunEventKind};

use repetition::RepetitionDetector;

use tool_execution::has_validation_failure;
pub use tool_execution::READ_ONLY_TOOLS;

/// Tools allowed in Plan mode. The LLM only sees these — write/edit are hidden.
/// Bash is included but restricted at execution time to read-only commands.
///
/// This is intentionally public because the extracted `ava-agent-orchestration`
/// seam reuses the same canonical plan-mode tool-visibility policy.
pub const PLAN_MODE_ALLOWED_TOOLS: &[&str] = &[
    "read",
    "glob",
    "grep",
    "web_fetch",
    "web_search",
    "git",
    "plan",
    "todo_read",
    "todo_write",
    "question",
    "codebase_search",
    "memory_read",
    "bash",
];

fn usage_cost_usd(model: &str, usage: &TokenUsage) -> f64 {
    let (in_rate, out_rate) = ava_llm::providers::common::model_pricing_usd_per_million(model);
    ava_llm::providers::common::estimate_cost_with_cache_usd(usage, in_rate, out_rate)
}

/// Hard cap on total tool calls per agent turn to prevent runaway tool invocations.
const MAX_TOOLS_PER_TURN: usize = 50;

/// Default timeout in seconds for LLM stream silence. If no tokens arrive within
/// this window, the stream is cancelled. This is a per-chunk timeout (resets on each
/// received token), not a total request timeout. Set high enough to accommodate
/// thinking models (e.g., Opus with extended thinking can take 30-60s before the
/// first token).
pub const LLM_STREAM_TIMEOUT_SECS: u64 = 90;

/// Core agent execution loop that orchestrates LLM calls, tool execution, and stuck detection.
///
/// Uses a single unified engine (`run_unified`) for both headless and streaming modes.
/// Read-only tools are executed concurrently; write tools run sequentially.
pub struct AgentLoop {
    pub llm: Box<dyn LLMProvider>,
    pub tools: Arc<ToolRegistry>,
    pub context: ContextManager,
    pub config: AgentConfig,
    pub(crate) last_request_hash: Option<u64>,
    pub(crate) last_request_time: Option<Instant>,
    /// Conversation history from previous turns (injected after system prompt, before goal).
    history: Vec<Message>,
    /// Optional message queue for mid-stream user messaging (steering, follow-up, post-complete).
    pub message_queue: Option<MessageQueue>,
    /// Images to attach to the first user (goal) message.
    images: Vec<ImageContent>,
    /// Optional plugin manager for firing lifecycle hooks.
    plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
    /// Tracks file diffs for write/edit tool calls.
    diff_tracker: crate::streaming_diff::StreamingDiffTracker,
    /// Optional JSONL session logger (opt-in via config).
    session_logger: Option<crate::session_logger::SessionLogger>,
    /// Optional session ID to use instead of generating a new one.
    /// When set, the resulting session will use this ID, allowing
    /// external callers (e.g., web frontend) to pre-assign the ID.
    session_id: Option<uuid::Uuid>,
    /// Shadow git snapshot manager for full project-state undo/rollback.
    snapshot_manager: ava_tools::core::file_snapshot::SharedSnapshotManager,
    /// Trusted project root used for contextual instruction resolution.
    project_root: Option<std::path::PathBuf>,
    /// Tracks contextual `AGENTS.md` files already injected during this session.
    activated_context_paths: std::sync::Mutex<HashSet<std::path::PathBuf>>,
    /// Tracks `.ava/rules/*.md` files already injected during this session.
    activated_rule_paths: std::sync::Mutex<HashSet<std::path::PathBuf>>,
    /// When true, suppress `AgentEvent::Token` emissions for the current turn.
    /// Set after a stuck-detector `InjectMessage` so the model's acknowledgment
    /// of the nudge doesn't leak into the visible assistant response.
    suppress_next_tokens: bool,
    /// When false, skip on-demand project rule injection entirely.
    enable_dynamic_rules: bool,
    /// Cached active tool definitions for this loop configuration.
    cached_tool_defs: std::sync::Mutex<Option<Vec<ava_types::Tool>>>,
    /// Cached post-hook tool definitions for this loop configuration.
    /// Plugin tool-definition hooks are treated as stable for the loop lifetime.
    cached_hooked_tool_defs: std::sync::Mutex<Option<Vec<ava_types::Tool>>>,
    /// Visible tool subset for the current goal.
    tool_visibility_profile: crate::routing::ToolVisibilityProfile,
    /// F1 — Pre-dispatched tool results from streaming execution.
    /// Maps finalized tool call index → pre-executed result.
    pre_dispatched_results: std::collections::HashMap<String, ava_types::ToolResult>,
    /// Attachment delta tracker — only injects changed MCP/skills/memories per turn.
    /// Wired into context injection by callers via `compute_attachment_delta`.
    #[allow(dead_code)]
    pub attachment_state: attachment_state::AttachmentState,
    trace_data_dir: Option<std::path::PathBuf>,
    trace_run_id: Option<String>,
}

/// Configuration for a single agent loop run — turn limits, cost caps, and model identity.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentConfig {
    /// Maximum number of turns (0 = unlimited).
    pub max_turns: usize,
    /// Maximum budget in USD (0 = unlimited). CLI-level cost cap.
    #[serde(default)]
    pub max_budget_usd: f64,
    pub token_limit: usize,
    /// Provider name (e.g., "anthropic", "openai", "zai-coding-plan").
    #[serde(default)]
    pub provider: String,
    pub model: String,
    #[serde(default = "default_max_cost")]
    pub max_cost_usd: f64,
    #[serde(default = "default_loop_detection")]
    pub loop_detection: bool,
    /// Optional override for the system prompt. When set, replaces the default system prompt.
    #[serde(default)]
    pub custom_system_prompt: Option<String>,
    /// Thinking/reasoning level for models that support extended thinking.
    #[serde(default)]
    pub thinking_level: ThinkingLevel,
    /// Optional quantitative cap for providers that support explicit thinking budgets.
    #[serde(default)]
    pub thinking_budget_tokens: Option<u32>,
    /// Optional suffix appended to the system prompt (e.g., mode-specific instructions).
    #[serde(default)]
    pub system_prompt_suffix: Option<String>,
    /// Benchmark-only family/file override for prompt assembly.
    #[serde(default)]
    pub benchmark_prompt_override: Option<BenchmarkPromptOverride>,
    /// Trusted project root used for contextual instruction resolution.
    #[serde(default)]
    pub project_root: Option<std::path::PathBuf>,
    /// When true, inject matching `.ava/rules/*.md` lazily after file touches.
    #[serde(default = "default_enable_dynamic_rules")]
    pub enable_dynamic_rules: bool,
    /// When true, include extended-tier tools in the system prompt alongside
    /// default tools. Extended tools are always *executable* regardless of this flag.
    #[serde(default)]
    pub extended_tools: bool,
    /// When true, restrict write/edit tools to `.ava/plans/*.md` paths only (Plan mode).
    #[serde(default)]
    pub plan_mode: bool,
    /// When true, automatically compact context when usage exceeds the threshold (default true).
    #[serde(default = "default_auto_compact")]
    pub auto_compact: bool,
    /// Optional post-edit validation steps run after successful write/edit tools.
    #[serde(default)]
    pub post_edit_validation: Option<PostEditValidationConfig>,
    /// Timeout in seconds for LLM stream silence. If no chunk arrives within this
    /// window, the stream is cancelled. Resets on each received chunk. 0 = no timeout.
    #[serde(default = "default_stream_timeout_secs")]
    pub stream_timeout_secs: u64,
    /// When true (default), tell providers to cache static prompt prefixes (system
    /// prompt + tool definitions). Reduces latency and cost on multi-turn conversations
    /// for providers that support it (e.g., Anthropic `cache_control`).
    #[serde(default = "default_prompt_caching")]
    pub prompt_caching: bool,
    /// When true, the agent is running in headless/non-interactive mode.
    /// Affects retry behavior (persistent mode) and other background-friendly defaults.
    #[serde(default)]
    pub headless: bool,
    /// When true, this agent is a sub-agent or background worker.
    /// Combined with `headless`, determines persistent retry mode.
    #[serde(default)]
    pub is_subagent: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct PostEditValidationConfig {
    /// Run the existing extended-tier `lint` tool after successful edits.
    #[serde(default)]
    pub lint: bool,
    /// Run the existing extended-tier `test_runner` tool after successful edits.
    #[serde(default)]
    pub tests: bool,
    /// Optional custom lint command passed through to the lint tool.
    #[serde(default)]
    pub lint_command: Option<String>,
    /// Optional custom test command passed through to the test_runner tool.
    #[serde(default)]
    pub test_command: Option<String>,
    /// Timeout in seconds for post-edit test execution.
    #[serde(default = "default_post_edit_test_timeout_secs")]
    pub test_timeout_secs: u64,
}

impl PostEditValidationConfig {
    pub fn enabled(&self) -> bool {
        self.lint || self.tests
    }
}

fn default_max_cost() -> f64 {
    1.0
}

fn default_loop_detection() -> bool {
    true
}

fn default_auto_compact() -> bool {
    true
}

fn default_post_edit_test_timeout_secs() -> u64 {
    60
}

fn default_stream_timeout_secs() -> u64 {
    LLM_STREAM_TIMEOUT_SECS
}

fn default_prompt_caching() -> bool {
    true
}

fn default_enable_dynamic_rules() -> bool {
    true
}

/// Events emitted during streaming agent execution for UI consumption.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompactedMessagePreview {
    pub role: String,
    pub content: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum SubAgentLiveEvent {
    Started { session_id: String },
    Token(String),
    Thinking(String),
    ToolCall(ToolCall),
    ToolResult(ToolResult),
    Checkpoint(Session),
    Error(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum AgentEvent {
    Token(String),
    /// Thinking/reasoning content from the model (displayed separately in UI).
    Thinking(String),
    ToolCall(ToolCall),
    ToolResult(ToolResult),
    Progress(String),
    Complete(Session),
    /// Periodic checkpoint of the session state, emitted after each turn so
    /// callers can persist progress incrementally. If the process exits
    /// unexpectedly, the last checkpoint is recoverable.
    Checkpoint(Session),
    Error(String),
    /// F8 — Stream silence warning: emitted when no chunks have arrived for half
    /// the configured timeout. Gives the UI a chance to inform the user before
    /// the hard kill at the full timeout.
    StreamSilenceWarning {
        elapsed_secs: u64,
    },
    ToolStats(ava_tools::monitor::ToolStats),
    TokenUsage {
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
    },
    BudgetWarning {
        threshold_percent: u8,
        current_cost_usd: f64,
        max_budget_usd: f64,
    },
    ContextCompacted {
        auto: bool,
        tokens_before: usize,
        tokens_after: usize,
        tokens_saved: usize,
        messages_before: usize,
        messages_after: usize,
        usage_before_percent: f64,
        summary: String,
        context_summary: String,
        active_messages: Vec<CompactedMessagePreview>,
    },
    /// A file edit has completed. Contains the unified diff for UI display.
    DiffPreview {
        file: std::path::PathBuf,
        diff_text: String,
        additions: usize,
        deletions: usize,
    },
    /// An MCP server sent a `notifications/tools/list_changed` notification and
    /// AVA has successfully re-fetched its tool list. The UI should refresh any
    /// displayed tool list and inform the user.
    MCPToolsChanged {
        /// Name of the MCP server whose tool list changed.
        server_name: String,
        /// Number of tools now registered from that server.
        tool_count: usize,
    },
    /// Streaming child event for an in-flight native sub-agent.
    SubAgentUpdate {
        /// The parent `subagent` tool call ID for this delegated run.
        call_id: String,
        /// The child event payload to merge into the transcript view.
        event: SubAgentLiveEvent,
    },
    /// A sub-agent has completed its run. Contains the full conversation for
    /// display/storage by the TUI.
    SubAgentComplete {
        /// The tool call ID that triggered this sub-agent.
        call_id: String,
        /// The sub-agent's session ID (persisted in the session store).
        session_id: String,
        /// The sub-agent's full conversation messages.
        messages: Vec<Message>,
        /// The task description/prompt given to the sub-agent.
        description: String,
        /// Total input tokens consumed by the sub-agent.
        input_tokens: usize,
        /// Total output tokens consumed by the sub-agent.
        output_tokens: usize,
        /// Estimated cost in USD for the sub-agent's LLM calls.
        cost_usd: f64,
        /// Specialist/subagent type label when known.
        agent_type: Option<String>,
        /// External provider/runtime when applicable.
        provider: Option<String>,
        /// Whether this subagent reused a previous external session.
        resumed: bool,
    },
    /// A project-state snapshot was taken before a write/edit tool execution.
    /// The TUI can use the commit hash for rewind/restore operations.
    SnapshotTaken {
        /// The git commit hash in the shadow snapshot repo.
        commit_hash: String,
        /// Human-readable label for the snapshot.
        message: String,
    },
    /// Agent completed a plan step.
    PlanStepComplete {
        step_id: String,
    },
    /// A file edit tool is streaming its arguments (in-progress indicator).
    StreamingEditProgress {
        /// Tool call ID
        call_id: String,
        /// Name of the tool (write, edit, multiedit, apply_patch)
        tool_name: String,
        /// File path being edited (extracted from partial arguments if available)
        file_path: Option<String>,
        /// Approximate progress: bytes accumulated so far
        bytes_received: usize,
    },
    /// Heartbeat emitted during persistent retry waits >30s (F23).
    RetryHeartbeat {
        /// Current retry attempt number.
        attempt: u32,
        /// Seconds remaining in the current wait period.
        wait_secs: u64,
    },
    /// The agent switched to a fallback model after consecutive overloads (F24).
    FallbackModelSwitch {
        /// The primary model that was overloaded.
        primary_model: String,
        /// The fallback model being used.
        fallback_model: String,
    },
}

impl AgentLoop {
    pub fn new(
        llm: Box<dyn LLMProvider>,
        tools: ToolRegistry,
        context: ContextManager,
        config: AgentConfig,
    ) -> Self {
        let project_root = config.project_root.clone();
        let enable_dynamic_rules = config.enable_dynamic_rules;
        let mut activated_context_paths = HashSet::new();
        if let Some(ref root) = project_root {
            if let Some(path) = crate::instructions::contextual_agents_path(root) {
                activated_context_paths.insert(path);
            }
        }
        Self {
            llm,
            tools: Arc::new(tools),
            context,
            config,
            last_request_hash: None,
            last_request_time: None,
            history: Vec::new(),
            message_queue: None,
            images: Vec::new(),
            plugin_manager: None,
            diff_tracker: crate::streaming_diff::StreamingDiffTracker::new(),
            session_logger: None,
            session_id: None,
            snapshot_manager: ava_tools::core::file_snapshot::new_shared_snapshot_manager(),
            project_root,
            activated_context_paths: std::sync::Mutex::new(activated_context_paths),
            activated_rule_paths: std::sync::Mutex::new(HashSet::new()),
            suppress_next_tokens: false,
            enable_dynamic_rules,
            cached_tool_defs: std::sync::Mutex::new(None),
            cached_hooked_tool_defs: std::sync::Mutex::new(None),
            tool_visibility_profile: crate::routing::ToolVisibilityProfile::Full,
            pre_dispatched_results: std::collections::HashMap::new(),
            attachment_state: attachment_state::AttachmentState::new(),
            trace_data_dir: None,
            trace_run_id: None,
        }
    }

    pub fn with_trace_data_dir(mut self, data_dir: std::path::PathBuf) -> Self {
        self.trace_data_dir = Some(data_dir);
        self
    }

    /// Set a pre-assigned session ID. The resulting session will use this ID
    /// instead of generating a new one via `Uuid::new_v4()`.
    pub fn with_session_id(mut self, id: uuid::Uuid) -> Self {
        self.session_id = Some(id);
        self
    }

    /// Attach a shared snapshot manager for project-state undo/rollback.
    pub fn with_snapshot_manager(
        mut self,
        manager: ava_tools::core::file_snapshot::SharedSnapshotManager,
    ) -> Self {
        self.snapshot_manager = manager;
        self
    }

    /// Attach a session logger for structured JSONL logging of each turn.
    pub fn with_session_logger(mut self, logger: crate::session_logger::SessionLogger) -> Self {
        self.session_logger = Some(logger);
        self
    }

    /// Attach a plugin manager for hook dispatch during the agent loop.
    pub fn with_plugin_manager(mut self, pm: Arc<tokio::sync::Mutex<PluginManager>>) -> Self {
        self.plugin_manager = Some(pm);
        self
    }

    pub fn with_tool_visibility_profile(
        mut self,
        profile: crate::routing::ToolVisibilityProfile,
    ) -> Self {
        self.tool_visibility_profile = profile;
        self
    }

    /// Override project instruction context for contextual AGENTS/rule loading.
    pub fn with_project_instruction_context(
        mut self,
        project_root: Option<std::path::PathBuf>,
        enable_dynamic_rules: bool,
    ) -> Self {
        self.project_root = project_root;
        self.enable_dynamic_rules = enable_dynamic_rules;
        self
    }

    fn reset_dynamic_instruction_activation(&self) {
        let mut activated_context = self
            .activated_context_paths
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        activated_context.clear();
        if let Some(ref root) = self.project_root {
            if let Some(path) = crate::instructions::contextual_agents_path(root) {
                activated_context.insert(path);
            }
        }
        self.activated_rule_paths
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clear();
    }

    async fn has_plugin_hook_subscribers(&self, event: ava_plugin::HookEvent) -> bool {
        let Some(ref pm) = self.plugin_manager else {
            return false;
        };

        pm.lock().await.has_hook_subscribers(event)
    }

    async fn ensure_snapshot_manager_initialized(&self) {
        let mut manager_guard = self.snapshot_manager.write().await;
        if manager_guard.is_some() {
            return;
        }

        let project_root = self
            .project_root
            .clone()
            .unwrap_or_else(|| std::env::current_dir().unwrap_or_default());
        match ava_tools::core::file_snapshot::SnapshotManager::new(&project_root) {
            Ok(mut mgr) => {
                if let Err(e) = mgr.init().await {
                    debug!(error = %e, "snapshot manager init failed (non-fatal)");
                } else {
                    if let Err(e) = mgr.take_snapshot("pre-write baseline").await {
                        debug!(error = %e, "initial snapshot failed (non-fatal)");
                    }
                    *manager_guard = Some(mgr);
                }
            }
            Err(e) => {
                debug!(error = %e, "snapshot manager creation failed (non-fatal)");
            }
        }
    }

    /// Set conversation history to inject after the system prompt.
    ///
    /// Runs [`ava_types::cleanup_interrupted_tools`] so that any tool calls
    /// left without results after a crash get synthetic error results,
    /// keeping the conversation valid for the LLM API.
    pub fn with_history(mut self, mut history: Vec<Message>) -> Self {
        ava_types::cleanup_interrupted_tools(&mut history);
        ava_types::repair_conversation(&mut history);
        self.history = history;
        self
    }

    /// Attach a message queue for mid-stream user messaging.
    pub fn with_message_queue(mut self, queue: MessageQueue) -> Self {
        self.message_queue = Some(queue);
        self
    }

    /// Attach images to the first user (goal) message for multimodal input.
    pub fn with_images(mut self, images: Vec<ImageContent>) -> Self {
        self.images = images;
        self
    }

    /// Get a clone of the shared snapshot manager handle.
    ///
    /// The TUI uses this to perform restore/revert operations via `/rewind`.
    pub fn snapshot_manager(&self) -> ava_tools::core::file_snapshot::SharedSnapshotManager {
        self.snapshot_manager.clone()
    }

    /// Merge token usage from a single turn into a running total.
    fn merge_usage(total: &mut TokenUsage, usage: &Option<TokenUsage>) {
        if let Some(u) = usage {
            total.input_tokens += u.input_tokens;
            total.output_tokens += u.output_tokens;
            total.cache_read_tokens += u.cache_read_tokens;
            total.cache_creation_tokens += u.cache_creation_tokens;
        }
    }

    /// Send an event to the optional event channel. No-op when headless.
    fn emit(event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>, event: AgentEvent) {
        if let Some(tx) = event_tx {
            let _ = tx.send(event);
        }
    }

    /// Broadcast an agent event to subscribed plugins via the `event` hook.
    ///
    /// Serializes the event as JSON and fires `HookEvent::Event` (notification —
    /// fire-and-forget). Plugins that subscribe to `event` can observe the full
    /// agent event stream without blocking execution.
    async fn broadcast_event_to_plugins(&self, event: &AgentEvent) {
        if !self
            .has_plugin_hook_subscribers(ava_plugin::HookEvent::Event)
            .await
        {
            return;
        }
        let Ok(payload) = serde_json::to_value(event) else {
            return;
        };
        if let Some(pm) = self.plugin_manager.as_ref() {
            pm.lock()
                .await
                .trigger_hook(ava_plugin::HookEvent::Event, payload)
                .await;
        }
    }

    fn append_run_trace(&self, kind: RunEventKind) {
        let (Some(data_dir), Some(run_id)) =
            (self.trace_data_dir.as_ref(), self.trace_run_id.as_ref())
        else {
            return;
        };

        append_trace_event(
            data_dir,
            &RunEvent {
                timestamp: std::time::SystemTime::now(),
                run_id: run_id.clone(),
                kind,
            },
        );
    }

    /// Unified agent execution engine. Both `run()` (headless) and `run_streaming()`
    /// delegate to this method. When `event_tx` is `Some`, streaming events (Token,
    /// Thinking, ToolCall, ToolResult, Progress, Complete, etc.) are emitted. When
    /// `None`, execution is silent.
    async fn run_unified(
        &mut self,
        goal: &str,
        event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<Session> {
        let run_started_at = Instant::now();
        let mut session = if let Some(id) = self.session_id {
            Session::new().with_id(id)
        } else {
            Session::new()
        };
        self.trace_run_id = Some(session.id.to_string());
        self.append_run_trace(RunEventKind::RunStarted {
            goal: goal.to_string(),
            model: self.config.model.clone(),
        });
        let loop_thresholds = crate::stuck::LoopThresholds::for_provider_model(
            &self.config.provider,
            &self.config.model,
        );
        let rep_threshold = if loop_thresholds.tool_repeat_count < 3 {
            2
        } else {
            3
        };
        let mut detector = StuckDetector::with_thresholds(loop_thresholds);
        let mut repetition_detector = RepetitionDetector::new(rep_threshold);
        let mut total_usage = TokenUsage::default();
        let mut total_cost_usd = 0.0;
        let is_subscription = self.llm.capabilities().is_subscription;

        // --- Setup ---

        *self
            .cached_tool_defs
            .lock()
            .unwrap_or_else(|error| error.into_inner()) = None;
        *self
            .cached_hooked_tool_defs
            .lock()
            .unwrap_or_else(|error| error.into_inner()) = None;
        self.inject_system_prompt().await;

        // Inject conversation history from previous turns
        for msg in std::mem::take(&mut self.history) {
            self.context.add_message(msg.clone());
            session.add_message(msg);
        }

        let goal_images = std::mem::take(&mut self.images);
        let goal_message = if goal_images.is_empty() {
            Message::new(Role::User, goal.to_string())
        } else {
            Message::new(Role::User, goal.to_string()).with_images(goal_images)
        };
        self.context.add_message(goal_message.clone());
        session.add_message(goal_message.clone());

        // --- chat.message hook (notification) ---
        if self
            .has_plugin_hook_subscribers(ava_plugin::HookEvent::ChatMessage)
            .await
        {
            if let Some(pm) = self.plugin_manager.as_ref() {
                let mut pm = pm.lock().await;
                pm.trigger_hook(
                    ava_plugin::HookEvent::ChatMessage,
                    serde_json::json!({
                        "session_id": session.id.to_string(),
                        "message": {
                            "role": "user",
                            "content": goal,
                        }
                    }),
                )
                .await;
            }
        }

        let mut turn: usize = 0;
        let mut last_turn_all_failed = false;

        // --- Main loop ---
        loop {
            // Check turn limit
            if self
                .check_turn_limit(turn, &mut session, &mut total_usage, &event_tx)
                .await
            {
                break;
            }

            // Check budget limit
            if self
                .check_budget_limit(total_cost_usd, &mut session, &mut total_usage, &event_tx)
                .await
            {
                break;
            }

            turn += 1;
            self.append_run_trace(RunEventKind::TurnStarted { turn });
            Self::emit(&event_tx, AgentEvent::Progress(format!("turn {turn}")));

            // --- Fire AgentBefore plugin hook ---
            if self
                .has_plugin_hook_subscribers(ava_plugin::HookEvent::AgentBefore)
                .await
            {
                if let Some(pm) = self.plugin_manager.as_ref() {
                    let mut pm = pm.lock().await;
                    pm.trigger_hook(
                        ava_plugin::HookEvent::AgentBefore,
                        serde_json::json!({ "turn": turn, "model": self.config.model }),
                    )
                    .await;
                }
            }

            // --- Generate LLM response (with context overflow recovery) ---
            let (response_text, tool_calls, usage) =
                self.generate_turn_response_with_recovery(&event_tx).await?;

            // --- Fire AgentAfter plugin hook ---
            if self
                .has_plugin_hook_subscribers(ava_plugin::HookEvent::AgentAfter)
                .await
            {
                if let Some(pm) = self.plugin_manager.as_ref() {
                    let mut pm = pm.lock().await;
                    pm.trigger_hook(
                        ava_plugin::HookEvent::AgentAfter,
                        serde_json::json!({
                            "turn": turn,
                            "tool_calls": tool_calls.len(),
                            "response_len": response_text.len(),
                        }),
                    )
                    .await;
                }
            }

            Self::merge_usage(&mut total_usage, &usage);
            if let Some(ref usage) = usage {
                // Subscription providers (Copilot, ChatGPT OAuth) don't bill per-token,
                // so suppress cost display by emitting 0.0.
                let cost = if is_subscription {
                    0.0
                } else {
                    usage_cost_usd(&self.config.model, usage)
                };
                total_cost_usd += cost;
                Self::emit(
                    &event_tx,
                    AgentEvent::TokenUsage {
                        input_tokens: usage.input_tokens,
                        output_tokens: usage.output_tokens,
                        cost_usd: cost,
                    },
                );
            }

            // --- Execute tools ---
            let turn_start = Instant::now();
            let (tool_results, steering_triggered, repetition_warning) = self
                .execute_tools_unified(
                    &tool_calls,
                    &mut detector,
                    &mut repetition_detector,
                    &event_tx,
                )
                .await;

            // --- Session JSONL logging ---
            if let Some(ref logger) = self.session_logger {
                let (in_tok, out_tok, cost) = match &usage {
                    Some(u) => (
                        u.input_tokens,
                        u.output_tokens,
                        usage_cost_usd(&self.config.model, u),
                    ),
                    None => (0, 0, 0.0),
                };
                let entry = crate::session_logger::SessionLogger::build_entry(
                    turn,
                    "assistant",
                    &tool_calls,
                    in_tok,
                    out_tok,
                    cost,
                    turn_start.elapsed(),
                );
                logger.log_turn(&entry);
            }

            // --- Repetition detection ---
            if let Some(ref warning) = repetition_warning {
                let mut assistant_message = Message::new(Role::Assistant, response_text.clone())
                    .with_tool_calls(tool_calls.clone());
                assistant_message.user_visible = false;
                self.context.add_message(assistant_message.clone());
                session.add_message(assistant_message);
                self.add_tool_results_internal(&tool_calls, &tool_results, &mut session);
                Self::emit(&event_tx, AgentEvent::Progress(warning.clone()));
                let mut nudge = Message::new(Role::User, warning.clone());
                nudge.user_visible = false;
                self.context.add_message(nudge.clone());
                session.add_message(nudge);
                self.suppress_next_tokens = true;
                detector.start_inject_cooldown();
                continue;
            }

            // --- Stuck detection ---
            debug!(
                text_len = response_text.len(),
                tool_calls = tool_calls.len(),
                tool_results = tool_results.len(),
                "running stuck detection"
            );
            match detector.check_with_cooldown(
                &response_text,
                &tool_calls,
                &tool_results,
                usage.as_ref(),
                &self.config,
                self.llm.as_ref(),
            ) {
                StuckAction::Continue => {}
                StuckAction::InjectMessage(msg) => {
                    let mut assistant_message =
                        Message::new(Role::Assistant, response_text.clone())
                            .with_tool_calls(tool_calls.clone());
                    assistant_message.user_visible = false;
                    self.context.add_message(assistant_message.clone());
                    session.add_message(assistant_message);
                    self.add_tool_results_internal(&tool_calls, &tool_results, &mut session);
                    Self::emit(&event_tx, AgentEvent::Progress(msg.clone()));
                    let mut nudge = Message::new(Role::User, msg);
                    nudge.user_visible = false;
                    self.context.add_message(nudge.clone());
                    session.add_message(nudge);
                    // Suppress token output for the next turn so the model's
                    // acknowledgment of the nudge doesn't leak into the visible
                    // assistant response bubble.
                    self.suppress_next_tokens = true;
                    continue;
                }
                StuckAction::Stop(reason) => {
                    Self::emit(&event_tx, AgentEvent::Progress(reason.clone()));
                    session.add_message(Message::new(Role::System, reason));
                    break;
                }
                StuckAction::NeedsJudge(context_summary) => {
                    // Layer 3: Ask the same model (fresh, no context) if the agent is stuck
                    debug!("Layer 3 LLM-as-judge triggered");
                    Self::emit(
                        &event_tx,
                        AgentEvent::Progress(
                            "checking if agent is stuck (LLM judge)...".to_string(),
                        ),
                    );
                    let judge_prompt = format!(
                        "You are an AI agent monitor. Analyze this agent's recent behavior and determine if it is stuck in a loop.\n\n\
                         {context_summary}\n\n\
                         Respond with EXACTLY one line:\n\
                         - \"STUCK: <brief reason>\" if the agent is repeating itself or making no progress\n\
                         - \"NOT_STUCK\" if the agent is making genuine progress"
                    );
                    let judge_msgs = vec![Message::new(Role::User, judge_prompt)];
                    let judge_result = tokio::time::timeout(
                        std::time::Duration::from_secs(10),
                        self.llm.generate(&judge_msgs),
                    )
                    .await;

                    match judge_result {
                        Ok(Ok(response)) => {
                            let trimmed = response.trim();
                            if let Some(reason) = trimmed.strip_prefix("STUCK:") {
                                let reason = reason.trim();
                                let msg = format!(
                                    "LLM judge determined agent is stuck: {reason}. Try a completely different approach."
                                );
                                let mut assistant_message =
                                    Message::new(Role::Assistant, response_text.clone())
                                        .with_tool_calls(tool_calls.clone());
                                assistant_message.user_visible = false;
                                self.context.add_message(assistant_message.clone());
                                session.add_message(assistant_message);
                                self.add_tool_results_internal(
                                    &tool_calls,
                                    &tool_results,
                                    &mut session,
                                );
                                Self::emit(&event_tx, AgentEvent::Progress(msg.clone()));
                                let mut nudge = Message::new(Role::User, msg);
                                nudge.user_visible = false;
                                self.context.add_message(nudge.clone());
                                session.add_message(nudge);
                                self.suppress_next_tokens = true;
                                continue;
                            }
                            // NOT_STUCK — continue normally
                        }
                        Ok(Err(e)) => {
                            debug!(error = %e, "LLM judge call failed, continuing");
                        }
                        Err(_) => {
                            debug!("LLM judge timed out after 10s, continuing");
                        }
                    }
                }
            }

            // Response passed stuck detection — this is a productive turn.
            // Reset token suppression so output streams normally again.
            self.suppress_next_tokens = false;

            // --- Empty response handling ---
            if response_text.trim().is_empty() && tool_calls.is_empty() {
                let msg = format!(
                    "Provider returned empty response (model: {}, turn {}). \
                     Possible API format mismatch. Run with RUST_LOG=debug for details.",
                    self.config.model, turn
                );
                warn!("{msg}");
                Self::emit(&event_tx, AgentEvent::Error(msg));
                self.last_request_hash = None;
                self.last_request_time = None;
                break;
            }

            let mut assistant_message = Message::new(Role::Assistant, response_text.clone())
                .with_tool_calls(tool_calls.clone());
            // When suppress_next_tokens is active, this is the model's internal
            // acknowledgment of a stuck-detector nudge — hide it from the UI.
            if self.suppress_next_tokens {
                assistant_message.user_visible = false;
            }
            self.context.add_message(assistant_message.clone());
            session.add_message(assistant_message);

            // Natural completion: non-empty text with no tool calls = final answer
            // BUT first check the steering queue — the user may have sent a message
            // while we were waiting for the LLM response.
            if tool_calls.is_empty() {
                // Failure-aware completion guard: if ALL tools failed last turn,
                // nudge the agent to retry instead of silently completing.
                if last_turn_all_failed && turn < self.effective_max_turns() {
                    last_turn_all_failed = false; // fire once to avoid infinite nudge loop
                    let mut nudge = Message::new(
                        Role::User,
                        "Your previous tool calls all failed. Review the errors above and try a different approach to complete the task.".to_string(),
                    );
                    nudge.user_visible = false;
                    self.context.add_message(nudge.clone());
                    session.add_message(nudge);
                    Self::emit(
                        &event_tx,
                        AgentEvent::Progress(
                            "nudging agent to retry after all tool calls failed".to_string(),
                        ),
                    );
                    continue;
                }

                if self
                    .handle_natural_completion(
                        &response_text,
                        &mut session,
                        total_usage.clone(),
                        &detector,
                        &event_tx,
                    )
                    .await
                {
                    return Ok(session);
                }
                // Steering was injected — continue to next turn
                continue;
            }

            // Add tool results to context
            self.add_tool_results(&tool_calls, &tool_results, &mut session);

            // Track whether ALL tool calls failed (for failure-aware completion guard)
            last_turn_all_failed =
                !tool_results.is_empty() && tool_results.iter().all(|r| r.is_error);

            // Checkpoint: emit the session state so callers can persist progress.
            // If the process exits before the final Complete event, this is recoverable.
            {
                let mut checkpoint = session.clone();
                checkpoint.token_usage = total_usage.clone();
                Self::emit(&event_tx, AgentEvent::Checkpoint(checkpoint));
            }

            // Inject a self-correction hint for the first error (skip if steering overrides)
            if !steering_triggered {
                if let Some(err_result) = tool_results
                    .iter()
                    .find(|result| result.is_error || has_validation_failure(result))
                {
                    let (prefix, first_line) = correction_hint_parts(err_result);
                    let smart = crate::error_hints::smart_error_hint(&err_result.content);
                    let suffix =
                        smart.unwrap_or("Try a different approach — don't repeat the same call.");
                    let hint = format!("{prefix}: {first_line}. {suffix}");
                    let hint_msg = Message::new(Role::User, hint);
                    self.context.add_message(hint_msg.clone());
                    session.add_message(hint_msg);
                }
            }

            // Steering injection: if steering was triggered, inject all steering
            // messages as a single user turn and skip to the next LLM call.
            if steering_triggered {
                self.inject_steering_messages(&mut session, &event_tx);
                continue;
            }

            self.run_auto_compaction_phase(&session, &event_tx).await;

            if self
                .handle_attempt_completion(
                    &tool_calls,
                    &mut session,
                    total_usage.clone(),
                    &detector,
                    &event_tx,
                )
                .await
            {
                return Ok(session);
            }
        }

        // --- Cleanup ---
        self.append_run_trace(RunEventKind::RunCompleted {
            turns: turn,
            total_ms: run_started_at.elapsed().as_millis() as u64,
        });
        self.emit_final_completion(&mut session, total_usage, &detector, &event_tx)
            .await;
        Ok(session)
    }

    /// Headless (non-streaming) execution. Delegates to the unified engine with no event sink.
    #[instrument(skip(self), fields(model = %self.config.model))]
    pub async fn run(&mut self, goal: &str) -> ava_types::Result<Session> {
        self.run_unified(goal, None).await
    }

    /// Execution with an explicit event sink supplied by the caller.
    #[instrument(skip(self, event_tx), fields(model = %self.config.model))]
    pub async fn run_with_event_tx(
        &mut self,
        goal: &str,
        event_tx: mpsc::UnboundedSender<AgentEvent>,
    ) -> ava_types::Result<Session> {
        self.run_unified(goal, Some(event_tx)).await
    }

    /// Streaming execution. Delegates to the unified engine with an event channel,
    /// returning the receiver wrapped as a stream for backward compatibility.
    #[instrument(skip(self), fields(model = %self.config.model))]
    pub async fn run_streaming(
        &mut self,
        goal: &str,
    ) -> Pin<Box<dyn Stream<Item = AgentEvent> + Send + '_>> {
        let (tx, rx) = mpsc::unbounded_channel();
        let goal = goal.to_string();

        // Run the unified engine inline (not spawned) so we can borrow `self`.
        // We use async_stream to drive the unified engine and yield events from
        // the receiver in lock-step.
        Box::pin(async_stream::stream! {
            // Start the unified engine — it will send events to `tx`.
            // We run it as a future and poll the receiver for events.
            let mut engine_fut = std::pin::pin!(self.run_unified(&goal, Some(tx)));
            let mut rx = rx;
            let mut engine_done = false;

            loop {
                if engine_done {
                    // Drain remaining events from the channel after engine completes
                    while let Ok(event) = rx.try_recv() {
                        yield event;
                    }
                    break;
                }

                tokio::select! {
                    biased;
                    // Prefer draining events before polling the engine again
                    event = rx.recv() => {
                        match event {
                            Some(event) => yield event,
                            None => break, // channel closed
                        }
                    }
                    result = &mut engine_fut => {
                        engine_done = true;
                        // If engine errored and didn't emit Error event, emit one now
                        if let Err(error) = result {
                            yield AgentEvent::Error(error.to_string());
                        }
                        // Continue to drain remaining events
                    }
                }
            }
        })
    }
}

fn correction_hint_parts(result: &ToolResult) -> (&'static str, &str) {
    if result.is_error {
        (
            "Tool call failed",
            result.content.lines().next().unwrap_or("unknown error"),
        )
    } else {
        (
            "Post-edit validation failed",
            result
                .content
                .lines()
                .find(|line| line.starts_with("- "))
                .unwrap_or("validation failed"),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::hash_map::DefaultHasher;
    use std::fs;
    use std::hash::{Hash, Hasher};

    use ava_llm::providers::mock::MockProvider;
    use tempfile::TempDir;

    use crate::stuck::StuckAction;

    #[test]
    fn stuck_detector_empty_responses() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 1.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };
        let llm = crate::tests::mock_llm();

        // First empty: continue
        let action = detector.check("", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));

        // Second empty: stop
        let action = detector.check("", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_identical_responses() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };
        let llm = crate::tests::mock_llm();

        for i in 0..2 {
            let action = detector.check("same response", &[], &[], None, &config, llm.as_ref());
            assert!(
                matches!(action, StuckAction::Continue),
                "iteration {i} should continue"
            );
        }

        let action = detector.check("same response", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_tool_call_loop() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };
        let llm = crate::tests::mock_llm();

        let call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };

        for i in 0..2 {
            let action = detector.check(
                &format!("reading {i}"),
                std::slice::from_ref(&call),
                &[],
                None,
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check(
            "reading again",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    #[test]
    fn stuck_detector_error_loop() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };
        let llm = crate::tests::mock_llm();

        let error_result = ToolResult {
            call_id: "1".to_string(),
            content: "file not found".to_string(),
            is_error: true,
        };

        for i in 0..2 {
            let action = detector.check(
                &format!("trying {i}"),
                &[],
                std::slice::from_ref(&error_result),
                None,
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check(
            "trying again",
            &[],
            std::slice::from_ref(&error_result),
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    #[test]
    fn stuck_detector_cost_threshold() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 0.0, // Zero threshold = immediate stop
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };
        let llm = crate::tests::mock_llm();

        let action = detector.check("hello", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_disabled() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 0.0,
            loop_detection: false,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };
        let llm = crate::tests::mock_llm();

        // Would normally trigger cost stop, but detection is disabled
        let action = detector.check("hello", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));
    }

    #[test]
    fn read_only_tools_constant_is_populated() {
        assert!(!READ_ONLY_TOOLS.is_empty());
        assert!(READ_ONLY_TOOLS.contains(&"read"));
        assert!(READ_ONLY_TOOLS.contains(&"glob"));
        assert!(READ_ONLY_TOOLS.contains(&"grep"));
        // Write tools should NOT be in the list
        assert!(!READ_ONLY_TOOLS.contains(&"write"));
        assert!(!READ_ONLY_TOOLS.contains(&"bash"));
        assert!(!READ_ONLY_TOOLS.contains(&"edit"));
    }

    #[test]
    fn dedup_guard_skips_rapid_duplicate() {
        // Verify the hash mechanism works deterministically
        let mut hasher1 = DefaultHasher::new();
        "same content".hash(&mut hasher1);
        let h1 = hasher1.finish();

        let mut hasher2 = DefaultHasher::new();
        "same content".hash(&mut hasher2);
        let h2 = hasher2.finish();

        assert_eq!(h1, h2, "same content should produce same hash");

        let mut hasher3 = DefaultHasher::new();
        "different content".hash(&mut hasher3);
        let h3 = hasher3.finish();

        assert_ne!(h1, h3, "different content should produce different hash");
    }

    #[test]
    fn token_usage_event_serializes() {
        let event = AgentEvent::TokenUsage {
            input_tokens: 1000,
            output_tokens: 200,
            cost_usd: 0.015,
        };
        let json = serde_json::to_string(&event).unwrap();
        assert!(json.contains("1000"));
        assert!(json.contains("200"));
        assert!(json.contains("0.015"));
    }

    #[test]
    fn agent_loop_preseeds_workspace_agents_and_skips_reinjecting_it() {
        let tmp = TempDir::new().unwrap();
        let src_dir = tmp.path().join("src");
        fs::create_dir_all(&src_dir).unwrap();
        fs::write(tmp.path().join("AGENTS.md"), "Root guidance.").unwrap();
        fs::write(src_dir.join("main.rs"), "fn main() {}\n").unwrap();

        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: Some(tmp.path().to_path_buf()),
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };

        let agent = AgentLoop::new(
            Box::new(MockProvider::new("mock", vec![])),
            ToolRegistry::new(),
            ContextManager::new(4_096),
            config,
        );

        let root_agents = fs::canonicalize(tmp.path().join("AGENTS.md")).unwrap();
        {
            let activated = agent
                .activated_context_paths
                .lock()
                .unwrap_or_else(|error| error.into_inner());
            assert!(activated.contains(&root_agents));
        }

        let mut activated = agent
            .activated_context_paths
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        let result = crate::instructions::contextual_instructions_for_file_once(
            &src_dir.join("main.rs"),
            tmp.path(),
            &mut activated,
        );
        assert!(
            result.is_none(),
            "startup-loaded workspace AGENTS.md should not be re-injected on first file touch"
        );
    }

    #[test]
    fn reset_dynamic_instruction_activation_reseeds_workspace_agents() {
        let tmp = TempDir::new().unwrap();
        fs::write(tmp.path().join("AGENTS.md"), "Root guidance.").unwrap();

        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: Some(tmp.path().to_path_buf()),
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        };

        let agent = AgentLoop::new(
            Box::new(MockProvider::new("mock", vec![])),
            ToolRegistry::new(),
            ContextManager::new(4_096),
            config,
        );

        let root_agents = fs::canonicalize(tmp.path().join("AGENTS.md")).unwrap();
        {
            let mut activated = agent
                .activated_context_paths
                .lock()
                .unwrap_or_else(|error| error.into_inner());
            activated.clear();
            activated.insert(tmp.path().join("other"));
        }

        agent.reset_dynamic_instruction_activation();

        let activated = agent
            .activated_context_paths
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        assert_eq!(activated.len(), 1);
        assert!(activated.contains(&root_agents));
    }

    // --- Plan mode tests ---

    #[test]
    fn plan_mode_allows_write_to_plan_dir() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": ".ava/plans/my-plan.md", "content": "# Plan"}),
        };
        assert!(check_plan_mode_tool(&tc).is_none());
    }

    #[test]
    fn plan_mode_allows_write_to_nested_plan_dir() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": "/home/user/project/.ava/plans/refactor.md", "content": "# Plan"}),
        };
        assert!(check_plan_mode_tool(&tc).is_none());
    }

    #[test]
    fn plan_mode_blocks_write_to_source_files() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": "src/main.rs", "content": "fn main() {}"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
        assert!(result.unwrap().contains("Plan mode"));
    }

    #[test]
    fn plan_mode_blocks_write_to_non_md_in_plan_dir() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": ".ava/plans/script.sh", "content": "#!/bin/bash"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
        assert!(result.unwrap().contains("Plan mode"));
    }

    #[test]
    fn plan_mode_blocks_destructive_bash() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({"command": "rm -rf /"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
        assert!(result.unwrap().contains("Plan mode"));
    }

    #[test]
    fn plan_mode_allows_readonly_bash() {
        use tool_execution::check_plan_mode_tool;
        for cmd in &[
            "ls -la",
            "cat README.md",
            "git status",
            "git log --oneline -10",
            "cargo test --workspace",
        ] {
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": cmd}),
            };
            assert!(
                check_plan_mode_tool(&tc).is_none(),
                "bash '{cmd}' should be allowed in plan mode"
            );
        }
    }

    #[test]
    fn plan_mode_blocks_high_risk_bash() {
        use tool_execution::check_plan_mode_tool;
        for cmd in &[
            "git push --force origin main",
            "npm publish",
            "docker rm container_id",
        ] {
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": cmd}),
            };
            assert!(
                check_plan_mode_tool(&tc).is_some(),
                "bash '{cmd}' should be blocked in plan mode"
            );
        }
    }

    #[test]
    fn plan_mode_allows_read_tools() {
        use tool_execution::check_plan_mode_tool;
        for tool_name in &[
            "read",
            "glob",
            "grep",
            "codebase_search",
            "todo_read",
            "web_fetch",
            "web_search",
            "git",
            "plan",
            "question",
            "memory_read",
        ] {
            let tc = ToolCall {
                id: "1".to_string(),
                name: tool_name.to_string(),
                arguments: serde_json::json!({"path": "src/main.rs"}),
            };
            assert!(
                check_plan_mode_tool(&tc).is_none(),
                "{tool_name} should be allowed in plan mode"
            );
        }
    }

    #[test]
    fn plan_mode_blocks_edit_to_source() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "edit".to_string(),
            arguments: serde_json::json!({"path": "src/lib.rs", "old_string": "a", "new_string": "b"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
    }

    #[test]
    fn plan_mode_allows_attempt_completion() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "attempt_completion".to_string(),
            arguments: serde_json::json!({"result": "Plan complete."}),
        };
        assert!(check_plan_mode_tool(&tc).is_none());
    }

    #[test]
    fn is_plan_path_validates_correctly() {
        use tool_execution::is_plan_path;
        assert!(is_plan_path(".ava/plans/my-plan.md"));
        assert!(is_plan_path("/home/user/.ava/plans/refactor.md"));
        assert!(!is_plan_path(".ava/plans/script.sh"));
        assert!(!is_plan_path("src/main.rs"));
        assert!(!is_plan_path(".ava/config.toml"));
        assert!(!is_plan_path(".ava/plans/")); // no filename
    }

    // --- Tool schema pre-validation tests ---

    mod validate_tool_call_tests {
        use super::*;
        use ava_tools::registry::{Tool as ToolTrait, ToolRegistry};
        use tool_execution::validate_tool_call;

        /// Minimal tool for testing schema validation without platform dependencies.
        struct FakeBashTool;

        #[async_trait::async_trait]
        impl ToolTrait for FakeBashTool {
            fn name(&self) -> &str {
                "bash"
            }
            fn description(&self) -> &str {
                "Run a shell command"
            }
            fn parameters(&self) -> serde_json::Value {
                serde_json::json!({
                    "type": "object",
                    "required": ["command"],
                    "properties": {
                        "command": { "type": "string" },
                        "timeout_ms": { "type": "integer", "minimum": 1 },
                        "cwd": { "type": "string" }
                    }
                })
            }
            async fn execute(&self, _args: serde_json::Value) -> ava_types::Result<ToolResult> {
                Ok(ToolResult {
                    call_id: String::new(),
                    content: String::new(),
                    is_error: false,
                })
            }
        }

        fn registry_with_bash() -> ToolRegistry {
            let mut registry = ToolRegistry::new();
            registry.register(FakeBashTool);
            registry
        }

        #[test]
        fn valid_tool_call_passes() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "echo hello"}),
            };
            assert!(validate_tool_call(&tc, &registry).is_none());
        }

        #[test]
        fn unknown_tool_fails() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "nonexistent".to_string(),
                arguments: serde_json::json!({}),
            };
            let err = validate_tool_call(&tc, &registry);
            assert!(err.is_some());
            assert!(err.unwrap().contains("not found"));
        }

        #[test]
        fn missing_required_param_fails() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({}),
            };
            let err = validate_tool_call(&tc, &registry);
            assert!(err.is_some());
            let msg = err.unwrap();
            assert!(
                msg.contains("missing required parameter 'command'"),
                "got: {msg}"
            );
        }

        #[test]
        fn wrong_type_fails() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": 42}),
            };
            let err = validate_tool_call(&tc, &registry);
            assert!(err.is_some());
            let msg = err.unwrap();
            assert!(msg.contains("expected type 'string'"), "got: {msg}");
        }

        #[test]
        fn null_args_with_required_params_fails() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::Value::Null,
            };
            let err = validate_tool_call(&tc, &registry);
            assert!(err.is_some());
            assert!(err.unwrap().contains("Missing required parameter"));
        }

        #[test]
        fn optional_params_with_wrong_type_fails() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "ls", "timeout_ms": "not a number"}),
            };
            let err = validate_tool_call(&tc, &registry);
            assert!(err.is_some());
            let msg = err.unwrap();
            assert!(msg.contains("expected type 'integer'"), "got: {msg}");
        }

        #[test]
        fn extra_params_are_allowed() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::json!({"command": "ls", "unknown_param": "value"}),
            };
            assert!(validate_tool_call(&tc, &registry).is_none());
        }

        #[test]
        fn string_arguments_are_parsed_and_validated() {
            let registry = registry_with_bash();
            // Arguments as a raw JSON string (some providers send this format)
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::Value::String(r#"{"command": "echo hello"}"#.to_string()),
            };
            assert!(validate_tool_call(&tc, &registry).is_none());
        }

        #[test]
        fn string_arguments_missing_required_still_fails() {
            let registry = registry_with_bash();
            let tc = ToolCall {
                id: "1".to_string(),
                name: "bash".to_string(),
                arguments: serde_json::Value::String(r#"{}"#.to_string()),
            };
            let err = validate_tool_call(&tc, &registry);
            assert!(err.is_some());
            assert!(err.unwrap().contains("missing required parameter"));
        }
    }
}
