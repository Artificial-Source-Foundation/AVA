use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use ava_agent::stack::AgentStack;
use ava_config::HqConfig as HqSettingsConfig;
use ava_db::models::{HqAgentRecord, HqIssueRecord, HqPlanRecord};
use ava_db::HqRepository;
use tauri::{AppHandle, Emitter, Manager};
use tokio::sync::mpsc;
use tracing::warn;
use uuid::Uuid;

use super::data::{
    now_ms, to_string_error, HqDelegationCardDto, HqEpicDto, HqIssueDto, HqPlanDto,
    TeamConfigPayload,
};
use super::director_runtime::{append_activity, append_chat_message, build_director};
use super::mappings::{
    board_review_from_result, commander_planning_context, convert_plan, epic_from_record,
    fallback_hq_plan, role_icon, should_consult_board,
};
use super::plan_commands::validate_parallel_phase_dependencies;

pub(super) async fn create_plan_issues(
    repo: &HqRepository,
    epic_id: &str,
    plan: &HqPlanDto,
) -> Result<Vec<HqIssueDto>, String> {
    let mut created = Vec::new();
    for phase in &plan.phases {
        for task in &phase.tasks {
            let issue_number = repo.next_issue_number().await.map_err(to_string_error)?;
            let now = now_ms();
            let issue_record = HqIssueRecord {
                id: Uuid::new_v4().to_string(),
                issue_number,
                identifier: format!("HQ-{issue_number}"),
                title: task.title.clone(),
                description: task.steps.join("\n"),
                status: "backlog".to_string(),
                priority: match task.complexity.as_str() {
                    "complex" => "urgent".to_string(),
                    "medium" => "high".to_string(),
                    _ => "medium".to_string(),
                },
                assignee_id: task.assignee_id.clone(),
                assignee_name: task.assignee_name.clone(),
                epic_id: epic_id.to_string(),
                phase_label: Some(format!("{} - {}", phase.number, phase.name)),
                agent_turn: None,
                agent_max_turns: None,
                agent_live_action: None,
                is_live: 0,
                files_changed_json: Some("[]".to_string()),
                created_at: now,
                updated_at: now,
            };
            repo.create_issue(&issue_record)
                .await
                .map_err(to_string_error)?;

            created.push(super::mappings::issue_from_record(issue_record, vec![]));
        }
    }
    Ok(created)
}

pub(super) async fn replace_plan_issues(
    repo: &HqRepository,
    epic_id: &str,
    plan: &HqPlanDto,
) -> Result<Vec<HqIssueDto>, String> {
    let previous_issues = repo
        .list_issues(Some(epic_id))
        .await
        .map_err(to_string_error)?;
    let previous_by_title: HashMap<String, String> = previous_issues
        .into_iter()
        .map(|issue| (issue.title.trim().to_string(), issue.id))
        .collect();

    repo.delete_issues_by_epic(epic_id)
        .await
        .map_err(to_string_error)?;
    let created = create_plan_issues(repo, epic_id, plan).await?;

    for issue in &created {
        if let Some(previous_issue_id) = previous_by_title.get(issue.title.trim()) {
            repo.reassign_comments(previous_issue_id, &issue.id)
                .await
                .map_err(to_string_error)?;
        }
    }

    Ok(created)
}

pub(super) async fn save_plan_for_epic(
    repo: &HqRepository,
    epic_id: &str,
    plan: &HqPlanDto,
) -> Result<(), String> {
    let now = now_ms();
    repo.save_plan(&HqPlanRecord {
        id: plan.id.clone(),
        epic_id: epic_id.to_string(),
        title: plan.title.clone(),
        status: plan.status.clone(),
        director_description: plan.director_description.clone(),
        plan_json: super::data::serialize_json(plan)?,
        created_at: now,
        updated_at: now,
    })
    .await
    .map_err(to_string_error)
}

pub(super) async fn list_epic_dtos(repo: &HqRepository) -> Result<Vec<HqEpicDto>, String> {
    let epics = repo.list_epics().await.map_err(to_string_error)?;
    let issues = repo.list_issues(None).await.map_err(to_string_error)?;
    Ok(epics
        .into_iter()
        .map(|epic| {
            let issue_ids = issues
                .iter()
                .filter(|issue| issue.epic_id == epic.id)
                .map(|issue| issue.id.clone())
                .collect();
            epic_from_record(epic, issue_ids)
        })
        .collect())
}

