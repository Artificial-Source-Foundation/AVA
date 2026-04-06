//! Lead — domain-specific team leads that manage workers.

use std::sync::Arc;
use std::time::Duration;

use ava_agent::{AgentConfig, AgentLoop};
use ava_config::AgentRoleProfile;
use ava_context::ContextManager;
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_platform::StandardPlatform;
use ava_tools::registry::ToolRegistry;
use ava_types::{AvaError, Message, Result, Role, Session};
use futures::future::join_all;
use serde::Deserialize;
use tokio::sync::{mpsc, Mutex};
use uuid::Uuid;

use crate::events::HqEvent;
use crate::plan::HqTask;
use crate::prompts;
use crate::routing::{domain_to_task_type, topological_sort};
use crate::worker::{run_worker, Worker};
use crate::{Budget, Domain, Task, TaskType};

/// Worker name pool from the HQ design spec.
const WORKER_NAMES: &[&str] = &[
    "Pedro", "Sofia", "Luna", "Kai", "Mira", "Rio", "Ash", "Nico", "Ivy", "Juno", "Zara", "Leo",
];

/// Pick a worker name from the pool, cycling if more workers than names.
fn worker_name_for_index(index: usize) -> &'static str {
    WORKER_NAMES[index % WORKER_NAMES.len()]
}

