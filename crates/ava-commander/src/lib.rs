use std::collections::HashMap;
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_agent::{AgentConfig, AgentEvent, AgentLoop};
use ava_context::ContextManager;
use ava_llm::provider::LLMProvider;
use ava_tools::registry::ToolRegistry;
use ava_types::{AvaError, Message, Result, Session};
use futures::future::join_all;
use futures::{Stream, StreamExt};
use serde::{Deserialize, Serialize};
use tokio::sync::{mpsc, Mutex};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

pub mod events;

pub use events::CommanderEvent;

pub struct Commander {
    leads: Vec<Lead>,
    budget: Budget,
}

pub struct CommanderConfig {
    pub budget: Budget,
    pub default_provider: Arc<dyn LLMProvider>,
    pub domain_providers: HashMap<Domain, Arc<dyn LLMProvider>>,
}

impl CommanderConfig {
    pub fn provider_for(&self, domain: Domain) -> Arc<dyn LLMProvider> {
        self.domain_providers
            .get(&domain)
            .cloned()
            .unwrap_or_else(|| self.default_provider.clone())
    }
}

pub struct Lead {
    name: String,
    domain: Domain,
    workers: Vec<Worker>,
    provider: Arc<dyn LLMProvider>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Hash)]
pub enum Domain {
    Frontend,
    Backend,
    QA,
    Research,
    Debug,
    Fullstack,
    DevOps,
}

