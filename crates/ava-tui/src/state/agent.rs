use crate::event::AppEvent;
use ava_agent::stack::{AgentRunResult, AgentStack, AgentStackConfig};
use ava_types::{QueuedMessage, ThinkingLevel};
use color_eyre::Result;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Instant;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

#[derive(Debug, Default, Clone)]
pub struct TokenUsage {
    pub input: usize,
    pub output: usize,
}

/// Tracks the state of a sub-agent spawned by the task tool.
#[derive(Debug, Clone)]
pub struct SubAgentInfo {
    pub description: String,
    pub is_running: bool,
    pub tool_count: usize,
    pub current_tool: Option<String>,
    pub started_at: Instant,
    /// Duration of completed sub-agents (set when `is_running` becomes false).
    pub elapsed: Option<std::time::Duration>,
    /// The sub-agent's session ID (set on completion via `SubAgentComplete` event).
    pub session_id: Option<String>,
    /// The sub-agent's full conversation as UI messages (set on completion).
    pub session_messages: Vec<crate::state::messages::UiMessage>,
    /// Provider powering this sub-agent (e.g. "claude-code"). `None` means native AVA.
    pub provider: Option<String>,
}

/// Agent execution mode — determines tool access and prompt behavior.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum AgentMode {
    #[default]
    Code, // Full tool access, standard execution
    Plan, // Read-only tools only, analysis/planning
}

impl AgentMode {
    pub fn label(&self) -> &'static str {
        match self {
            Self::Code => "Code",
            Self::Plan => "Plan",
        }
    }

    pub fn cycle_next(&self) -> Self {
        match self {
            Self::Code => Self::Plan,
            Self::Plan => Self::Code,
        }
    }

    pub fn cycle_prev(&self) -> Self {
        self.cycle_next()
    }

    /// Returns mode-specific system prompt suffix, or None for Code mode.
    pub fn prompt_suffix(&self) -> Option<&'static str> {
        match self {
            Self::Code => None,
            Self::Plan => Some(
                "You are in Plan mode. You can analyze code, create plans, and write plan documents \
                 to .ava/plans/*.md. You cannot modify source code files or run destructive commands. \
                 Use read, glob, grep, and todo_read freely. You may ONLY use the write tool to create \
                 files under the .ava/plans/ directory with a .md extension.\n\n\
                 When creating a plan, structure it with:\n\
                 - **Goal**: What the plan aims to achieve\n\
                 - **Analysis**: Current state and findings\n\
                 - **Steps**: Numbered implementation steps\n\
                 - **Files to modify**: List of files that will need changes\n\
                 - **Risks**: Potential issues and mitigations\n\n\
                 When your plan is complete, the user can switch to Code mode to implement it."
            ),
        }
    }
}

#[derive(Debug, Clone, Default)]
pub enum AgentActivity {
    #[default]
    Idle,
    Thinking,
    ExecutingTool(String),
}

impl std::fmt::Display for AgentActivity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Idle => write!(f, "idle"),
            Self::Thinking => write!(f, "thinking"),
            Self::ExecutingTool(name) => write!(f, "running {name}"),
        }
    }
}

pub struct AgentState {
    stack: Option<Arc<AgentStack>>,
    pub is_running: bool,
    pub current_turn: usize,
    pub max_turns: usize,
    pub tokens_used: TokenUsage,
    pub cost: f64,
    pub activity: AgentActivity,
    pub provider_name: String,
    pub model_name: String,
    /// Context window size for the current model (from registry).
    pub context_window: Option<usize>,
    pub mcp_server_count: usize,
    pub mcp_tool_count: usize,
    pub tool_start: Option<Instant>,
    /// Workflow phase: (current_index, total_count, phase_name)
    pub workflow_phase: Option<(usize, usize, String)>,
    /// Workflow iteration: (current, max)
    pub workflow_iteration: Option<(usize, usize)>,
    /// Recently used models (most recent first, max 5).
    pub recent_models: Vec<String>,
    /// Current thinking/reasoning level.
    pub thinking_level: ThinkingLevel,
    /// Sub-agents spawned by the task tool.
    pub sub_agents: Vec<SubAgentInfo>,
    cancel: Option<CancellationToken>,
    task: Option<tokio::task::JoinHandle<()>>,
    /// Sender for mid-stream user messages (steering, follow-up, post-complete).
    /// Set when the agent is started; cleared when it finishes.
    pub message_tx: Option<mpsc::UnboundedSender<QueuedMessage>>,
}

