//! AVA Praxis — multi-agent orchestration with domain-specific leads.
//!
//! This crate implements the director pattern for coordinating multiple agents:
//! - Domain-specific leads (Frontend, Backend, QA, etc.)
//! - Worker spawning and task delegation
//! - Event streaming and coordination
//!
//! Hierarchy: User (CEO) -> Director -> Leads -> Workers

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use ava_agent::{AgentConfig, AgentEvent, AgentLoop};
#[cfg(feature = "cli-providers")]
use ava_cli_providers::{create_providers, discover_agents};
use ava_context::ContextManager;
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_platform::StandardPlatform;
use ava_tools::core::register_core_tools;
use ava_tools::registry::ToolRegistry;
use ava_types::{AvaError, Message, Result, Role, Session};
use futures::future::join_all;
use futures::StreamExt;
use serde::{Deserialize, Serialize};
use tokio::sync::{mpsc, Mutex};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

pub mod acp;
pub mod acp_handler;
pub mod acp_transport;
pub mod artifact;
pub mod artifact_store;
pub mod conflict;
pub mod events;
pub mod mailbox;
pub mod review;
pub mod spec;
pub mod spec_workflow;
pub mod workflow;

pub use acp::{AcpError, AcpMethod, AcpRequest, AcpResponse};
pub use acp_handler::AcpHandler;
pub use acp_transport::InProcessAcpTransport;
pub use artifact::{Artifact, ArtifactKind};
pub use artifact_store::{ArtifactStore, FileArtifactStore};
pub use conflict::{ConflictDetector, ConflictReport, WorkerIntent};
pub use events::PraxisEvent;
pub use mailbox::{Mailbox, PeerMessage, PeerMessageKind};
pub use review::{DiffMode, ReviewContext, ReviewResult, ReviewVerdict, Severity};
pub use spec::{SpecDocument, SpecStatus, SpecStore, SpecTask};
pub use spec_workflow::build_spec_goal;
pub use workflow::{Phase, PhaseRole, Workflow, WorkflowExecutor};

pub struct Director {
    leads: Vec<Lead>,
    budget: Budget,
}

pub struct DirectorConfig {
    pub budget: Budget,
    pub default_provider: Arc<dyn LLMProvider>,
    pub domain_providers: HashMap<Domain, Arc<dyn LLMProvider>>,
    pub platform: Option<Arc<StandardPlatform>>,
}

impl DirectorConfig {
    pub fn provider_for(&self, domain: Domain) -> Arc<dyn LLMProvider> {
        self.domain_providers
            .get(&domain)
            .cloned()
            .unwrap_or_else(|| self.default_provider.clone())
    }

    #[cfg(feature = "cli-providers")]
    pub async fn apply_cli_tier_routes(
        &mut self,
        tier_providers: &HashMap<Domain, String>,
        yolo: bool,
    ) {
        let discovered = discover_agents().await;
        let cli_providers = create_providers(&discovered, yolo);

        for (domain, provider_name) in tier_providers {
            if provider_name.starts_with("cli:") {
                if let Some(provider) = cli_providers.get(provider_name) {
                    self.domain_providers
                        .insert(domain.clone(), provider.clone());
                }
            }
        }
    }
}

pub struct Lead {
    name: String,
    domain: Domain,
    workers: Vec<Worker>,
    provider: Arc<dyn LLMProvider>,
    platform: Option<Arc<StandardPlatform>>,
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

impl Budget {
    pub fn new(max_tokens: usize, max_turns: usize, max_cost_usd: f64) -> Self {
        Self {
            max_tokens,
            max_turns,
            max_cost_usd,
        }
    }

