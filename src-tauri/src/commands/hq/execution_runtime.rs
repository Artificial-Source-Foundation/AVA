use ava_agent::stack::AgentStack;
use ava_config::HqConfig as HqSettingsConfig;
use ava_db::models::{HqAgentRecord, HqAgentTranscriptRecord, HqPlanRecord};
use ava_db::{models::HqIssueRecord, HqRepository};
use ava_hq::{Budget, Domain, HqEvent, HqPlan, TaskComplexity};
use tauri::{AppHandle, Emitter, Manager};
use tokio::sync::mpsc;
use tracing::warn;
use uuid::Uuid;

use super::data::{now_ms, serialize_json, HqPlanDto};
use super::director_runtime::{append_activity, append_chat_message, build_director};
use super::mappings::role_icon;
use super::plan_commands::validate_parallel_phase_dependencies;
use crate::commands::helpers::parse_domain;

async fn find_issue_by_task_title(
    repo: &HqRepository,
    epic_id: &str,
    task_description: &str,
) -> Option<HqIssueRecord> {
    match repo.list_issues(Some(epic_id)).await {
        Ok(issues) => issues
            .into_iter()
            .find(|issue| issue.title.trim() == task_description.trim()),
        Err(error) => {
            warn!(%error, epic_id = %epic_id, task = %task_description, "failed to load HQ issues while matching runtime task");
            None
        }
    }
}