/// Look up context window for a model from the compiled-in registry.
/// Tries provider-specific lookup first, then falls back to model-only lookup.
fn lookup_context_window(provider: &str, model: &str) -> Option<usize> {
    let reg = ava_config::model_catalog::registry::registry();
    // For openrouter models like "anthropic/claude-sonnet-4", strip the prefix
    let model_id = model.rsplit_once('/').map(|(_, m)| m).unwrap_or(model);
    // Try provider-specific match first (handles copilot, coding plan providers, etc.)
    if let Some(m) = reg.find_for_provider(provider, model_id) {
        return Some(m.limits.context_window);
    }
    // Fall back to global search (works for openrouter where provider is "openrouter"
    // but the model ID contains the real provider)
    if let Some(m) = reg.find(model_id) {
        return Some(m.limits.context_window);
    }
    None
}

impl AgentState {
    pub async fn new(
        data_dir: PathBuf,
        provider: Option<String>,
        model: Option<String>,
        max_turns: usize,
        yolo: bool,
    ) -> Result<(
        Self,
        tokio::sync::mpsc::UnboundedReceiver<ava_tools::core::question::QuestionRequest>,
    )> {
        let provider_name = provider.clone().unwrap_or_else(|| "default".to_string());
        let model_name = model.clone().unwrap_or_else(|| "default".to_string());

        let config = AgentStackConfig {
            data_dir,
            provider,
            model,
            max_turns,
            yolo,
            ..AgentStackConfig::default()
        };
        let (agent_stack, question_rx) = AgentStack::new(config).await?;
        let stack = Arc::new(agent_stack);

        let mcp_server_count = stack.mcp_server_count().await;
        let mcp_tool_count = stack.mcp_tool_count().await;

        let context_window = lookup_context_window(&provider_name, &model_name);

        Ok((
            Self {
                stack: Some(stack),
                is_running: false,
                current_turn: 0,
                max_turns,
                tokens_used: TokenUsage::default(),
                cost: 0.0,
                activity: AgentActivity::Idle,
                provider_name,
                model_name,
                context_window,
                mcp_server_count,
                mcp_tool_count,
                tool_start: None,
                workflow_phase: None,
                workflow_iteration: None,
                recent_models: Vec::new(),
                thinking_level: ThinkingLevel::Off,
                sub_agents: Vec::new(),
                cancel: None,
                task: None,
                message_tx: None,
            },
            question_rx,
        ))
    }

    pub(crate) fn stack(&self) -> std::result::Result<&Arc<AgentStack>, String> {
        self.stack
            .as_ref()
            .ok_or_else(|| "AgentStack not initialised".to_string())
    }

    pub(crate) fn stack_handle(&self) -> Option<Arc<AgentStack>> {
        self.stack.as_ref().map(Arc::clone)
    }