pub struct Worker {
    id: Uuid,
    lead: String,
    agent: Arc<Mutex<AgentLoop>>,
    budget: Budget,
    task: Task,
    provider: Arc<dyn LLMProvider>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Budget {
    pub max_tokens: usize,
    pub max_turns: usize,
    pub max_cost_usd: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Task {
    pub description: String,
    pub task_type: TaskType,
    pub files: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum TaskType {
    Planning,
    CodeGeneration,
    Testing,
    Review,
    Research,
    Debug,
    Simple,
}

impl Commander {
    pub fn new(config: CommanderConfig) -> Self {
        let leads = vec![
            Lead::new(
                "frontend-lead",
                Domain::Frontend,
                config.provider_for(Domain::Frontend),
            ),
            Lead::new(
                "backend-lead",
                Domain::Backend,
                config.provider_for(Domain::Backend),
            ),
            Lead::new("qa-lead", Domain::QA, config.provider_for(Domain::QA)),
            Lead::new(
                "research-lead",
                Domain::Research,
                config.provider_for(Domain::Research),
            ),
            Lead::new(
                "debug-lead",
                Domain::Debug,
                config.provider_for(Domain::Debug),
            ),
            Lead::new(
                "fullstack-lead",
                Domain::Fullstack,
                config.provider_for(Domain::Fullstack),
            ),
            Lead::new(
                "devops-lead",
                Domain::DevOps,
                config.provider_for(Domain::DevOps),
            ),
        ];

        Self {
            leads,
            budget: config.budget,
        }
    }

    pub fn leads(&self) -> &[Lead] {
        &self.leads
    }

    pub fn budget(&self) -> &Budget {
        &self.budget
    }

    pub fn delegate(&mut self, task: Task) -> Result<Worker> {
        let domain = self.pick_domain(&task);
        let Some(lead) = self.leads.iter_mut().find(|lead| lead.domain == domain) else {
            return Err(AvaError::NotFound("lead not found".to_string()));
        };

        let worker = lead.spawn_worker(task, &self.budget)?;
        lead.workers.push(worker.clone());
        Ok(worker)
    }

    pub async fn coordinate(
        &self,
        workers: Vec<Worker>,
        cancel: CancellationToken,
        event_tx: mpsc::UnboundedSender<CommanderEvent>,
    ) -> Result<Session> {
        let futures = workers.into_iter().map(|worker| {
            let cancel = cancel.clone();
            let tx = event_tx.clone();
            let timeout = Duration::from_secs((worker.budget.max_turns * 60) as u64);

            async move {
                let _ = tx.send(CommanderEvent::WorkerStarted {
                    worker_id: worker.id,
                    lead: worker.lead.clone(),
                    task_description: worker.task.description.clone(),
                });

                let result = tokio::select! {
                    value = tokio::time::timeout(timeout, run_worker(&worker, tx.clone())) => {
                        match value {
                            Ok(result) => result,
                            Err(_) => Err(AvaError::TimeoutError("Worker timed out".to_string())),
                        }
                    }
                    _ = cancel.cancelled() => {
                        Err(AvaError::TimeoutError("Operation cancelled".to_string()))
                    }
                };

                match &result {
                    Ok(session) => {
                        let _ = tx.send(CommanderEvent::WorkerCompleted {
                            worker_id: worker.id,
                            success: true,
                            turns: session.messages.len(),
                        });
                    }
                    Err(error) => {
                        let _ = tx.send(CommanderEvent::WorkerFailed {
                            worker_id: worker.id,
                            error: error.to_string(),
                        });
                    }
                }

                (worker.id, result)
            }
        });

        let results = join_all(futures).await;

        let mut combined = Session::new();
        let mut succeeded = 0;
        let mut failed = 0;

        for (_, result) in &results {
            match result {
                Ok(session) => {
                    for message in &session.messages {
                        combined.add_message(message.clone());
                    }
                    succeeded += 1;
                }
                Err(_) => failed += 1,
            }
        }

        let _ = event_tx.send(CommanderEvent::AllComplete {
            total_workers: results.len(),
            succeeded,
            failed,
        });

        Ok(combined)
    }

    fn pick_domain(&self, task: &Task) -> Domain {
        match task.task_type {
            TaskType::Planning => Domain::Fullstack,
            TaskType::CodeGeneration => Domain::Backend,
            TaskType::Testing | TaskType::Review => Domain::QA,
            TaskType::Research => Domain::Research,
            TaskType::Debug => Domain::Debug,
            TaskType::Simple => Domain::Fullstack,
        }
    }
}

impl Lead {
    pub fn new(name: impl Into<String>, domain: Domain, provider: Arc<dyn LLMProvider>) -> Self {
        Self {
            name: name.into(),
            domain,
            workers: Vec::new(),
            provider,
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn domain(&self) -> &Domain {
        &self.domain
    }

    pub fn workers(&self) -> &[Worker] {
        &self.workers
    }

    pub fn spawn_worker(&self, task: Task, budget: &Budget) -> Result<Worker> {
        let worker_budget = Budget {
            max_tokens: (budget.max_tokens / 2).max(1),
            max_turns: (budget.max_turns / 2).max(1),
            max_cost_usd: budget.max_cost_usd / 2.0,
        };

        let model_name = self.provider.model_name().to_string();
        let agent = AgentLoop::new(
            Box::new(SharedProvider::new(self.provider.clone())),
            ToolRegistry::new(),
            ContextManager::new(worker_budget.max_tokens),
            AgentConfig {
                max_turns: worker_budget.max_turns,
                token_limit: worker_budget.max_tokens,
                model: model_name,
            },
        );

        Ok(Worker {
            id: Uuid::new_v4(),
            lead: self.name.clone(),
            agent: Arc::new(Mutex::new(agent)),
            budget: worker_budget,
            task,
            provider: self.provider.clone(),
        })
    }
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

async fn run_worker(
    worker: &Worker,
    event_tx: mpsc::UnboundedSender<CommanderEvent>,
) -> Result<Session> {
    let mut agent = worker.agent.lock().await;
    let mut stream = agent.run_streaming(&worker.task.description).await;

    while let Some(event) = stream.next().await {
        match event {
            AgentEvent::Progress(progress) => {
                if let Some(turn) = parse_turn(&progress) {
                    let _ = event_tx.send(CommanderEvent::WorkerProgress {
                        worker_id: worker.id,
                        turn,
                        max_turns: worker.budget.max_turns,
                    });
                }
            }
            AgentEvent::Token(token) => {
                let _ = event_tx.send(CommanderEvent::WorkerToken {
                    worker_id: worker.id,
                    token,
                });
            }
            AgentEvent::Complete(session) => return Ok(session),
            AgentEvent::Error(error) => return Err(AvaError::ToolError(error)),
            AgentEvent::ToolCall(_) | AgentEvent::ToolResult(_) => {}
        }
    }

    Err(AvaError::ToolError(
        "worker stream ended without completion".to_string(),
    ))
}

fn parse_turn(progress: &str) -> Option<usize> {
    progress.strip_prefix("turn ")?.parse::<usize>().ok()
}

struct SharedProvider {
    inner: Arc<dyn LLMProvider>,
}

impl SharedProvider {
    fn new(inner: Arc<dyn LLMProvider>) -> Self {
        Self { inner }
    }
}

#[async_trait]
impl LLMProvider for SharedProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        self.inner.generate(messages).await
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        self.inner.generate_stream(messages).await
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        self.inner.estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        self.inner.estimate_cost(input_tokens, output_tokens)
    }

    fn model_name(&self) -> &str {
        self.inner.model_name()
    }
}
