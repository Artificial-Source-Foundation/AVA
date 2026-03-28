//! Worker — individual task executors managed by leads.

use std::sync::Arc;

use ava_agent::{AgentEvent, AgentLoop};
use ava_llm::provider::LLMProvider;
use ava_types::{AvaError, Result, Session};
use futures::StreamExt;
use tokio::sync::{mpsc, Mutex};
use uuid::Uuid;

use crate::events::HqEvent;
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
    event_tx: mpsc::UnboundedSender<HqEvent>,
) -> Result<Session> {
    let mut agent = worker.agent.lock().await;
    let mut stream = agent.run_streaming(&worker.task.description).await;

    while let Some(event) = stream.next().await {
        match event {
            AgentEvent::Progress(progress) => {
                if let Some(turn) = parse_turn(&progress) {
                    let _ = event_tx.send(HqEvent::WorkerProgress {
                        worker_id: worker.id,
                        turn,
                        max_turns: worker.budget.max_turns,
                    });
                }
            }
            AgentEvent::Token(token) => {
                let _ = event_tx.send(HqEvent::WorkerToken {
                    worker_id: worker.id,
                    token,
                });
            }
            AgentEvent::Thinking(content) => {
                let _ = event_tx.send(HqEvent::WorkerThinking {
                    worker_id: worker.id,
                    content,
                });
            }
            AgentEvent::ToolCall(call) => {
                let _ = event_tx.send(HqEvent::WorkerToolCall {
                    worker_id: worker.id,
                    call_id: call.id.clone(),
                    name: call.name.clone(),
                    args_json: serde_json::to_string(&call.arguments)
                        .unwrap_or_else(|_| "{}".to_string()),
                });
            }
            AgentEvent::ToolResult(result) => {
                let _ = event_tx.send(HqEvent::WorkerToolResult {
                    worker_id: worker.id,
                    call_id: result.call_id.clone(),
                    content: result.content.clone(),
                    is_error: result.is_error,
                });
            }
            AgentEvent::Complete(session) => return Ok(session),
            AgentEvent::Error(error) => return Err(AvaError::ToolError(error)),
            AgentEvent::ToolStats(_)
            | AgentEvent::BudgetWarning { .. }
            | AgentEvent::TokenUsage { .. }
            | AgentEvent::SubAgentComplete { .. }
            | AgentEvent::DiffPreview { .. }
            | AgentEvent::MCPToolsChanged { .. }
            | AgentEvent::Checkpoint(_)
            | AgentEvent::SnapshotTaken { .. }
            | AgentEvent::PlanStepComplete { .. }
            | AgentEvent::StreamingEditProgress { .. } => {}
        }
    }

    Err(AvaError::ToolError(
        "worker stream ended without completion".to_string(),
    ))
}

fn parse_turn(progress: &str) -> Option<usize> {
    progress.strip_prefix("turn ")?.parse::<usize>().ok()
}