pub(super) async fn persist_runtime_event(repo: &HqRepository, epic_id: &str, event: &HqEvent) {
    match event {
        HqEvent::WorkerStarted {
            worker_id,
            lead,
            task_description,
        } => {
            let matched_issue = find_issue_by_task_title(repo, epic_id, task_description).await;
            let issue_id = matched_issue.as_ref().map(|issue| issue.id.clone());

            let _ = repo
                .upsert_agent(&HqAgentRecord {
                    id: worker_id.to_string(),
                    name: lead.clone(),
                    role: lead.clone(),
                    tier: "worker".to_string(),
                    model: "runtime".to_string(),
                    status: "running".to_string(),
                    icon: role_icon("worker").to_string(),
                    parent_id: Some("director".to_string()),
                    current_task: Some(task_description.clone()),
                    current_issue_id: issue_id.clone(),
                    turn: Some(0),
                    max_turns: None,
                    assigned_issue_ids_json: Some(
                        serialize_json(&issue_id.iter().cloned().collect::<Vec<_>>())
                            .unwrap_or_else(|_| "[]".to_string()),
                    ),
                    files_touched_json: Some("[]".to_string()),
                    total_cost_usd: 0.0,
                    created_at: now_ms(),
                    updated_at: now_ms(),
                })
                .await;

            if let Some(mut issue) = matched_issue {
                issue.status = "in-progress".to_string();
                issue.is_live = 1;
                issue.agent_live_action = Some(task_description.clone());
                issue.updated_at = now_ms();
                let _ = repo.update_issue(&issue).await;
            }

            append_activity(
                repo,
                "delegation",
                Some(lead),
                format!("{lead} started '{task_description}'"),
            )
            .await;
        }
        HqEvent::WorkerProgress {
            worker_id,
            turn,
            max_turns,
        } => {
            if let Ok(Some(mut agent)) = repo.get_agent(&worker_id.to_string()).await {
                agent.turn = Some(*turn as i64);
                agent.max_turns = Some(*max_turns as i64);
                agent.updated_at = now_ms();
                let _ = repo.upsert_agent(&agent).await;
                if let Some(issue_id) = agent.current_issue_id {
                    if let Ok(Some(mut issue)) = repo.get_issue(&issue_id).await {
                        issue.agent_turn = Some(*turn as i64);
                        issue.agent_max_turns = Some(*max_turns as i64);
                        issue.updated_at = now_ms();
                        let _ = repo.update_issue(&issue).await;
                    }
                }
            }
        }
        HqEvent::WorkerToken { worker_id, token } => {
            let _ = repo
                .append_agent_transcript(&HqAgentTranscriptRecord {
                    id: Uuid::new_v4().to_string(),
                    agent_id: worker_id.to_string(),
                    entry_type: "message".to_string(),
                    tool_name: None,
                    tool_path: None,
                    tool_status: None,
                    content: token.clone(),
                    timestamp: now_ms(),
                })
                .await;
        }
        HqEvent::ExternalWorkerStarted {
            worker_id,
            lead,
            agent_name,
            task_description,
        } => {
            let matched_issue = find_issue_by_task_title(repo, epic_id, task_description).await;
            let issue_id = matched_issue.as_ref().map(|issue| issue.id.clone());

            let existing_cost = repo
                .get_agent(&worker_id.to_string())
                .await
                .ok()
                .flatten()
                .map(|agent| agent.total_cost_usd)
                .unwrap_or(0.0);

            let _ = repo
                .upsert_agent(&HqAgentRecord {
                    id: worker_id.to_string(),
                    name: agent_name.clone(),
                    role: lead.clone(),
                    tier: "worker".to_string(),
                    model: agent_name.clone(),
                    status: "running".to_string(),
                    icon: role_icon("worker").to_string(),
                    parent_id: Some("director".to_string()),
                    current_task: Some(task_description.clone()),
                    current_issue_id: issue_id.clone(),
                    turn: Some(0),
                    max_turns: None,
                    assigned_issue_ids_json: Some(
                        serialize_json(&issue_id.iter().cloned().collect::<Vec<_>>())
                            .unwrap_or_else(|_| "[]".to_string()),
                    ),
                    files_touched_json: Some("[]".to_string()),
                    total_cost_usd: existing_cost,
                    created_at: now_ms(),
                    updated_at: now_ms(),
                })
                .await;

            append_activity(
                repo,
                "delegation",
                Some(agent_name),
                format!("{agent_name} started '{task_description}'"),
            )
            .await;
        }
        HqEvent::ExternalWorkerThinking { worker_id, content } => {
            let _ = repo
                .append_agent_transcript(&HqAgentTranscriptRecord {
                    id: Uuid::new_v4().to_string(),
                    agent_id: worker_id.to_string(),
                    entry_type: "thinking".to_string(),
                    tool_name: None,
                    tool_path: None,
                    tool_status: None,
                    content: content.clone(),
                    timestamp: now_ms(),
                })
                .await;
        }
        HqEvent::ExternalWorkerText { worker_id, content } => {
            let _ = repo
                .append_agent_transcript(&HqAgentTranscriptRecord {
                    id: Uuid::new_v4().to_string(),
                    agent_id: worker_id.to_string(),
                    entry_type: "message".to_string(),
                    tool_name: None,
                    tool_path: None,
                    tool_status: None,
                    content: content.clone(),
                    timestamp: now_ms(),
                })
                .await;
        }
        HqEvent::ExternalWorkerToolUse {
            worker_id,
            tool_name,
        } => {
            let _ = repo
                .append_agent_transcript(&HqAgentTranscriptRecord {
                    id: Uuid::new_v4().to_string(),
                    agent_id: worker_id.to_string(),
                    entry_type: "tool-call".to_string(),
                    tool_name: Some(tool_name.clone()),
                    tool_path: None,
                    tool_status: Some("running".to_string()),
                    content: format!("Using {tool_name}"),
                    timestamp: now_ms(),
                })
                .await;
        }
        HqEvent::ExternalWorkerCompleted {
            worker_id,
            success,
            cost_usd,
            turns,
            ..
        } => {
            if let Ok(Some(mut agent)) = repo.get_agent(&worker_id.to_string()).await {
                agent.status = if *success { "idle" } else { "error" }.to_string();
                agent.turn = Some(*turns as i64);
                agent.total_cost_usd += cost_usd.unwrap_or(0.0);
                agent.updated_at = now_ms();
                let current_issue_id = agent.current_issue_id.clone();
                let agent_name = agent.name.clone();
                let total_cost = agent.total_cost_usd;
                let _ = repo.upsert_agent(&agent).await;

                if let Some(issue_id) = current_issue_id {
                    if let Ok(Some(mut issue)) = repo.get_issue(&issue_id).await {
                        issue.status = if *success {
                            "review".to_string()
                        } else {
                            "backlog".to_string()
                        };
                        issue.is_live = 0;
                        issue.agent_turn = Some(*turns as i64);
                        issue.agent_live_action = None;
                        issue.updated_at = now_ms();
                        let _ = repo.update_issue(&issue).await;
                    }
                }

                let message = if let Some(cost) = cost_usd {
                    format!("{agent_name} completed work at ${cost:.4} (${total_cost:.4} total PAYG spend)")
                } else {
                    format!("{agent_name} completed work")
                };
                append_activity(
                    repo,
                    if *success { "completion" } else { "error" },
                    Some(&agent_name),
                    message,
                )
                .await;
            }
        }
        HqEvent::ExternalWorkerFailed { worker_id, error } => {
            if let Ok(Some(mut agent)) = repo.get_agent(&worker_id.to_string()).await {
                agent.status = "error".to_string();
                agent.updated_at = now_ms();
                let _ = repo.upsert_agent(&agent).await;
            }
            append_activity(
                repo,
                "error",
                None,
                format!("External worker {} failed: {}", worker_id, error),
            )
            .await;
        }
        HqEvent::WorkerCompleted {
            worker_id,
            success,
            turns,
        } => {
            if let Ok(Some(mut agent)) = repo.get_agent(&worker_id.to_string()).await {
                agent.status = if *success { "idle" } else { "error" }.to_string();
                agent.turn = Some(*turns as i64);
                agent.updated_at = now_ms();
                let current_issue_id = agent.current_issue_id.clone();
                let _ = repo.upsert_agent(&agent).await;

                if let Some(issue_id) = current_issue_id {
                    if let Ok(Some(mut issue)) = repo.get_issue(&issue_id).await {
                        issue.status = if *success {
                            "review".to_string()
                        } else {
                            "backlog".to_string()
                        };
                        issue.is_live = 0;
                        issue.agent_turn = Some(*turns as i64);
                        issue.agent_live_action = None;
                        issue.updated_at = now_ms();
                        let _ = repo.update_issue(&issue).await;
                    }
                }
            }
            append_activity(
                repo,
                if *success { "completion" } else { "error" },
                None,
                format!(
                    "Worker {} {}",
                    worker_id,
                    if *success { "completed" } else { "failed" }
                ),
            )
            .await;
        }
        HqEvent::WorkerFailed { worker_id, error } => {
            if let Ok(Some(mut agent)) = repo.get_agent(&worker_id.to_string()).await {
                agent.status = "error".to_string();
                agent.updated_at = now_ms();
                let _ = repo.upsert_agent(&agent).await;
            }
            append_activity(
                repo,
                "error",
                None,
                format!("Worker {} failed: {}", worker_id, error),
            )
            .await;
        }
        HqEvent::PhaseStarted { phase_name, .. } => {
            append_activity(
                repo,
                "status-change",
                Some("Director"),
                format!("Started {phase_name}"),
            )
            .await;
        }
        HqEvent::PhaseCompleted { phase_name, .. } => {
            append_activity(
                repo,
                "review",
                Some("Director"),
                format!("Completed {phase_name}"),
            )
            .await;
        }
        HqEvent::AllComplete { .. } => {
            if let Ok(Some(mut epic)) = repo.get_epic(epic_id).await {
                epic.status = "completed".to_string();
                epic.progress = 100;
                epic.updated_at = now_ms();
                let _ = repo.update_epic(&epic).await;
            }
            append_chat_message(
                repo,
                "director",
                "Execution finished. Review the issue board and activity feed for details."
                    .to_string(),
                Some(epic_id),
                vec![],
            )
            .await;
        }
        _ => {}
    }
}

