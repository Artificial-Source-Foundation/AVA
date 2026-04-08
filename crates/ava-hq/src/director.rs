//! Director — top-level orchestrator that delegates to domain-specific leads.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use ava_llm::provider::LLMProvider;
use ava_platform::StandardPlatform;
use ava_types::{AvaError, Message, Result, Role, Session};
use futures::future::join_all;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

use crate::board;
use crate::events::HqEvent;
use crate::lead::Lead;
use crate::plan::{ExecutionGroup, HqPlan, HqTask, PlannerConfig, TaskComplexity};
use crate::routing::derive_board_name;
use crate::scout;
use crate::worker::{run_worker, Worker};
use crate::{Budget, Domain, Task, TaskType};

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
    /// Custom system prompt overrides per lead domain.
    pub lead_prompts: HashMap<Domain, String>,
    /// Optional separate provider for workers (cheaper/faster than the lead's provider).
    /// When set, workers will use this provider instead of inheriting the lead's provider.
    pub worker_provider: Option<Arc<dyn LLMProvider>>,
}

impl DirectorConfig {
    pub fn provider_for(&self, domain: Domain) -> Arc<dyn LLMProvider> {
        self.domain_providers
            .get(&domain)
            .cloned()
            .unwrap_or_else(|| self.default_provider.clone())
    }

