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

pub struct AgentState {
    stack: Arc<AgentStack>,
    pub is_running: bool,
    pub current_turn: usize,
    pub max_turns: usize,
    pub tokens_used: TokenUsage,
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
    }

    pub fn finish(&mut self, _result: &AgentRunResult) {
        self.is_running = false;
        self.cancel = None;
        self.task = None;
    }
}
