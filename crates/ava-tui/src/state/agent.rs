use crate::event::AppEvent;
use ava_agent::stack::{AgentRunResult, AgentStack, AgentStackConfig};
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
    pub mcp_server_count: usize,
    pub mcp_tool_count: usize,
    pub tool_start: Option<Instant>,
    /// Workflow phase: (current_index, total_count, phase_name)
    pub workflow_phase: Option<(usize, usize, String)>,
    /// Workflow iteration: (current, max)
    pub workflow_iteration: Option<(usize, usize)>,
    /// Recently used models (most recent first, max 5).
    pub recent_models: Vec<String>,
    cancel: Option<CancellationToken>,
    task: Option<tokio::task::JoinHandle<()>>,
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
            mcp_server_count,
            mcp_tool_count,
            tool_start: None,
            workflow_phase: None,
            workflow_iteration: None,
            recent_models: Vec::new(),
            cancel: None,
            task: None,
        })
    }

    fn stack(&self) -> &Arc<AgentStack> {
        self.stack.as_ref().expect("AgentStack not initialised")
    }

    pub fn start(
        &mut self,
        goal: String,
        max_turns: usize,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
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
            let result = stack.run(&goal, max_turns, Some(agent_tx), run_cancel).await;
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
        self.stack()
            .switch_model(provider, model)
            .await
            .map_err(|e| e.to_string())?;
        self.provider_name = provider.to_string();
        self.model_name = model.to_string();

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
    pub async fn mcp_server_info(&self) -> Vec<ava_agent::stack::MCPServerInfo> {
        self.stack().mcp_server_info().await
    }

    /// Reload MCP servers from config. Updates cached counts.
    pub async fn reload_mcp(&mut self) -> std::result::Result<String, String> {
        let (servers, tools) = self.stack().reload_mcp().await.map_err(|e| e.to_string())?;
        self.mcp_server_count = servers;
        self.mcp_tool_count = tools;
        Ok(format!("MCP reloaded: {servers} servers, {tools} tools"))
    }

    /// Reload all tools (core + custom + MCP). Updates cached counts.
    pub async fn reload_tools(&mut self) -> std::result::Result<String, String> {
        let count = self.stack().reload_tools().await.map_err(|e| e.to_string())?;
        self.mcp_server_count = self.stack().mcp_server_count().await;
        self.mcp_tool_count = self.stack().mcp_tool_count().await;
        Ok(format!("Reloaded {count} tools"))
    }

    /// Get tool list with source info.
    pub async fn list_tools_with_source(
        &self,
    ) -> Vec<(ava_types::Tool, ava_tools::registry::ToolSource)> {
        self.stack().tools.read().await.list_tools_with_source()
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
            mcp_server_count: 0,
            mcp_tool_count: 0,
            tool_start: None,
            workflow_phase: None,
            workflow_iteration: None,
            recent_models: Vec::new(),
            cancel: None,
            task: None,
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