    pub fn interactive(max_turns: usize, max_budget_usd: f64) -> Self {
        Self::new(
            128_000,
            if max_turns == 0 { 200 } else { max_turns },
            if max_budget_usd > 0.0 {
                max_budget_usd
            } else {
                10.0
            },
        )
    }
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

impl Director {
    pub fn new(config: DirectorConfig) -> Self {
        let platform = config.platform.clone();
        let leads = vec![
            Lead::new(
                "frontend-lead",
                Domain::Frontend,
                config.provider_for(Domain::Frontend),
                platform.clone(),
            ),
            Lead::new(
                "backend-lead",
                Domain::Backend,
                config.provider_for(Domain::Backend),
                platform.clone(),
            ),
            Lead::new(
                "qa-lead",
                Domain::QA,
                config.provider_for(Domain::QA),
                platform.clone(),
            ),
            Lead::new(
                "research-lead",
                Domain::Research,
                config.provider_for(Domain::Research),
                platform.clone(),
            ),
            Lead::new(
                "debug-lead",
                Domain::Debug,
                config.provider_for(Domain::Debug),
                platform.clone(),
            ),
            Lead::new(
                "fullstack-lead",
                Domain::Fullstack,
                config.provider_for(Domain::Fullstack),
                platform.clone(),
            ),
            Lead::new(
                "devops-lead",
                Domain::DevOps,
                config.provider_for(Domain::DevOps),
                platform.clone(),
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
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> Result<Session> {
        let futures = workers.into_iter().map(|worker| {
            let cancel = cancel.clone();
            let tx = event_tx.clone();
            let timeout = Duration::from_secs((worker.budget.max_turns * 60) as u64);

            async move {
                let _ = tx.send(PraxisEvent::WorkerStarted {
                    worker_id: worker.id,
                    lead: worker.lead.clone(),
                    task_description: worker.task.description.clone(),
                });

                let result = tokio::select! {
                    value = tokio::time::timeout(timeout, run_worker(&worker, tx.clone())) => {
                        match value {
                            Ok(result) => result,
                            Err(_) => Err(AvaError::TimeoutError(format!(
                                "Worker '{}' timed out after {}s on task: {}",
                                worker.lead,
                                timeout.as_secs(),
                                worker.task.description
                            ))),
                        }
                    }
                    _ = cancel.cancelled() => {
                        Err(AvaError::TimeoutError(format!(
                            "Worker '{}' cancelled while executing: {}",
                            worker.lead, worker.task.description
                        )))
                    }
                };

                match &result {
                    Ok(session) => {
                        let _ = tx.send(PraxisEvent::WorkerCompleted {
                            worker_id: worker.id,
                            success: true,
                            turns: session.messages.len(),
                        });
                    }
                    Err(error) => {
                        let _ = tx.send(PraxisEvent::WorkerFailed {
                            worker_id: worker.id,
                            error: error.to_string(),
                        });
                    }
                }

                (worker.id, worker.lead.clone(), result)
            }
        });

        let results = join_all(futures).await;

        let mut combined = Session::new();
        let mut succeeded = 0;
        let mut failed = 0;
        let mut total_turns = 0;

        for (worker_id, lead_name, result) in &results {
            match result {
                Ok(session) => {
                    // Add a separator message attributing this group to the worker
                    let header = Message::new(
                        Role::System,
                        format!(
                            "[worker-{}: {}] — {} messages",
                            worker_id,
                            lead_name,
                            session.messages.len()
                        ),
                    );
                    combined.add_message(header);

                    for message in &session.messages {
                        combined.add_message(message.clone());
                    }
                    total_turns += session.messages.len();
                    succeeded += 1;
                }
                Err(error) => {
                    let error_msg = Message::new(
                        Role::System,
                        format!("[worker-{worker_id}: {lead_name}] ERROR: {error}"),
                    );
                    combined.add_message(error_msg);
                    failed += 1;
                }
            }
        }

        // Summary message
        combined.add_message(Message::new(
            Role::System,
            format!(
                "Completed {}/{} workers successfully ({} total turns)",
                succeeded,
                results.len(),
                total_turns
            ),
        ));

        let _ = event_tx.send(PraxisEvent::AllComplete {
            total_workers: results.len(),
            succeeded,
            failed,
        });

        let _ = event_tx.send(PraxisEvent::Summary {
            total_workers: results.len(),
            succeeded,
            failed,
            total_turns,
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
    pub fn new(
        name: impl Into<String>,
        domain: Domain,
        provider: Arc<dyn LLMProvider>,
        platform: Option<Arc<StandardPlatform>>,
    ) -> Self {
        Self {
            name: name.into(),
            domain,
            workers: Vec::new(),
            provider,
            platform,
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

        let mut registry = ToolRegistry::new();
        if let Some(platform) = &self.platform {
            register_core_tools(&mut registry, platform.clone());
        }

        let agent = AgentLoop::new(
            Box::new(SharedProvider::new(self.provider.clone())),
            registry,
            ContextManager::new(worker_budget.max_tokens),
            AgentConfig {
                max_turns: worker_budget.max_turns,
                max_budget_usd: 0.0,
                token_limit: worker_budget.max_tokens,
                model: model_name,
                max_cost_usd: worker_budget.max_cost_usd,
                loop_detection: true,
                custom_system_prompt: None,
                thinking_level: ava_types::ThinkingLevel::Off,
                thinking_budget_tokens: None,
                system_prompt_suffix: None,
                extended_tools: true,
                plan_mode: false,
                post_edit_validation: None,
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