    /// Get the shared todo state from the agent stack (if initialized).
    pub fn todo_state(&self) -> Option<ava_types::TodoState> {
        self.stack.as_ref().map(|s| s.todo_state.clone())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn start(
        &mut self,
        run_id: u64,
        goal: String,
        max_turns: usize,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        history: Vec<ava_types::Message>,
        parent_session_id: Option<String>,
        images: Vec<ava_types::ImageContent>,
    ) {
        let Some(stack) = self.stack.as_ref().map(Arc::clone) else {
            // No AgentStack (test mode) — mark running state but skip spawn
            self.is_running = true;
            self.current_turn = 0;
            self.max_turns = max_turns;
            self.activity = AgentActivity::Thinking;
            return;
        };

        let cancel = CancellationToken::new();
        let run_cancel = cancel.clone();

        self.is_running = true;
        self.current_turn = 0;
        self.max_turns = max_turns;
        self.activity = AgentActivity::Thinking;

        // Create message queue for mid-stream messaging
        let (message_queue, message_sender) = stack.create_message_queue();
        self.message_tx = Some(message_sender);

        self.task = Some(tokio::spawn(async move {
            let (agent_tx, mut agent_rx) = mpsc::unbounded_channel();
            let app_events = app_tx.clone();
            let relay = tokio::spawn(async move {
                while let Some(event) = agent_rx.recv().await {
                    let _ = app_events.send(AppEvent::AgentRunEvent { run_id, event });
                }
            });

            // Set parent session ID so sub-agents can link back to this session
            if let Some(pid) = parent_session_id {
                *stack.parent_session_id.write().await = Some(pid);
            }
            let result = stack
                .run(
                    &goal,
                    max_turns,
                    Some(agent_tx),
                    run_cancel,
                    history,
                    Some(message_queue),
                    images,
                )
                .await;
            let _ = relay.await;
            let mapped = result.map_err(|err| err.to_string());
            let _ = app_tx.send(AppEvent::AgentRunDone {
                run_id,
                result: mapped,
            });
        }));
        self.cancel = Some(cancel);
    }

    pub fn abort(&mut self) {
        if let Some(cancel) = self.cancel.take() {
            cancel.cancel();
        }
        if let Some(task) = self.task.take() {
            task.abort();
        }
        self.is_running = false;
        self.activity = AgentActivity::Idle;
        self.message_tx = None;
    }

    pub fn finish(&mut self, _result: &AgentRunResult) {
        self.is_running = false;
        self.activity = AgentActivity::Idle;
        self.cancel = None;
        self.task = None;
        self.message_tx = None;
    }

    pub fn detach_run(&mut self) {
        self.is_running = false;
        self.activity = AgentActivity::Idle;
        self.cancel = None;
        self.task = None;
        self.message_tx = None;
        self.tool_start = None;
        self.workflow_phase = None;
        self.workflow_iteration = None;
    }

    /// Switch model at runtime. Returns Ok(description) or Err(message).
    pub async fn switch_model(
        &mut self,
        provider: &str,
        model: &str,
    ) -> std::result::Result<String, String> {
        self.stack()?
            .switch_model(provider, model)
            .await
            .map_err(|e| e.to_string())?;
        self.provider_name = provider.to_string();
        self.model_name = model.to_string();
        self.context_window = lookup_context_window(provider, model);

        // Track in recent models (most recent first, max 5)
        let key = format!("{provider}/{model}");
        self.recent_models.retain(|m| m != &key);
        self.recent_models.insert(0, key);
        self.recent_models.truncate(5);

        Ok(format!("{provider}/{model}"))
    }

    pub fn apply_switched_model(&mut self, provider: &str, model: &str) -> String {
        self.provider_name = provider.to_string();
        self.model_name = model.to_string();
        self.context_window = lookup_context_window(provider, model);

        let key = format!("{provider}/{model}");
        self.recent_models.retain(|m| m != &key);
        self.recent_models.insert(0, key);
        self.recent_models.truncate(5);

        format!("{provider}/{model}")
    }

    /// Get current provider/model description.
    pub fn current_model_display(&self) -> String {
        format!("{}/{}", self.provider_name, self.model_name)
    }

    /// Get MCP server info for display.
    pub async fn mcp_server_info(
        &self,
    ) -> std::result::Result<Vec<ava_agent::stack::MCPServerInfo>, String> {
        Ok(self.stack()?.mcp_server_info().await)
    }

    /// Reload MCP servers from config. Updates cached counts.
    pub async fn reload_mcp(&mut self) -> std::result::Result<String, String> {
        let (servers, tools) = self
            .stack()?
            .reload_mcp()
            .await
            .map_err(|e| e.to_string())?;
        self.mcp_server_count = servers;
        self.mcp_tool_count = tools;
        Ok(format!("MCP reloaded: {servers} servers, {tools} tools"))
    }

    /// Reload all tools (core + custom + MCP). Updates cached counts.
    pub async fn reload_tools(&mut self) -> std::result::Result<String, String> {
        let stack = self.stack()?.clone();
        let count = stack.reload_tools().await.map_err(|e| e.to_string())?;
        self.mcp_server_count = stack.mcp_server_count().await;
        self.mcp_tool_count = stack.mcp_tool_count().await;
        Ok(format!("Reloaded {count} tools"))
    }

    /// Get tool list with source info.
    pub async fn list_tools_with_source(
        &self,
    ) -> std::result::Result<Vec<(ava_types::Tool, ava_tools::registry::ToolSource)>, String> {
        Ok(self.stack()?.tools.read().await.list_tools_with_source())
    }

    /// Set thinking level and sync to agent stack.
    pub fn set_thinking_level(&mut self, level: ThinkingLevel) {
        self.thinking_level = level;
        if let Some(stack) = &self.stack {
            let stack = stack.clone();
            tokio::spawn(async move {
                stack.set_thinking_level(level).await;
            });
        }
    }

    /// Cycle thinking level and sync to agent stack. Returns the new level's label.
    pub fn cycle_thinking(&mut self) -> &'static str {
        self.thinking_level = self.thinking_level.cycle();
        if let Some(stack) = &self.stack {
            let stack = stack.clone();
            let level = self.thinking_level;
            tokio::spawn(async move {
                stack.set_thinking_level(level).await;
            });
        }
        self.thinking_level.label()
    }