pub(super) async fn plan_epic_background(
    app: AppHandle,
    repo: HqRepository,
    stack: Arc<AgentStack>,
    epic_id: String,
    title: String,
    description: String,
    team_config: Option<TeamConfigPayload>,
    settings: HqSettingsConfig,
) {
    let goal = if description.trim().is_empty() {
        title.clone()
    } else {
        format!("{title}\n\nAdditional context:\n{description}")
    };

    append_chat_message(
        &repo,
        "director",
        "Planning the epic and decomposing work for the team.".to_string(),
        Some(&epic_id),
        vec![],
    )
    .await;

    append_activity(
        &repo,
        "planning",
        Some("Director"),
        format!("Started planning epic '{title}'"),
    )
    .await;

    let plan_result = async {
        let director = build_director(stack.clone(), team_config.clone(), &settings).await?;
        let planning_context = commander_planning_context(
            &settings,
            (!description.trim().is_empty()).then_some(description.as_str()),
        );
        let raw_plan = match tokio::time::timeout(
            Duration::from_secs(20),
            director.plan(&goal, planning_context.as_deref()),
        )
        .await
        {
            Ok(Ok(plan)) => plan,
            Ok(Err(error)) => {
                warn!(%error, epic_id = %epic_id, "HQ planning provider failed, using fallback plan");
                fallback_hq_plan(&goal)
            }
            Err(_) => {
                warn!(epic_id = %epic_id, "HQ planning timed out, using fallback plan");
                fallback_hq_plan(&goal)
            }
        };
        let board_review = if should_consult_board(&raw_plan) {
            let (tx, rx) = mpsc::unbounded_channel();
            drop(rx);
            director
                .consult_board(&goal, &[], tx)
                .await
                .map_err(to_string_error)?
        } else {
            None
        };
        let plan_id = repo
            .get_epic(&epic_id)
            .await
            .map_err(to_string_error)?
            .and_then(|epic| epic.plan_id)
            .unwrap_or_else(|| Uuid::new_v4().to_string());
        let mut plan_dto =
            convert_plan(&epic_id, &plan_id, "awaiting-approval", &raw_plan, &settings);
        plan_dto.board_review = board_review.as_ref().map(board_review_from_result);
        validate_parallel_phase_dependencies(&plan_dto)?;
        save_plan_for_epic(&repo, &epic_id, &plan_dto).await?;
        let _ = replace_plan_issues(&repo, &epic_id, &plan_dto).await?;

        let Some(mut epic_record) = repo.get_epic(&epic_id).await.map_err(to_string_error)? else {
            return Err("epic disappeared during planning".to_string());
        };
        epic_record.plan_id = Some(plan_id.clone());
        epic_record.progress = 15;
        epic_record.status = "planning".to_string();
        epic_record.updated_at = now_ms();
        repo.update_epic(&epic_record).await.map_err(to_string_error)?;

        for phase in &plan_dto.phases {
            for task in &phase.tasks {
                if let Some(assignee_id) = &task.assignee_id {
                    repo.upsert_agent(&HqAgentRecord {
                        id: assignee_id.clone(),
                        name: task.assignee_name.clone().unwrap_or_else(|| assignee_id.clone()),
                        role: task.domain.clone(),
                        tier: "lead".to_string(),
                        model: settings.director_model.clone(),
                        status: "idle".to_string(),
                        icon: role_icon("lead").to_string(),
                        parent_id: Some("director".to_string()),
                        current_task: Some(task.title.clone()),
                        current_issue_id: None,
                        turn: None,
                        max_turns: None,
                        assigned_issue_ids_json: Some("[]".to_string()),
                        files_touched_json: Some("[]".to_string()),
                        total_cost_usd: 0.0,
                        created_at: now_ms(),
                        updated_at: now_ms(),
                    })
                    .await
                    .map_err(to_string_error)?;
                }
            }
        }

        append_chat_message(
            &repo,
            "director",
            if let Some(review) = &plan_dto.board_review {
                format!(
                    "Plan ready: {} phase(s), {} task(s). Board consensus: {} Review it in the Plan screen before execution.",
                    plan_dto.phases.len(),
                    raw_plan.tasks.len(),
                    review.vote_summary
                )
            } else {
                format!(
                    "Plan ready: {} phase(s), {} task(s). Review it in the Plan screen before execution.",
                    plan_dto.phases.len(),
                    raw_plan.tasks.len()
                )
            },
            Some(&epic_id),
            plan_dto
                .phases
                .iter()
                .flat_map(|phase| phase.tasks.iter())
                .filter_map(|task| {
                    task.assignee_name.clone().map(|agent_name| HqDelegationCardDto {
                        agent_name,
                        task: task.title.clone(),
                        status: "waiting".to_string(),
                    })
                })
                .collect(),
        )
        .await;

        append_activity(
            &repo,
            "review",
            Some("Director"),
            format!("Plan ready for epic '{title}'"),
        )
        .await;

        crate::events::emit_hq_event(&app, &ava_hq::HqEvent::PlanCreated { plan: raw_plan.clone() });

        Ok::<(), String>(())
    }
    .await;

    if let Err(error) = plan_result {
        warn!(%error, epic_id = %epic_id, "HQ planning failed");
        append_chat_message(
            &repo,
            "director",
            format!("Planning failed: {error}"),
            Some(&epic_id),
            vec![],
        )
        .await;
        append_activity(
            &repo,
            "error",
            Some("Director"),
            format!("Planning failed for epic '{title}': {error}"),
        )
        .await;
        let _ = app.emit(
            "agent-event",
            crate::events::AgentEvent::Error {
                message: format!("HQ planning failed: {error}"),
            },
        );
    }

    let bridge = app.state::<crate::bridge::DesktopBridge>();
    *bridge.running.write().await = false;
}
