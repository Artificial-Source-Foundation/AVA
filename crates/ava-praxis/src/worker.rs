//! Worker — individual task executors managed by leads.

use std::sync::Arc;

use ava_agent::{AgentEvent, AgentLoop};
use ava_llm::provider::LLMProvider;
use ava_types::{AvaError, Result, Session};
use futures::StreamExt;
use tokio::sync::{mpsc, Mutex};
use uuid::Uuid;

use crate::events::PraxisEvent;
use crate::{Budget, Task};

pub struct Worker {
    pub(crate) id: Uuid,
    pub(crate) lead: String,
    pub(crate) agent: Arc<Mutex<AgentLoop>>,
    pub(crate) budget: Budget,
    pub(crate) task: Task,
    pub(crate) provider: Arc<dyn LLMProvider>,
}

impl Worker {
    pub fn id(&self) -> Uuid {
        self.id
    }

    pub fn lead(&self) -> &str {
        &self.lead
    }

    pub fn budget(&self) -> &Budget {
        &self.budget
    }

    pub fn task(&self) -> &Task {
        &self.task
    }

    pub fn model_name(&self) -> &str {
        self.provider.model_name()
    }
}

impl Clone for Worker {
    fn clone(&self) -> Self {
        Self {
            id: self.id,
            lead: self.lead.clone(),
            agent: Arc::clone(&self.agent),
            budget: self.budget.clone(),
            task: self.task.clone(),
            provider: self.provider.clone(),
        }
    }
}

pub(crate) async fn run_worker(
    worker: &Worker,
    event_tx: mpsc::UnboundedSender<PraxisEvent>,
) -> Result<Session> {
    let mut agent = worker.agent.lock().await;
    let mut stream = agent.run_streaming(&worker.task.description).await;

    while let Some(event) = stream.next().await {
        match event {
            AgentEvent::Progress(progress) => {
                if let Some(turn) = parse_turn(&progress) {
                    let _ = event_tx.send(PraxisEvent::WorkerProgress {
                        worker_id: worker.id,
                        turn,
                        max_turns: worker.budget.max_turns,
                    });
                }
            }
            AgentEvent::Token(token) => {
                let _ = event_tx.send(PraxisEvent::WorkerToken {
                    worker_id: worker.id,
                    token,
                });
            }
            AgentEvent::Complete(session) => return Ok(session),
            AgentEvent::Error(error) => return Err(AvaError::ToolError(error)),
            AgentEvent::Thinking(_)
            | AgentEvent::ToolCall(_)
            | AgentEvent::ToolResult(_)
            | AgentEvent::ToolStats(_)
            | AgentEvent::BudgetWarning { .. }
            | AgentEvent::TokenUsage { .. }
            | AgentEvent::SubAgentComplete { .. }
            | AgentEvent::DiffPreview { .. } => {}
        }
    }

    Err(AvaError::ToolError(
        "worker stream ended without completion".to_string(),
    ))
}

fn parse_turn(progress: &str) -> Option<usize> {
    progress.strip_prefix("turn ")?.parse::<usize>().ok()
}
