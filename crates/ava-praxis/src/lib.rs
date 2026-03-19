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
pub mod plan;
pub mod prompts;
pub mod review;
pub mod scout;
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
pub use plan::{ExecutionGroup, PlannerConfig, PraxisPlan, PraxisTask, TaskComplexity};
pub use prompts::{
    director_system_prompt, lead_system_prompt, lead_system_prompt_for_domain,
    worker_system_prompt, worker_system_prompt_for_domain,
};
pub use review::{DiffMode, ReviewContext, ReviewResult, ReviewVerdict, Severity};
pub use scout::{CodeSnippet, Scout, ScoutReport};
pub use spec::{SpecDocument, SpecStatus, SpecStore, SpecTask};
pub use spec_workflow::build_spec_goal;
pub use workflow::{Phase, PhaseRole, Workflow, WorkflowExecutor};

pub struct Director {
    leads: Vec<Lead>,
    budget: Budget,
    /// The Director's own LLM provider, used for planning.
    planning_provider: Arc<dyn LLMProvider>,
    /// Optional scout provider (cheap/fast model). Falls back to `planning_provider`.
    scout_provider: Option<Arc<dyn LLMProvider>>,
    /// Platform for file system access (needed by scouts).
    platform: Option<Arc<StandardPlatform>>,
}

pub struct DirectorConfig {
    pub budget: Budget,
    pub default_provider: Arc<dyn LLMProvider>,
    pub domain_providers: HashMap<Domain, Arc<dyn LLMProvider>>,
    pub platform: Option<Arc<StandardPlatform>>,
    /// Optional provider for scouts (cheap/fast model: Haiku, Flash, Mercury).
    /// Falls back to `default_provider` when `None`.
    pub scout_provider: Option<Arc<dyn LLMProvider>>,
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
            planning_provider: config.default_provider.clone(),
            scout_provider: config.scout_provider.clone(),
            platform: config.platform.clone(),
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