    /// Check if the current model likely supports thinking/reasoning.
    pub fn model_supports_thinking(&self) -> bool {
        let m = self.model_name.to_lowercase();
        m.contains("claude")
            || m.contains("gpt-5")
            || m.contains("gemini-2.5")
            || m.contains("gemini-3")
            || m.starts_with("o3")
            || m.starts_with("o4")
            || m.contains("deepseek-r1")
            || m.contains("qwq")
            || m.contains("kimi")
    }

    /// Check if the current model supports granular thinking levels (low/med/high/max).
    /// Models that think natively (e.g. Kimi, DeepSeek-R1) don't accept level parameters.
    pub fn model_supports_thinking_levels(&self) -> bool {
        let m = self.model_name.to_lowercase();
        m.contains("claude")
            || m.contains("gpt-5")
            || m.contains("gemini-2.5")
            || m.contains("gemini-3")
            || m.starts_with("o3")
            || m.starts_with("o4")
    }

    /// Create a lightweight `AgentState` without an `AgentStack` (for tests).
    #[doc(hidden)]
    pub fn test_new(provider: &str, model: &str) -> Self {
        Self {
            stack: None,
            is_running: false,
            current_turn: 0,
            max_turns: 0,
            tokens_used: TokenUsage::default(),
            cost: 0.0,
            activity: AgentActivity::Idle,
            provider_name: provider.to_string(),
            model_name: model.to_string(),
            context_window: lookup_context_window(provider, model),
            mcp_server_count: 0,
            mcp_tool_count: 0,
            tool_start: None,
            workflow_phase: None,
            workflow_iteration: None,
            recent_models: Vec::new(),
            thinking_level: ThinkingLevel::Off,
            sub_agents: Vec::new(),
            cancel: None,
            task: None,
            message_tx: None,
        }
    }

    /// Sync the agent mode prompt suffix and plan_mode flag to the stack.
    pub fn set_mode(&self, mode: super::agent::AgentMode) {
        let suffix = mode.prompt_suffix().map(|s| s.to_string());
        let is_plan = matches!(mode, super::agent::AgentMode::Plan);
        if let Some(stack) = &self.stack {
            let stack = stack.clone();
            tokio::spawn(async move {
                stack.set_mode_prompt_suffix(suffix).await;
                stack.set_plan_mode(is_plan).await;
            });
        }
    }

    /// Create tool templates in project .ava/tools directory.
    pub fn create_tool_templates(&self) -> std::result::Result<String, String> {
        let dir = std::env::current_dir()
            .unwrap_or_default()
            .join(".ava")
            .join("tools");
        let created =
            ava_tools::core::custom_tool::create_tool_templates(&dir).map_err(|e| e.to_string())?;
        if created.is_empty() {
            Ok("Templates already exist in .ava/tools/".to_string())
        } else {
            let names: Vec<_> = created
                .iter()
                .filter_map(|p| p.file_name())
                .map(|n| n.to_string_lossy().to_string())
                .collect();
            Ok(format!("Created templates: {}", names.join(", ")))
        }
    }
}
