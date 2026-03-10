use crate::event::AppEvent;
use ava_agent::stack::{AgentRunResult, AgentStack, AgentStackConfig};
use ava_types::ThinkingLevel;
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

/// Agent execution mode — determines tool access and prompt behavior.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum AgentMode {
    #[default]
    Code,      // Full tool access, standard execution
    Plan,      // Read-only tools only, analysis/planning
    Architect, // Plan first, then hand off to code (future)
}

impl AgentMode {
    pub fn label(&self) -> &'static str {
        match self {
            Self::Code => "Code",
            Self::Plan => "Plan",
            Self::Architect => "Architect",
        }
    }

    pub fn cycle_next(&self) -> Self {
        match self {
            Self::Code => Self::Plan,
            Self::Plan => Self::Architect,
            Self::Architect => Self::Code,
        }
    }

    pub fn cycle_prev(&self) -> Self {
        match self {
            Self::Code => Self::Architect,
            Self::Plan => Self::Code,
            Self::Architect => Self::Plan,
        }
    }

    /// Returns mode-specific system prompt suffix, or None for Code mode.
    pub fn prompt_suffix(&self) -> Option<&'static str> {
        match self {
            Self::Code => None,
            Self::Plan => Some(
                "You are in PLAN MODE (read-only). You may ONLY use read-only tools: \
                 read, glob, grep, codebase_search, diagnostics, session_search, session_list, \
                 recall, memory_search. You MUST NOT modify any files, execute commands, or make \
                 changes. Focus on analysis, research, and creating a plan."
            ),
            Self::Architect => Some(
                "You are in ARCHITECT MODE. First, analyze the codebase and create a detailed \
                 implementation plan. Present the plan to the user. Do not implement changes \
                 until the user approves the plan."
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
    cancel: Option<CancellationToken>,
    task: Option<tokio::task::JoinHandle<()>>,
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
    ) -> Result<Self> {
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
        let stack = Arc::new(AgentStack::new(config).await?);

        let mcp_server_count = stack.mcp_server_count().await;
        let mcp_tool_count = stack.mcp_tool_count().await;

        let context_window = lookup_context_window(&provider_name, &model_name);

        Ok(Self {
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
            cancel: None,
            task: None,
        })
    }

    fn stack(&self) -> std::result::Result<&Arc<AgentStack>, String> {
        self.stack
            .as_ref()
            .ok_or_else(|| "AgentStack not initialised".to_string())
    }

    pub fn start(
        &mut self,
        goal: String,
        max_turns: usize,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
        history: Vec<ava_types::Message>,
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

        self.task = Some(tokio::spawn(async move {
            let result = stack.run(&goal, max_turns, Some(agent_tx), run_cancel, history).await;
            let mapped = result.map_err(|err| err.to_string());
            let _ = app_tx.send(AppEvent::AgentDone(mapped));
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
    }

    pub fn finish(&mut self, _result: &AgentRunResult) {
        self.is_running = false;
        self.activity = AgentActivity::Idle;
        self.cancel = None;
        self.task = None;
    }

    /// Switch model at runtime. Returns Ok(description) or Err(message).
    pub async fn switch_model(&mut self, provider: &str, model: &str) -> std::result::Result<String, String> {
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

    /// Get current provider/model description.
    pub fn current_model_display(&self) -> String {
        format!("{}/{}", self.provider_name, self.model_name)
    }

    /// Get MCP server info for display.
    pub async fn mcp_server_info(&self) -> std::result::Result<Vec<ava_agent::stack::MCPServerInfo>, String> {
        Ok(self.stack()?.mcp_server_info().await)
    }

    /// Reload MCP servers from config. Updates cached counts.
    pub async fn reload_mcp(&mut self) -> std::result::Result<String, String> {
        let (servers, tools) = self.stack()?.reload_mcp().await.map_err(|e| e.to_string())?;
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
            tokio::task::block_in_place(|| {
                tokio::runtime::Handle::current()
                    .block_on(stack.set_thinking_level(level))
            });
        }
    }

    /// Cycle thinking level and sync to agent stack. Returns the new level's label.
    pub fn cycle_thinking(&mut self) -> &'static str {
        self.thinking_level = self.thinking_level.cycle();
        if let Some(stack) = &self.stack {
            let stack = stack.clone();
            let level = self.thinking_level;
            tokio::task::block_in_place(|| {
                tokio::runtime::Handle::current()
                    .block_on(stack.set_thinking_level(level))
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
            max_turns: 10,
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
            cancel: None,
            task: None,
        }
    }

    /// Sync the agent mode prompt suffix to the stack.
    pub fn set_mode(&self, mode: super::agent::AgentMode) {
        let suffix = mode.prompt_suffix().map(|s| s.to_string());
        if let Some(stack) = &self.stack {
            let stack = stack.clone();
            tokio::task::block_in_place(|| {
                tokio::runtime::Handle::current()
                    .block_on(stack.set_mode_prompt_suffix(suffix))
            });
        }
    }

    /// Create tool templates in project .ava/tools directory.
    pub fn create_tool_templates(&self) -> std::result::Result<String, String> {
        let dir = std::env::current_dir()
            .unwrap_or_default()
            .join(".ava")
            .join("tools");
        let created = ava_tools::core::custom_tool::create_tool_templates(&dir)
            .map_err(|e| e.to_string())?;
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
