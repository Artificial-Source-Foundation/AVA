use super::*;
use crate::state::messages::UiMessage;
use crate::state::praxis::{PraxisTaskStatus, PraxisWorkerState};
use ava_praxis::{Budget, Director, DirectorConfig, PraxisEvent};
use std::collections::HashMap;

impl App {
    pub(crate) fn launch_praxis_task(
        &mut self,
        goal: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let Some(stack) = self.state.agent.stack_handle() else {
            self.set_status(
                "Cannot launch Praxis: AgentStack not initialised",
                StatusLevel::Error,
            );
            return;
        };

        let task_id = self.state.praxis.add_task(goal.clone());
        let cancel = tokio_util::sync::CancellationToken::new();
        if let Some(task) = self.state.praxis.task_mut(task_id) {
            task.status = PraxisTaskStatus::Running;
            task.cancel = Some(cancel.clone());
        }
        self.state.view_mode = ViewMode::PraxisTask {
            task_id,
            goal: goal.clone(),
        };
        self.state.messages.reset_scroll();

        let provider_name = self.state.agent.provider_name.clone();
        let model_name = self.state.agent.model_name.clone();
        let max_turns = self.state.agent.max_turns;
        let max_budget_usd = self.state.agent.max_budget_usd;

        tokio::spawn(async move {
            let provider = match stack
                .router
                .route_required(&provider_name, &model_name)
                .await
            {
                Ok(provider) => provider,
                Err(err) => {
                    let _ = app_tx.send(AppEvent::PraxisRunDone {
                        task_id,
                        result: Err(err.to_string()),
                    });
                    return;
                }
            };

            let platform = Arc::new(ava_platform::StandardPlatform);
            let mut director = Director::new(DirectorConfig {
                budget: Budget::interactive(max_turns, max_budget_usd),
                default_provider: provider,
                domain_providers: HashMap::new(),
                platform: Some(platform),
                scout_provider: None,
                board_providers: vec![],
            });

            let (tx, mut rx) = mpsc::unbounded_channel();
            let relay_tx = app_tx.clone();
            let relay_task_id = task_id;
            let relay = tokio::spawn(async move {
                while let Some(event) = rx.recv().await {
                    let _ = relay_tx.send(AppEvent::PraxisRunEvent {
                        task_id: relay_task_id,
                        event,
                    });
                }
            });

            // Step 1: Run scouts
            let cwd = std::env::current_dir().unwrap_or_default();
            let scout_reports = director
                .scout(
                    vec![format!("Analyze codebase for: {}", goal)],
                    &cwd,
                    tx.clone(),
                )
                .await;

            // Step 2: Build context from scout reports
            let context = if scout_reports.is_empty() {
                None
            } else {
                Some(
                    scout_reports
                        .iter()
                        .map(|r| r.as_summary())
                        .collect::<Vec<_>>()
                        .join("\n\n"),
                )
            };

            // Step 3: Create plan using LLM
            let plan = match director.plan(&goal, context.as_deref()).await {
                Ok(plan) => plan,
                Err(err) => {
                    let _ = app_tx.send(AppEvent::PraxisRunDone {
                        task_id,
                        result: Err(format!("Planning failed: {err}")),
                    });
                    return;
                }
            };

            // Step 4: Execute plan with sequential groups
            let result = director.execute_plan(plan, cancel, tx).await;
            let _ = relay.await;
            let _ = app_tx.send(AppEvent::PraxisRunDone {
                task_id,
                result: result.map_err(|err| err.to_string()),
            });
        });

        self.set_status(
            format!("Praxis task #{task_id} launched"),
            StatusLevel::Info,
        );
    }