    /// Dispatch scouts to investigate multiple queries in parallel.
    ///
    /// Each query spawns one [`Scout`] with read-only tools.  All scouts run
    /// concurrently and their reports are collected.  Events are emitted via
    /// `event_tx` for progress tracking.
    ///
    /// Requires a platform (returns an error if `self.platform` is `None`).
    pub async fn scout(
        &self,
        queries: Vec<String>,
        cwd: &std::path::Path,
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> Vec<scout::ScoutReport> {
        let platform = match &self.platform {
            Some(p) => p.clone(),
            None => {
                tracing::warn!("Scout requested but no platform configured; returning empty");
                return Vec::new();
            }
        };

        let provider = self
            .scout_provider
            .clone()
            .unwrap_or_else(|| self.planning_provider.clone());

        let futures = queries.into_iter().map(|query| {
            let platform = platform.clone();
            let provider = provider.clone();
            let tx = event_tx.clone();
            let cwd = cwd.to_path_buf();

            async move {
                let scout_instance = scout::Scout::new(provider, platform);
                let id = scout_instance.id;

                let _ = tx.send(PraxisEvent::ScoutStarted {
                    id,
                    query: query.clone(),
                });

                match scout_instance.investigate(&query, &cwd).await {
                    Ok(report) => {
                        let _ = tx.send(PraxisEvent::ScoutCompleted {
                            id,
                            query: report.query.clone(),
                            files_examined: report.files_examined.len(),
                            snippets_found: report.relevant_code.len(),
                        });
                        Some(report)
                    }
                    Err(e) => {
                        let _ = tx.send(PraxisEvent::ScoutFailed {
                            id,
                            query: query.clone(),
                            error: e.to_string(),
                        });
                        tracing::warn!("Scout failed for query '{}': {}", query, e);
                        None
                    }
                }
            }
        });

        let results = join_all(futures).await;
        results.into_iter().flatten().collect()
    }

    /// Use the Director's LLM to analyze the goal and produce a structured plan.
    ///
    /// If LLM planning fails, falls back to the static `pick_domain()` routing
    /// by returning a single-task plan mapped via `TaskType`.
    pub async fn plan(&self, goal: &str, context: Option<&str>) -> Result<PraxisPlan> {
        self.plan_with_config(goal, context, &PlannerConfig::default())
            .await
    }

    /// Like [`plan`](Self::plan) but with explicit planner configuration.
    pub async fn plan_with_config(
        &self,
        goal: &str,
        context: Option<&str>,
        config: &PlannerConfig,
    ) -> Result<PraxisPlan> {
        if !config.enabled {
            return self.fallback_plan(goal);
        }

        match plan::create_plan(
            self.planning_provider.clone(),
            goal,
            context,
            &self.budget,
            config,
        )
        .await
        {
            Ok(plan) => {
                tracing::info!(
                    tasks = plan.tasks.len(),
                    groups = plan.execution_groups.len(),
                    "Director created LLM-powered plan"
                );
                Ok(plan)
            }
            Err(err) => {
                tracing::warn!(%err, "LLM planning failed, falling back to static routing");
                self.fallback_plan(goal)
            }
        }
    }

    /// Execute a plan by spawning workers per execution group.
    ///
    /// Groups execute sequentially; tasks within a group run in parallel.
    /// Emits `PraxisEvent::PlanCreated` before execution begins.
    pub async fn execute_plan(
        &mut self,
        plan: PraxisPlan,
        cancel: CancellationToken,
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> Result<Session> {
        let _ = event_tx.send(PraxisEvent::PlanCreated { plan: plan.clone() });

        let mut combined = Session::new();
        let mut spawn_failures = 0usize;

        for (group_idx, group) in plan.execution_groups.iter().enumerate() {
            if cancel.is_cancelled() {
                break;
            }

            let _ = event_tx.send(PraxisEvent::PhaseStarted {
                phase_index: group_idx,
                phase_count: plan.execution_groups.len(),
                phase_name: group.label.clone(),
                role: "director".to_string(),
            });

            // Spawn workers for all tasks in this group
            let mut workers = Vec::new();
            for task_id in &group.task_ids {
                if let Some(plan_task) = plan.tasks.iter().find(|t| &t.id == task_id) {
                    let task = Task {
                        description: plan_task.description.clone(),
                        task_type: domain_to_task_type(&plan_task.domain),
                        files: plan_task.files_hint.clone(),
                    };

                    match self.delegate_to_domain(task, &plan_task.domain, &plan_task.budget) {
                        Ok(worker) => workers.push(worker),
                        Err(err) => {
                            tracing::warn!(task_id, %err, "failed to spawn worker for plan task");
                            spawn_failures += 1;
                        }
                    }
                }
            }

            if workers.is_empty() {
                continue;
            }

            // Run all workers in this group in parallel.
            // `coordinate` emits its own WorkerStarted/Completed/Failed + AllComplete events.
            let group_session = self
                .coordinate(workers, cancel.clone(), event_tx.clone())
                .await?;

            // Merge group results into combined session
            for msg in &group_session.messages {
                combined.add_message(msg.clone());
            }

            let _ = event_tx.send(PraxisEvent::PhaseCompleted {
                phase_index: group_idx,
                phase_name: group.label.clone(),
                turns: group_session.messages.len(),
                output_preview: String::new(),
            });
        }

        if spawn_failures > 0 {
            tracing::warn!(spawn_failures, "some plan tasks could not be spawned");
        }

        Ok(combined)
    }

    /// Delegate a task to a specific domain with a specific budget.
    fn delegate_to_domain(
        &mut self,
        task: Task,
        domain: &Domain,
        task_budget: &Budget,
    ) -> Result<Worker> {
        let Some(lead) = self.leads.iter_mut().find(|lead| &lead.domain == domain) else {
            return Err(AvaError::NotFound(format!(
                "no lead found for domain {domain:?}"
            )));
        };

        let worker = lead.spawn_worker_with_budget(task, task_budget)?;
        lead.workers.push(worker.clone());
        Ok(worker)
    }

    /// Produce a single-task fallback plan using the static `pick_domain()` logic.
    fn fallback_plan(&self, goal: &str) -> Result<PraxisPlan> {
        let task = Task {
            description: goal.to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        };
        let domain = self.pick_domain(&task);

        Ok(PraxisPlan {
            goal: goal.to_string(),
            tasks: vec![PraxisTask {
                id: "t1".to_string(),
                description: goal.to_string(),
                domain,
                complexity: TaskComplexity::Simple,
                dependencies: vec![],
                budget: self.budget.clone(),
                files_hint: vec![],
            }],
            execution_groups: vec![ExecutionGroup {
                task_ids: vec!["t1".to_string()],
                label: "Phase 1: Execution".to_string(),
            }],
            total_budget: self.budget.clone(),
        })
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
        self.build_worker(task, worker_budget)
    }

    /// Spawn a worker with a pre-allocated budget (used by plan execution).
    pub fn spawn_worker_with_budget(&self, task: Task, budget: &Budget) -> Result<Worker> {
        self.build_worker(task, budget.clone())
    }

    fn build_worker(&self, task: Task, worker_budget: Budget) -> Result<Worker> {
        let model_name = self.provider.model_name().to_string();
        let worker_id = Uuid::new_v4();

        // Pick a worker name from the name pool based on worker count
        let worker_name = worker_name_for_index(self.workers.len());
        let system_prompt = prompts::worker_system_prompt_for_domain(worker_name, &self.domain);

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
                custom_system_prompt: Some(system_prompt),
                thinking_level: ava_types::ThinkingLevel::Off,
                thinking_budget_tokens: None,
                system_prompt_suffix: None,
                extended_tools: true,
                plan_mode: false,
                post_edit_validation: None,
            },
        );

        Ok(Worker {
            id: worker_id,
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

/// Worker name pool from the Praxis design spec.
const WORKER_NAMES: &[&str] = &[
    "Pedro", "Sofia", "Luna", "Kai", "Mira", "Rio", "Ash", "Nico", "Ivy", "Juno", "Zara", "Leo",
];

/// Pick a worker name from the pool, cycling if more workers than names.
fn worker_name_for_index(index: usize) -> &'static str {
    WORKER_NAMES[index % WORKER_NAMES.len()]
}

/// Map a domain to the most appropriate TaskType.
fn domain_to_task_type(domain: &Domain) -> TaskType {
    match domain {
        Domain::Frontend | Domain::Backend | Domain::DevOps => TaskType::CodeGeneration,
        Domain::QA => TaskType::Testing,
        Domain::Research => TaskType::Research,
        Domain::Debug => TaskType::Debug,
        Domain::Fullstack => TaskType::Simple,
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
