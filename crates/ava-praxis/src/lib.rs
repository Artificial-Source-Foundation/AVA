//! AVA Praxis — multi-agent orchestration with domain-specific leads.
//!
//! This crate implements the director pattern for coordinating multiple agents:
//! - Domain-specific leads (Frontend, Backend, QA, etc.)
//! - Worker spawning and task delegation
//! - Event streaming and coordination
//!
//! Hierarchy: User (CEO) -> Director -> Leads -> Workers

use std::collections::{HashMap, HashSet};
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
pub mod board;
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
pub use board::{Board, BoardMember, BoardOpinion, BoardPersonality, BoardResult, BoardVote};
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
    /// Optional Board of Directors providers (SOTA models for complex task consensus).
    /// Each provider becomes a board member with a rotating personality.
    board_providers: Vec<Arc<dyn LLMProvider>>,
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
    /// Optional Board of Directors providers (SOTA models for complex task consensus).
    /// Each provider becomes a board member with a rotating personality
    /// (Analytical, Pragmatic, Creative). Typically 3 providers from different vendors.
    #[allow(clippy::doc_markdown)]
    pub board_providers: Vec<Arc<dyn LLMProvider>>,
    /// Custom worker names. Falls back to the built-in pool when empty.
    pub worker_names: Vec<String>,
    /// Which lead domains are enabled. When empty, all domains are enabled.
    pub enabled_leads: Vec<Domain>,
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
    /// Custom worker names pool (empty = use built-in default).
    worker_names: Vec<String>,
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

        let all_domains = vec![
            ("frontend-lead", Domain::Frontend),
            ("backend-lead", Domain::Backend),
            ("qa-lead", Domain::QA),
            ("research-lead", Domain::Research),
            ("debug-lead", Domain::Debug),
            ("fullstack-lead", Domain::Fullstack),
            ("devops-lead", Domain::DevOps),
        ];

        let worker_names = config.worker_names.clone();
        let leads: Vec<Lead> = all_domains
            .into_iter()
            .filter(|(_, domain)| {
                // If enabled_leads is empty, all are enabled
                config.enabled_leads.is_empty() || config.enabled_leads.contains(domain)
            })
            .map(|(name, domain)| {
                Lead::new(
                    name,
                    domain.clone(),
                    config.provider_for(domain),
                    platform.clone(),
                )
                .with_worker_names(worker_names.clone())
            })
            .collect();

        Self {
            leads,
            budget: config.budget,
            planning_provider: config.default_provider.clone(),
            scout_provider: config.scout_provider.clone(),
            board_providers: config.board_providers,
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

    /// Convene the Board of Directors for multi-model consensus.
    ///
    /// Creates a [`Board`] from the configured `board_providers`, assigning each
    /// a rotating personality (Analytical, Pragmatic, Creative).  The board
    /// members evaluate the goal and scout reports in parallel and vote on the
    /// approach.
    ///
    /// Returns `None` if no board providers are configured.
    pub async fn consult_board(
        &self,
        goal: &str,
        scout_reports: &[scout::ScoutReport],
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> Result<Option<board::BoardResult>> {
        if self.board_providers.is_empty() {
            tracing::debug!("No board providers configured, skipping board consultation");
            return Ok(None);
        }

        let personalities = [
            board::BoardPersonality::Analytical,
            board::BoardPersonality::Pragmatic,
            board::BoardPersonality::Creative,
        ];

        let members: Vec<board::BoardMember> = self
            .board_providers
            .iter()
            .enumerate()
            .map(|(i, provider)| {
                let model = provider.model_name().to_string();
                // Derive a display name from the model name (e.g. "claude-opus-4" -> "Opus")
                let name = derive_board_name(&model);
                let personality = personalities[i % personalities.len()];
                board::BoardMember::new(name, provider.clone(), personality)
            })
            .collect();

        let member_names: Vec<String> = members.iter().map(|m| m.name.clone()).collect();
        let _ = event_tx.send(PraxisEvent::BoardConvened {
            members: member_names,
        });

        let board = board::Board::new(members);
        let result = board.convene(goal, scout_reports).await?;

        // Emit individual opinion events
        for opinion in &result.opinions {
            let _ = event_tx.send(PraxisEvent::BoardOpinion {
                member: opinion.member_name.clone(),
                vote: opinion.vote.to_string(),
                summary: opinion.recommendation.clone(),
            });
        }

        // Emit the final result event
        let _ = event_tx.send(PraxisEvent::BoardResult {
            consensus: result.consensus.clone(),
            vote_summary: result.vote_summary.clone(),
        });

        Ok(Some(result))
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

    /// Execute a plan by delegating tasks to domain-specific leads.
    ///
    /// The Director groups tasks by domain lead and delegates execution to each
    /// lead via [`Lead::execute_tasks()`], which respects task dependencies
    /// (topological sort into waves). Groups execute sequentially; within each
    /// group, tasks with no inter-dependencies run in parallel.
    ///
    /// After each lead's workers complete, the lead reviews the results. If
    /// issues are found, a fix worker is spawned.
    ///
    /// Emits `PraxisEvent::PlanCreated` before execution begins.
    pub async fn execute_plan(
        &mut self,
        plan: PraxisPlan,
        cancel: CancellationToken,
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> Result<Session> {
        let _ = event_tx.send(PraxisEvent::PlanCreated { plan: plan.clone() });

        let mut combined = Session::new();

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

            // Collect plan tasks for this group
            let group_tasks: Vec<PraxisTask> = group
                .task_ids
                .iter()
                .filter_map(|task_id| plan.tasks.iter().find(|t| &t.id == task_id).cloned())
                .collect();

            if group_tasks.is_empty() {
                continue;
            }

            // Group tasks by domain so each lead manages its own
            let mut tasks_by_domain: HashMap<Domain, Vec<PraxisTask>> = HashMap::new();
            for task in group_tasks {
                tasks_by_domain
                    .entry(task.domain.clone())
                    .or_default()
                    .push(task);
            }

            // Execute each lead's tasks. Leads run sequentially to avoid
            // cross-lead race conditions; within each lead, dependency-based
            // waves provide the parallelism.
            let mut group_turns = 0usize;
            for (domain, domain_tasks) in tasks_by_domain {
                if cancel.is_cancelled() {
                    break;
                }

                let Some(lead) = self.leads.iter_mut().find(|l| l.domain == domain) else {
                    tracing::warn!(?domain, "no lead found for domain, skipping tasks");
                    continue;
                };

                let (lead_session, _results) = lead
                    .execute_tasks(domain_tasks, &self.budget, cancel.clone(), event_tx.clone())
                    .await?;

                group_turns += lead_session.messages.len();

                // Lead reviews the work
                let issues = lead
                    .review_results(&lead_session, event_tx.clone())
                    .await
                    .unwrap_or(0);

                if issues > 0 {
                    tracing::info!(
                        lead = %lead.name(),
                        issues,
                        "Lead review found issues, spawning fix worker"
                    );
                    // Spawn a fix worker under this lead
                    let fix_task = Task {
                        description: format!(
                            "Review found {} issue(s) in {} domain work. \
                             Fix the issues identified in the previous session.",
                            issues,
                            lead.name()
                        ),
                        task_type: TaskType::Review,
                        files: vec![],
                    };
                    if let Ok(fix_worker) = lead.spawn_worker(fix_task, &self.budget) {
                        lead.workers.push(fix_worker.clone());
                        // Run the fix worker inline
                        match run_worker(&fix_worker, event_tx.clone()).await {
                            Ok(fix_session) => {
                                let header = Message::new(
                                    Role::System,
                                    format!(
                                        "[{}: fix-worker-{}] — {} messages",
                                        lead.name(),
                                        fix_worker.id,
                                        fix_session.messages.len()
                                    ),
                                );
                                combined.add_message(header);
                                for msg in &fix_session.messages {
                                    combined.add_message(msg.clone());
                                }
                            }
                            Err(err) => {
                                tracing::warn!(%err, "fix worker failed");
                            }
                        }
                    }
                }

                // Merge lead session into combined
                for msg in &lead_session.messages {
                    combined.add_message(msg.clone());
                }
            }

            let _ = event_tx.send(PraxisEvent::PhaseCompleted {
                phase_index: group_idx,
                phase_name: group.label.clone(),
                turns: group_turns,
                output_preview: String::new(),
            });
        }

        Ok(combined)
    }

    /// Delegate a task to a specific domain with a specific budget.
    #[allow(dead_code)]
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
            worker_names: Vec::new(),
        }
    }

    /// Create a new lead with custom worker names.
    pub fn with_worker_names(mut self, names: Vec<String>) -> Self {
        self.worker_names = names;
        self
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

    /// Execute a set of tasks respecting their dependency order.
    ///
    /// Tasks are grouped into waves via [`topological_sort`]. Within each wave,
    /// workers run in parallel. Waves execute sequentially — wave N+1 starts
    /// only after wave N completes.
    ///
    /// Returns the merged session from all workers, plus a list of per-worker
    /// results (worker ID, success flag, optional session).
    pub async fn execute_tasks(
        &mut self,
        tasks: Vec<PraxisTask>,
        budget: &Budget,
        cancel: CancellationToken,
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> Result<(Session, Vec<(Uuid, bool)>)> {
        let waves = topological_sort(&tasks)?;

        let total_tasks = tasks.len();
        let total_waves = waves.len();

        let _ = event_tx.send(PraxisEvent::LeadExecutionStarted {
            lead: self.name.clone(),
            total_tasks,
            total_waves,
        });

        let mut combined = Session::new();
        let mut results_summary: Vec<(Uuid, bool)> = Vec::new();
        let mut total_succeeded = 0usize;
        let mut total_failed = 0usize;

        for (wave_idx, wave) in waves.into_iter().enumerate() {
            if cancel.is_cancelled() {
                break;
            }

            let wave_task_count = wave.len();
            let _ = event_tx.send(PraxisEvent::LeadWaveStarted {
                lead: self.name.clone(),
                wave_index: wave_idx,
                task_count: wave_task_count,
            });

            // Spawn workers for this wave
            let mut workers = Vec::new();
            for praxis_task in wave {
                let task = Task {
                    description: praxis_task.description.clone(),
                    task_type: domain_to_task_type(&praxis_task.domain),
                    files: praxis_task.files_hint.clone(),
                };
                let worker_budget = if praxis_task.budget.max_turns > 0 {
                    praxis_task.budget.clone()
                } else {
                    budget.clone()
                };
                match self.build_worker(task, worker_budget) {
                    Ok(worker) => {
                        self.workers.push(worker.clone());
                        workers.push(worker);
                    }
                    Err(err) => {
                        tracing::warn!(task_id = %praxis_task.id, %err, "failed to spawn worker");
                        total_failed += 1;
                    }
                }
            }

            if workers.is_empty() {
                let _ = event_tx.send(PraxisEvent::LeadWaveCompleted {
                    lead: self.name.clone(),
                    wave_index: wave_idx,
                    succeeded: 0,
                    failed: wave_task_count,
                });
                continue;
            }

            // Run all workers in this wave in parallel
            let wave_futures = workers.into_iter().map(|worker| {
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

                    (worker.id, result)
                }
            });

            let wave_results = join_all(wave_futures).await;

            let mut wave_succeeded = 0usize;
            let mut wave_failed = 0usize;

            for (worker_id, result) in wave_results {
                match result {
                    Ok(session) => {
                        let header = Message::new(
                            Role::System,
                            format!(
                                "[{}: worker-{}] — {} messages",
                                self.name,
                                worker_id,
                                session.messages.len()
                            ),
                        );
                        combined.add_message(header);
                        for msg in &session.messages {
                            combined.add_message(msg.clone());
                        }
                        results_summary.push((worker_id, true));
                        wave_succeeded += 1;
                    }
                    Err(error) => {
                        let error_msg = Message::new(
                            Role::System,
                            format!("[{}: worker-{worker_id}] ERROR: {error}", self.name),
                        );
                        combined.add_message(error_msg);
                        results_summary.push((worker_id, false));
                        wave_failed += 1;
                    }
                }
            }

            total_succeeded += wave_succeeded;
            total_failed += wave_failed;

            let _ = event_tx.send(PraxisEvent::LeadWaveCompleted {
                lead: self.name.clone(),
                wave_index: wave_idx,
                succeeded: wave_succeeded,
                failed: wave_failed,
            });
        }

        let _ = event_tx.send(PraxisEvent::LeadExecutionCompleted {
            lead: self.name.clone(),
            total_tasks,
            succeeded: total_succeeded,
            failed: total_failed,
        });

        Ok((combined, results_summary))
    }

    /// Review the results produced by this lead's workers.
    ///
    /// Uses the lead's own LLM provider to check whether the combined session
    /// output looks complete and correct. Returns the number of issues found
    /// (0 means the review passed).
    pub async fn review_results(
        &self,
        session: &Session,
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> Result<usize> {
        let _ = event_tx.send(PraxisEvent::LeadReviewStarted {
            lead: self.name.clone(),
        });

        // Build a summary of the session for the LLM to review
        let mut summary = String::new();
        for msg in &session.messages {
            let role_label = match msg.role {
                Role::System => "SYSTEM",
                Role::User => "USER",
                Role::Assistant => "ASSISTANT",
                Role::Tool => "TOOL",
            };
            summary.push_str(&format!("[{}] {}\n", role_label, msg.content));
            // Cap the summary to avoid blowing the context window
            if summary.len() > 8000 {
                summary.push_str("\n... (truncated)\n");
                break;
            }
        }

        let review_prompt = format!(
            "You are a QA reviewer for the {} domain. Review the following work session and identify any issues.\n\n\
             Respond with a JSON object: {{\"issues\": [\"description of issue 1\", ...]}}\n\
             If everything looks correct, respond with: {{\"issues\": []}}\n\n\
             Work session:\n{}",
            self.name, summary
        );

        let messages = vec![
            Message::new(
                Role::System,
                "You are a precise QA reviewer. Respond only with valid JSON.".to_string(),
            ),
            Message::new(Role::User, review_prompt),
        ];

        let response = self.provider.generate(&messages).await;

        let issues_found = match response {
            Ok(raw) => {
                // Try to parse the JSON response
                let trimmed = raw.trim();
                let json_str = if trimmed.starts_with("```") {
                    trimmed
                        .trim_start_matches("```json")
                        .trim_start_matches("```")
                        .trim_end_matches("```")
                        .trim()
                } else {
                    trimmed
                };

                #[derive(Deserialize)]
                struct ReviewResponse {
                    #[serde(default)]
                    issues: Vec<String>,
                }

                match serde_json::from_str::<ReviewResponse>(json_str) {
                    Ok(review) => review.issues.len(),
                    Err(_) => {
                        tracing::debug!(
                            "Could not parse review response as JSON, assuming no issues"
                        );
                        0
                    }
                }
            }
            Err(err) => {
                tracing::warn!(%err, "Lead review LLM call failed, skipping review");
                0
            }
        };

        let _ = event_tx.send(PraxisEvent::LeadReviewCompleted {
            lead: self.name.clone(),
            issues_found,
        });

        Ok(issues_found)
    }

    fn build_worker(&self, task: Task, worker_budget: Budget) -> Result<Worker> {
        let model_name = self.provider.model_name().to_string();
        let worker_id = Uuid::new_v4();

        // Pick a worker name from the custom pool or fall back to the built-in default
        let idx = self.workers.len();
        let worker_name: String = if self.worker_names.is_empty() {
            worker_name_for_index(idx).to_string()
        } else {
            self.worker_names[idx % self.worker_names.len()].clone()
        };
        let system_prompt = prompts::worker_system_prompt_for_domain(&worker_name, &self.domain);

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

/// Group tasks into waves based on their dependency graph.
///
/// - Wave 0: tasks with no dependencies (run in parallel)
/// - Wave 1: tasks whose dependencies are all in wave 0 (run in parallel)
/// - etc.
///
/// Returns an error if a dependency cycle is detected or a dependency references
/// an unknown task ID.
pub fn topological_sort(tasks: &[PraxisTask]) -> Result<Vec<Vec<&PraxisTask>>> {
    if tasks.is_empty() {
        return Ok(Vec::new());
    }

    let task_map: HashMap<&str, &PraxisTask> = tasks.iter().map(|t| (t.id.as_str(), t)).collect();

    // Validate all dependencies reference existing tasks
    for task in tasks {
        for dep in &task.dependencies {
            if !task_map.contains_key(dep.as_str()) {
                return Err(AvaError::ToolError(format!(
                    "task '{}' depends on unknown task id: {dep}",
                    task.id
                )));
            }
        }
    }

    let mut waves: Vec<Vec<&PraxisTask>> = Vec::new();
    let mut assigned: HashSet<&str> = HashSet::new();
    let mut remaining: Vec<&PraxisTask> = tasks.iter().collect();

    while !remaining.is_empty() {
        let wave: Vec<&PraxisTask> = remaining
            .iter()
            .filter(|t| {
                t.dependencies
                    .iter()
                    .all(|dep| assigned.contains(dep.as_str()))
            })
            .copied()
            .collect();

        if wave.is_empty() {
            // No tasks can be scheduled — cycle detected
            let stuck: Vec<&str> = remaining.iter().map(|t| t.id.as_str()).collect();
            return Err(AvaError::ToolError(format!(
                "dependency cycle detected among tasks: {stuck:?}"
            )));
        }

        for t in &wave {
            assigned.insert(&t.id);
        }

        let wave_ids: HashSet<&str> = wave.iter().map(|t| t.id.as_str()).collect();
        remaining.retain(|t| !wave_ids.contains(t.id.as_str()));

        waves.push(wave);
    }

    Ok(waves)
}

/// Worker name pool from the Praxis design spec.
const WORKER_NAMES: &[&str] = &[
    "Pedro", "Sofia", "Luna", "Kai", "Mira", "Rio", "Ash", "Nico", "Ivy", "Juno", "Zara", "Leo",
];

/// Pick a worker name from the pool, cycling if more workers than names.
fn worker_name_for_index(index: usize) -> &'static str {
    WORKER_NAMES[index % WORKER_NAMES.len()]
}

/// Derive a short display name for a board member from the model name.
///
/// Examples: "claude-opus-4" -> "Opus", "gpt-5.4" -> "GPT", "gemini-2.0-pro" -> "Gemini"
fn derive_board_name(model: &str) -> String {
    let lower = model.to_lowercase();
    if lower.contains("opus") {
        "Opus (Board)".to_string()
    } else if lower.contains("sonnet") {
        "Sonnet (Board)".to_string()
    } else if lower.contains("gemini") {
        "Gemini (Board)".to_string()
    } else if lower.contains("gpt") {
        "GPT (Board)".to_string()
    } else if lower.contains("mercury") {
        "Mercury (Board)".to_string()
    } else if lower.contains("haiku") {
        "Haiku (Board)".to_string()
    } else {
        // Use the model name itself, capitalised, with (Board) suffix
        let name = model
            .split('/')
            .next_back()
            .unwrap_or(model)
            .split('-')
            .next()
            .unwrap_or(model);
        let capitalised = {
            let mut c = name.chars();
            match c.next() {
                Some(first) => first.to_uppercase().collect::<String>() + c.as_str(),
                None => model.to_string(),
            }
        };
        format!("{capitalised} (Board)")
    }
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

#[cfg(test)]
mod tests {
    use super::*;

    fn make_task(id: &str, deps: &[&str]) -> PraxisTask {
        PraxisTask {
            id: id.to_string(),
            description: format!("Task {id}"),
            domain: Domain::Backend,
            complexity: TaskComplexity::Simple,
            dependencies: deps.iter().map(|s| s.to_string()).collect(),
            budget: Budget::new(10_000, 10, 1.0),
            files_hint: vec![],
        }
    }

    #[test]
    fn topological_sort_empty() {
        let waves = topological_sort(&[]).unwrap();
        assert!(waves.is_empty());
    }

    #[test]
    fn topological_sort_no_deps() {
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &[]),
            make_task("t3", &[]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(
            waves.len(),
            1,
            "all tasks with no deps should be in one wave"
        );
        assert_eq!(waves[0].len(), 3);
    }

    #[test]
    fn topological_sort_linear_chain() {
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &["t1"]),
            make_task("t3", &["t2"]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(waves.len(), 3);
        assert_eq!(waves[0][0].id, "t1");
        assert_eq!(waves[1][0].id, "t2");
        assert_eq!(waves[2][0].id, "t3");
    }

    #[test]
    fn topological_sort_diamond() {
        // t1 -> t2, t1 -> t3, t2+t3 -> t4
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &["t1"]),
            make_task("t3", &["t1"]),
            make_task("t4", &["t2", "t3"]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(waves.len(), 3);
        assert_eq!(waves[0].len(), 1); // t1
        assert_eq!(waves[1].len(), 2); // t2, t3 in parallel
        assert_eq!(waves[2].len(), 1); // t4
        assert_eq!(waves[2][0].id, "t4");
    }

    #[test]
    fn topological_sort_cycle_detected() {
        let tasks = vec![make_task("t1", &["t2"]), make_task("t2", &["t1"])];
        let result = topological_sort(&tasks);
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("cycle"), "error should mention cycle: {err}");
    }

    #[test]
    fn topological_sort_unknown_dep() {
        let tasks = vec![make_task("t1", &["t99"])];
        let result = topological_sort(&tasks);
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(
            err.contains("unknown task id"),
            "error should mention unknown: {err}"
        );
    }

    #[test]
    fn topological_sort_mixed_waves() {
        // t1, t2 no deps; t3 depends on t1; t4 depends on t1 and t2
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &[]),
            make_task("t3", &["t1"]),
            make_task("t4", &["t1", "t2"]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(waves.len(), 2);
        assert_eq!(waves[0].len(), 2); // t1, t2
        assert_eq!(waves[1].len(), 2); // t3, t4
    }
}
