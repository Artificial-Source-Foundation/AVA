use crate::event::AppEvent;
use ava_agent::stack::{AgentRunResult, AgentStack, AgentStackConfig};
use color_eyre::Result;
use std::path::PathBuf;
use std::sync::Arc;
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
    stack: Arc<AgentStack>,
    pub is_running: bool,
    pub current_turn: usize,
    pub max_turns: usize,
    pub tokens_used: TokenUsage,
    pub activity: AgentActivity,
    pub provider_name: String,
    pub model_name: String,
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

        Ok(Self {
            stack,
            is_running: false,
            current_turn: 0,
            max_turns,
            tokens_used: TokenUsage::default(),
            activity: AgentActivity::Idle,
            provider_name,
            model_name,
            cancel: None,
            task: None,
        })
    }

    pub fn start(
        &mut self,
        goal: String,
        max_turns: usize,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        let cancel = CancellationToken::new();
        let stack = Arc::clone(&self.stack);
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
        self.stack
            .switch_model(provider, model)
            .await
            .map_err(|e| e.to_string())?;
        self.provider_name = provider.to_string();
        self.model_name = model.to_string();
        Ok(format!("{provider}/{model}"))
    }

    /// Get current provider/model description.
    pub fn current_model_display(&self) -> String {
        format!("{}/{}", self.provider_name, self.model_name)
    }
}
