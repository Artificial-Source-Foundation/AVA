use std::{pin::Pin, sync::Arc};

use async_trait::async_trait;
use ava_agent::{AgentConfig, AgentLoop, LLMProvider};
use ava_context::ContextManager;
use ava_tools::registry::ToolRegistry;
use ava_types::{Message, Result, Session};
use futures::{future::join_all, stream, Stream};
use serde::{Deserialize, Serialize};
use tokio::sync::Mutex;
use uuid::Uuid;

pub struct Commander {
    leads: Vec<Lead>,
    budget: Budget,
}

pub struct Lead {
    name: String,
    domain: Domain,
    workers: Vec<Worker>,
    model: Arc<dyn LLMProvider>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
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
    pub fn new(budget: Budget) -> Self {
        let leads = vec![
            Lead::new("frontend-lead", Domain::Frontend),
            Lead::new("backend-lead", Domain::Backend),
            Lead::new("qa-lead", Domain::QA),
            Lead::new("research-lead", Domain::Research),
            Lead::new("debug-lead", Domain::Debug),
            Lead::new("fullstack-lead", Domain::Fullstack),
            Lead::new("devops-lead", Domain::DevOps),
        ];

        Self { leads, budget }
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
            return Err(ava_types::AvaError::NotFound("lead not found".to_string()));
        };

        let worker = lead.spawn_worker(task, &self.budget)?;
        lead.workers.push(worker.clone());
        Ok(worker)
    }

    pub async fn coordinate(&self, workers: Vec<Worker>) -> Result<Session> {
        let run_futures = workers.into_iter().map(|worker| async move {
            let mut agent = worker.agent.lock().await;
            agent.run(&worker.task.description).await
        });

        let sessions = join_all(run_futures).await;
        let mut combined = Session::new();
        for session in sessions {
            let session = session?;
            for message in session.messages {
                combined.add_message(message);
            }
        }
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
    pub fn new(name: impl Into<String>, domain: Domain) -> Self {
        Self {
            name: name.into(),
            domain,
            workers: Vec::new(),
            model: Arc::new(NullProvider::new("commander-default-model")),
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

        Ok(Worker {
            id: Uuid::new_v4(),
            lead: self.name.clone(),
            agent: Arc::new(Mutex::new(AgentLoop::new(
                Box::new(NullProvider::new(self.model.model_name())),
                ToolRegistry::new(),
                ContextManager::new(worker_budget.max_tokens),
                AgentConfig {
                    max_turns: worker_budget.max_turns,
                    token_limit: worker_budget.max_tokens,
                    model: self.model.model_name().to_string(),
                },
            ))),
            budget: worker_budget,
            task,
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
}

#[derive(Debug, Clone)]
struct NullProvider {
    model: String,
}

impl NullProvider {
    fn new(model: impl Into<String>) -> Self {
        Self {
            model: model.into(),
        }
    }
}

#[async_trait]
impl LLMProvider for NullProvider {
    async fn generate(&self, _messages: &[Message]) -> Result<String> {
        Ok("".to_string())
    }

    async fn generate_stream(
        &self,
        _messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        Ok(Box::pin(stream::iter(Vec::<String>::new())))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        (input.len() / 4).max(1)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        (input_tokens + output_tokens) as f64 / 1_000_000.0
    }

    fn model_name(&self) -> &str {
        &self.model
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
        }
    }
}