    // CLI tier routing is now handled by AcpProviderFactory registered on the ModelRouter.
    // No need for separate discovery — agents are created on-demand via provider="acp".
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
                let custom_prompt = config
                    .lead_prompts
                    .get(&domain)
                    .cloned()
                    .unwrap_or_default();
                let mut lead = Lead::new(
                    name,
                    domain.clone(),
                    config.provider_for(domain),
                    platform.clone(),
                )
                .with_worker_names(worker_names.clone())
                .with_custom_prompt(custom_prompt);
                if let Some(ref wp) = config.worker_provider {
                    lead = lead.with_worker_provider(wp.clone());
                }
                lead
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
        event_tx: mpsc::UnboundedSender<HqEvent>,
    ) -> Result<Session> {
        let futures = workers.into_iter().map(|worker| {
            let cancel = cancel.clone();
            let tx = event_tx.clone();
            let timeout = Duration::from_secs((worker.budget.max_turns * 60) as u64);

            async move {
                let _ = tx.send(HqEvent::WorkerStarted {
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
                        let _ = tx.send(HqEvent::WorkerCompleted {
                            worker_id: worker.id,
                            success: true,
                            turns: session.messages.len(),
                        });
                    }
                    Err(error) => {
                        let _ = tx.send(HqEvent::WorkerFailed {
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

        let _ = event_tx.send(HqEvent::AllComplete {
            total_workers: results.len(),
            succeeded,
            failed,
        });

        let _ = event_tx.send(HqEvent::Summary {
            total_workers: results.len(),
            succeeded,
            failed,
            total_turns,
        });

        Ok(combined)
    }

    /// Dispatch scouts to investigate multiple queries in parallel.
    ///
    /// Each query spawns one [`scout::Scout`] with read-only tools. All scouts run
    /// concurrently and their reports are collected.  Events are emitted via
    /// `event_tx` for progress tracking.
    ///
    /// Requires a platform (returns an error if `self.platform` is `None`).
    pub async fn scout(
        &self,
        queries: Vec<String>,
        cwd: &std::path::Path,
        event_tx: mpsc::UnboundedSender<HqEvent>,
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

                let _ = tx.send(HqEvent::ScoutStarted {
                    id,
                    query: query.clone(),
                });

                match scout_instance.investigate(&query, &cwd).await {
                    Ok(report) => {
                        let _ = tx.send(HqEvent::ScoutCompleted {
                            id,
                            query: report.query.clone(),
                            files_examined: report.files_examined.len(),
                            snippets_found: report.relevant_code.len(),
                        });
                        Some(report)
                    }
                    Err(e) => {
                        let _ = tx.send(HqEvent::ScoutFailed {
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
    /// Creates a [`board::Board`] from the configured `board_providers`, assigning each
    /// a rotating personality (Analytical, Pragmatic, Creative).  The board
    /// members evaluate the goal and scout reports in parallel and vote on the
    /// approach.
    ///
    /// Returns `None` if no board providers are configured.
    pub async fn consult_board(
        &self,
        goal: &str,
        scout_reports: &[scout::ScoutReport],
        event_tx: mpsc::UnboundedSender<HqEvent>,
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
        let _ = event_tx.send(HqEvent::BoardConvened {
            members: member_names,
        });

        let board = board::Board::new(members);
        let result = board.convene(goal, scout_reports).await?;

        // Emit individual opinion events
        for opinion in &result.opinions {
            let _ = event_tx.send(HqEvent::BoardOpinion {
                member: opinion.member_name.clone(),
                vote: opinion.vote.to_string(),
                summary: opinion.recommendation.clone(),
            });
        }

        // Emit the final result event
        let _ = event_tx.send(HqEvent::BoardResult {
            consensus: result.consensus.clone(),
            vote_summary: result.vote_summary.clone(),
        });

        Ok(Some(result))
    }

    /// Use the Director's LLM to analyze the goal and produce a structured plan.
    ///
    /// If LLM planning fails, falls back to the static `pick_domain()` routing
    /// by returning a single-task plan mapped via `TaskType`.
    pub async fn plan(&self, goal: &str, context: Option<&str>) -> Result<HqPlan> {
        self.plan_with_config(goal, context, &PlannerConfig::default())
            .await
    }

    /// Like [`plan`](Self::plan) but with explicit planner configuration.
    pub async fn plan_with_config(
        &self,
        goal: &str,
        context: Option<&str>,
        config: &PlannerConfig,
    ) -> Result<HqPlan> {
        if !config.enabled {
            return self.fallback_plan(goal);
        }

        match crate::plan::create_plan(
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
    /// Emits `HqEvent::PlanCreated` before execution begins.
    pub async fn execute_plan(
        &mut self,
        plan: HqPlan,
        cancel: CancellationToken,
        event_tx: mpsc::UnboundedSender<HqEvent>,
    ) -> Result<Session> {
        let _ = event_tx.send(HqEvent::PlanCreated { plan: plan.clone() });

        let mut combined = Session::new();

        for (group_idx, group) in plan.execution_groups.iter().enumerate() {
            if cancel.is_cancelled() {
                break;
            }

            let _ = event_tx.send(HqEvent::PhaseStarted {
                phase_index: group_idx,
                phase_count: plan.execution_groups.len(),
                phase_name: group.label.clone(),
                role: "director".to_string(),
            });

            // Collect plan tasks for this group
            let group_tasks: Vec<HqTask> = group
                .task_ids
                .iter()
                .filter_map(|task_id| plan.tasks.iter().find(|t| &t.id == task_id).cloned())
                .collect();

            if group_tasks.is_empty() {
                continue;
            }

            // Group tasks by domain so each lead manages its own
            let mut tasks_by_domain: HashMap<Domain, Vec<HqTask>> = HashMap::new();
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

            let _ = event_tx.send(HqEvent::PhaseCompleted {
                phase_index: group_idx,
                phase_name: group.label.clone(),
                turns: group_turns,
                output_preview: String::new(),
            });
        }

        Ok(combined)
    }

    /// Produce a single-task fallback plan using the static `pick_domain()` logic.
    fn fallback_plan(&self, goal: &str) -> Result<HqPlan> {
        let task = Task {
            description: goal.to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        };
        let domain = self.pick_domain(&task);

        Ok(HqPlan {
            goal: goal.to_string(),
            tasks: vec![HqTask {
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
            TaskType::Chat => Domain::Fullstack,
            TaskType::Simple => Domain::Fullstack,
        }
    }
}