    pub(crate) fn handle_praxis_event(&mut self, task_id: usize, event: PraxisEvent) {
        let Some(task) = self.state.praxis.task_mut(task_id) else {
            return;
        };

        match event {
            PraxisEvent::ScoutStarted { query, .. } => {
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!("Scout investigating: {query}"),
                ));
            }
            PraxisEvent::ScoutCompleted {
                query,
                files_examined,
                snippets_found,
                ..
            } => {
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!(
                        "Scout completed: {query} ({files_examined} files, {snippets_found} snippets)"
                    ),
                ));
            }
            PraxisEvent::ScoutFailed { query, error, .. } => {
                task.messages.push(UiMessage::new(
                    MessageKind::Error,
                    format!("Scout failed: {query} — {error}"),
                ));
            }
            PraxisEvent::PlanCreated { ref plan } => {
                let mut plan_msg = format!(
                    "Plan: {} ({} tasks, {} phases)\n",
                    plan.goal,
                    plan.tasks.len(),
                    plan.execution_groups.len()
                );
                for t in &plan.tasks {
                    plan_msg.push_str(&format!("  [{}] {:?}: {}\n", t.id, t.domain, t.description));
                }
                task.messages
                    .push(UiMessage::new(MessageKind::System, plan_msg));
            }
            PraxisEvent::PhaseStarted {
                phase_index,
                phase_count,
                phase_name,
                ..
            } => {
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!("Phase {}/{}: {}", phase_index + 1, phase_count, phase_name),
                ));
            }
            PraxisEvent::PhaseCompleted {
                phase_name, turns, ..
            } => {
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!("Phase completed: {phase_name} ({turns} turns)"),
                ));
            }
            PraxisEvent::LeadExecutionStarted {
                ref lead,
                total_tasks,
                total_waves,
            } => {
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!("{lead}: executing {total_tasks} task(s) in {total_waves} wave(s)"),
                ));
            }
            PraxisEvent::LeadReviewCompleted {
                ref lead,
                issues_found,
            } => {
                let msg = if issues_found > 0 {
                    format!("{lead} review: {issues_found} issue(s) found")
                } else {
                    format!("{lead} review: passed")
                };
                task.messages.push(UiMessage::new(MessageKind::System, msg));
            }
            PraxisEvent::WorkerStarted {
                worker_id,
                lead,
                task_description,
            } => {
                task.workers.push(PraxisWorkerState {
                    worker_id,
                    lead: lead.clone(),
                    task_description: task_description.clone(),
                    status: PraxisTaskStatus::Running,
                    turn: 0,
                    max_turns: 0,
                });
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!("{lead} started: {task_description}"),
                ));
            }
            PraxisEvent::WorkerProgress {
                worker_id,
                turn,
                max_turns,
            } => {
                if let Some(worker) = task.workers.iter_mut().find(|w| w.worker_id == worker_id) {
                    worker.turn = turn;
                    worker.max_turns = max_turns;
                }
                let lead = task
                    .workers
                    .iter()
                    .find(|w| w.worker_id == worker_id)
                    .map(|w| w.lead.clone())
                    .unwrap_or_else(|| "worker".to_string());
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!("{lead} progress: {turn}/{max_turns}"),
                ));
            }
            PraxisEvent::WorkerToken { worker_id, token } => {
                let lead = task
                    .workers
                    .iter()
                    .find(|w| w.worker_id == worker_id)
                    .map(|w| w.lead.clone())
                    .unwrap_or_else(|| "worker".to_string());
                let prefix = format!("[{lead}] ");
                if let Some(last) = task.messages.last_mut() {
                    if matches!(last.kind, MessageKind::Assistant)
                        && last.content.starts_with(&prefix)
                    {
                        last.content.push_str(&token);
                        return;
                    }
                }
                task.messages.push(UiMessage::new(
                    MessageKind::Assistant,
                    format!("{prefix}{token}"),
                ));
            }
            PraxisEvent::WorkerCompleted {
                worker_id,
                success,
                turns,
            } => {
                if let Some(worker) = task.workers.iter_mut().find(|w| w.worker_id == worker_id) {
                    worker.status = if success {
                        PraxisTaskStatus::Completed
                    } else {
                        PraxisTaskStatus::Failed
                    };
                    worker.turn = turns;
                }
                let lead = task
                    .workers
                    .iter()
                    .find(|w| w.worker_id == worker_id)
                    .map(|w| w.lead.clone())
                    .unwrap_or_else(|| "worker".to_string());
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!("{lead} completed in {turns} turns"),
                ));
            }
            PraxisEvent::WorkerFailed { worker_id, error } => {
                if let Some(worker) = task.workers.iter_mut().find(|w| w.worker_id == worker_id) {
                    worker.status = PraxisTaskStatus::Failed;
                }
                task.messages.push(UiMessage::new(
                    MessageKind::Error,
                    format!("Praxis worker failed: {error}"),
                ));
            }
            PraxisEvent::Summary {
                total_workers,
                succeeded,
                failed,
                total_turns,
            } => {
                task.messages.push(UiMessage::new(
                    MessageKind::System,
                    format!(
                        "Praxis summary: {succeeded}/{total_workers} workers succeeded, {failed} failed, {total_turns} turns"
                    ),
                ));
            }
            _ => {
                // Other events (Board, ACP, Artifact, etc.) will be surfaced in a richer Praxis view later.
            }
        }
    }
}