pub(super) async fn start_execution_background(
    app: AppHandle,
    repo: HqRepository,
    stack: std::sync::Arc<AgentStack>,
    epic_id: String,
    plan_record: HqPlanRecord,
    settings: HqSettingsConfig,
) {
    let parsed_plan: HqPlanDto = match serde_json::from_str(&plan_record.plan_json) {
        Ok(plan) => plan,
        Err(error) => {
            let _ = app.emit(
                "agent-event",
                crate::events::AgentEvent::Error {
                    message: format!("Failed to parse HQ plan: {error}"),
                },
            );
            let bridge = app.state::<crate::bridge::DesktopBridge>();
            *bridge.running.write().await = false;
            return;
        }
    };

    if let Err(error) = validate_parallel_phase_dependencies(&parsed_plan) {
        let _ = app.emit(
            "agent-event",
            crate::events::AgentEvent::Error {
                message: format!("HQ plan validation failed: {error}"),
            },
        );
        append_activity(
            &repo,
            "error",
            Some("Director"),
            format!("Execution blocked by invalid plan structure: {error}"),
        )
        .await;
        append_chat_message(
            &repo,
            "director",
            format!(
                "I found a plan structure problem before execution: {error} Please revise the plan so dependent tasks are split across phases."
            ),
            Some(&epic_id),
            vec![],
        )
        .await;
        let bridge = app.state::<crate::bridge::DesktopBridge>();
        *bridge.running.write().await = false;
        return;
    }

    let raw_plan = HqPlan {
        goal: parsed_plan.title.clone(),
        tasks: parsed_plan
            .phases
            .iter()
            .flat_map(|phase| phase.tasks.iter())
            .map(|task| ava_hq::HqTask {
                id: task.id.clone(),
                description: task.title.clone(),
                domain: parse_domain(&task.domain).unwrap_or(Domain::Fullstack),
                complexity: match task.complexity.as_str() {
                    "simple" => TaskComplexity::Simple,
                    "complex" => TaskComplexity::Complex,
                    _ => TaskComplexity::Medium,
                },
                dependencies: task.dependencies.clone(),
                budget: Budget {
                    max_tokens: task.budget_max_tokens,
                    max_turns: task.budget_max_turns,
                    max_cost_usd: task.budget_max_cost_usd,
                },
                files_hint: task.file_hints.clone(),
            })
            .collect(),
        execution_groups: parsed_plan
            .phases
            .iter()
            .map(|phase| ava_hq::ExecutionGroup {
                task_ids: phase.tasks.iter().map(|task| task.id.clone()).collect(),
                label: phase.name.clone(),
            })
            .collect(),
        total_budget: Budget::interactive(200, 10.0),
    };

    let mut director = match build_director(stack.clone(), None, &settings).await {
        Ok(director) => director,
        Err(error) => {
            let _ = app.emit(
                "agent-event",
                crate::events::AgentEvent::Error {
                    message: format!("Failed to build HQ director: {error}"),
                },
            );
            let bridge = app.state::<crate::bridge::DesktopBridge>();
            *bridge.running.write().await = false;
            return;
        }
    };

    let cancel = {
        let bridge = app.state::<crate::bridge::DesktopBridge>();
        bridge.new_cancel_token().await
    };
    let (tx, mut rx) = mpsc::unbounded_channel();

    let app_forward = app.clone();
    let repo_forward = repo.clone();
    let epic_id_forward = epic_id.clone();
    let forwarder = tokio::spawn(async move {
        while let Some(event) = rx.recv().await {
            persist_runtime_event(&repo_forward, &epic_id_forward, &event).await;
            crate::events::emit_hq_event(&app_forward, &event);
        }
    });

    append_chat_message(
        &repo,
        "director",
        "Plan approved. Starting execution across the assigned agents.".to_string(),
        Some(&epic_id),
        vec![],
    )
    .await;

    let result = director.execute_plan(raw_plan, cancel, tx).await;
    let _ = forwarder.await;

    if let Err(error) = result {
        append_activity(
            &repo,
            "error",
            Some("Director"),
            format!("Execution failed: {error}"),
        )
        .await;
        let _ = app.emit(
            "agent-event",
            crate::events::AgentEvent::Error {
                message: format!("HQ execution failed: {error}"),
            },
        );
    }

    let bridge = app.state::<crate::bridge::DesktopBridge>();
    *bridge.running.write().await = false;
}