pub struct Lead {
    name: String,
    pub(crate) domain: Domain,
    pub(crate) workers: Vec<Worker>,
    provider: Arc<dyn LLMProvider>,
    /// Optional separate provider for workers (cheaper/faster than the lead's provider).
    /// When set, `build_worker()` uses this instead of `self.provider`.
    worker_provider: Option<Arc<dyn LLMProvider>>,
    platform: Option<Arc<StandardPlatform>>,
    /// Custom worker names pool (empty = use built-in default).
    worker_names: Vec<String>,
    /// Custom system prompt override (empty = use default from prompts.rs).
    custom_prompt: String,
    /// Resolved role profile for workers spawned by this lead.
    worker_role_profile: Option<AgentRoleProfile>,
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
            worker_provider: None,
            platform,
            worker_names: Vec::new(),
            custom_prompt: String::new(),
            worker_role_profile: None,
        }
    }

    /// Create a new lead with custom worker names.
    pub fn with_worker_names(mut self, names: Vec<String>) -> Self {
        self.worker_names = names;
        self
    }

    /// Set a separate provider for workers (cheaper/faster than the lead's provider).
    pub fn with_worker_provider(mut self, provider: Arc<dyn LLMProvider>) -> Self {
        self.worker_provider = Some(provider);
        self
    }

    /// Set a custom system prompt override for this lead's workers.
    pub fn with_custom_prompt(mut self, prompt: String) -> Self {
        self.custom_prompt = prompt;
        self
    }

    /// Set the resolved role profile for workers spawned by this lead.
    pub fn with_worker_role_profile(mut self, profile: AgentRoleProfile) -> Self {
        self.worker_role_profile = Some(profile);
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
        tasks: Vec<HqTask>,
        budget: &Budget,
        cancel: tokio_util::sync::CancellationToken,
        event_tx: mpsc::UnboundedSender<HqEvent>,
    ) -> Result<(Session, Vec<(Uuid, bool)>)> {
        let waves = topological_sort(&tasks)?;

        let total_tasks = tasks.len();
        let total_waves = waves.len();

        let _ = event_tx.send(HqEvent::LeadExecutionStarted {
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
            let _ = event_tx.send(HqEvent::LeadWaveStarted {
                lead: self.name.clone(),
                wave_index: wave_idx,
                task_count: wave_task_count,
            });

            // Spawn workers for this wave
            let mut workers = Vec::new();
            for hq_task in wave {
                let task = Task {
                    description: hq_task.description.clone(),
                    task_type: domain_to_task_type(&hq_task.domain),
                    files: hq_task.files_hint.clone(),
                };
                let worker_budget = if hq_task.budget.max_turns > 0 {
                    hq_task.budget.clone()
                } else {
                    budget.clone()
                };
                match self.build_worker(task, worker_budget) {
                    Ok(worker) => {
                        self.workers.push(worker.clone());
                        workers.push(worker);
                    }
                    Err(err) => {
                        tracing::warn!(task_id = %hq_task.id, %err, "failed to spawn worker");
                        total_failed += 1;
                    }
                }
            }

            if workers.is_empty() {
                let _ = event_tx.send(HqEvent::LeadWaveCompleted {
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

            let _ = event_tx.send(HqEvent::LeadWaveCompleted {
                lead: self.name.clone(),
                wave_index: wave_idx,
                succeeded: wave_succeeded,
                failed: wave_failed,
            });
        }

        let _ = event_tx.send(HqEvent::LeadExecutionCompleted {
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
        event_tx: mpsc::UnboundedSender<HqEvent>,
    ) -> Result<usize> {
        let _ = event_tx.send(HqEvent::LeadReviewStarted {
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

        let _ = event_tx.send(HqEvent::LeadReviewCompleted {
            lead: self.name.clone(),
            issues_found,
        });

        Ok(issues_found)
    }

    pub(crate) fn build_worker(&self, task: Task, worker_budget: Budget) -> Result<Worker> {
        let effective_provider = self
            .worker_provider
            .as_ref()
            .unwrap_or(&self.provider)
            .clone();
        let model_name = effective_provider.model_name().to_string();
        let worker_id = Uuid::new_v4();

        let idx = self.workers.len();
        let worker_name: String = if self.worker_names.is_empty() {
            worker_name_for_index(idx).to_string()
        } else {
            self.worker_names[idx % self.worker_names.len()].clone()
        };

        // Resolve system prompt from worker role profile or fallback to legacy prompts
        let (system_prompt, thinking_level, extended_tools) =
            if let Some(ref profile) = self.worker_role_profile {
                let resolved = ava_config::apply_template_vars(
                    profile,
                    &[
                        ("name", &worker_name),
                        ("domain", prompts::domain_label(&self.domain)),
                    ],
                );
                let mut prompt = resolved.system_prompt.clone();
                if !resolved.system_prompt_suffix.is_empty() {
                    prompt.push_str("\n\n");
                    prompt.push_str(&resolved.system_prompt_suffix);
                }
                if !self.custom_prompt.is_empty() {
                    prompt.push_str("\n\n## Lead Instructions\n");
                    prompt.push_str(&self.custom_prompt);
                }
                let thinking = resolved
                    .thinking_level
                    .unwrap_or(ava_types::ThinkingLevel::Off);
                let extended = resolved.extended_tools.unwrap_or(false);
                (prompt, thinking, extended)
            } else {
                let prompt = if self.custom_prompt.is_empty() {
                    prompts::worker_system_prompt_for_domain(&worker_name, &self.domain)
                } else {
                    format!(
                        "{}\n\n## Lead Instructions\n{}",
                        prompts::worker_system_prompt_for_domain(&worker_name, &self.domain),
                        self.custom_prompt
                    )
                };
                (prompt, ava_types::ThinkingLevel::Off, true)
            };

        // Build tool registry from worker role profile or fallback to all core tools
        let registry = if let (Some(ref profile), Some(platform)) =
            (&self.worker_role_profile, &self.platform)
        {
            let resolved = ava_config::apply_template_vars(
                profile,
                &[
                    ("name", &worker_name),
                    ("domain", prompts::domain_label(&self.domain)),
                ],
            );
            let (reg, _backup) =
                crate::role_tools::build_registry_for_role(&resolved, platform.clone());
            reg
        } else {
            let mut reg = ToolRegistry::new();
            if let Some(platform) = &self.platform {
                ava_tools::core::register_core_tools(&mut reg, platform.clone());
            }
            reg
        };

        let agent = AgentLoop::new(
            Box::new(SharedProvider::new(effective_provider.clone())),
            registry,
            ContextManager::new(worker_budget.max_tokens),
            AgentConfig {
                max_turns: worker_budget.max_turns,
                max_budget_usd: 0.0,
                token_limit: worker_budget.max_tokens,
                provider: String::new(),
                model: model_name,
                max_cost_usd: worker_budget.max_cost_usd,
                loop_detection: true,
                custom_system_prompt: Some(system_prompt),
                thinking_level: if matches!(task.task_type, TaskType::Chat) {
                    ava_types::ThinkingLevel::Medium
                } else {
                    thinking_level
                },
                thinking_budget_tokens: None,
                system_prompt_suffix: None,
                project_root: None,
                enable_dynamic_rules: false,
                extended_tools,
                plan_mode: false,
                post_edit_validation: None,
                auto_compact: true,
                stream_timeout_secs: ava_agent::agent_loop::LLM_STREAM_TIMEOUT_SECS,
                prompt_caching: true,
                headless: true,
                is_subagent: true,
            },
        );

        Ok(Worker {
            id: worker_id,
            lead: self.name.clone(),
            agent: Arc::new(Mutex::new(agent)),
            budget: worker_budget,
            task,
            provider: effective_provider,
        })
    }
}
