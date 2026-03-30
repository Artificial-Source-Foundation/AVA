use tauri::{AppHandle, State};

use super::data::{hq_repo, now_ms, to_string_error, HqPlanDto};
use super::director_runtime::{append_chat_message, load_settings};
use super::execution_runtime::start_execution_background;
use super::plan_persistence::plan_epic_background;
use crate::app_state::AppState;
use crate::bridge::DesktopBridge;

pub(super) fn validate_parallel_phase_dependencies(plan: &HqPlanDto) -> Result<(), String> {
    for phase in &plan.phases {
        let task_ids: std::collections::HashSet<&str> =
            phase.tasks.iter().map(|task| task.id.as_str()).collect();
        for task in &phase.tasks {
            if let Some(dependency) = task
                .dependencies
                .iter()
                .find(|dependency| task_ids.contains(dependency.as_str()))
            {
                return Err(format!(
                    "Plan phase '{}' contains an invalid intra-phase dependency: task '{}' depends on '{}'. Tasks inside a single execution phase run in parallel, so this plan needs to be revised.",
                    phase.name, task.title, dependency
                ));
            }
        }
    }
    Ok(())
}

#[tauri::command]
pub async fn get_plan(
    epic_id: String,
    app_state: State<'_, AppState>,
) -> Result<Option<HqPlanDto>, String> {
    let repo = hq_repo(&app_state);
    let Some(record) = repo
        .get_plan_by_epic(&epic_id)
        .await
        .map_err(to_string_error)?
    else {
        return Ok(None);
    };
    serde_json::from_str(&record.plan_json)
        .map(Some)
        .map_err(to_string_error)
}

#[tauri::command]
pub async fn approve_plan(
    plan_id: String,
    app: AppHandle,
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<Option<HqPlanDto>, String> {
    {
        let mut running = bridge.running.write().await;
        if *running {
            return Err(
                "HQ is already running. Cancel the current action before approving another plan."
                    .to_string(),
            );
        }
        *running = true;
    }

    let repo = hq_repo(&app_state);
    let Some(plan_record) = repo.get_plan(&plan_id).await.map_err(to_string_error)? else {
        *bridge.running.write().await = false;
        return Ok(None);
    };

    repo.update_plan_status(&plan_id, "executing", now_ms())
        .await
        .map_err(to_string_error)?;
    if let Some(mut epic) = repo
        .get_epic(&plan_record.epic_id)
        .await
        .map_err(to_string_error)?
    {
        epic.status = "in-progress".to_string();
        epic.progress = 25;
        epic.updated_at = now_ms();
        repo.update_epic(&epic).await.map_err(to_string_error)?;
    }

    let parsed: HqPlanDto =
        serde_json::from_str(&plan_record.plan_json).map_err(to_string_error)?;
    let repo_bg = repo.clone();
    let stack = bridge.stack.clone();
    let settings = load_settings(&bridge).await;
    tokio::spawn(start_execution_background(
        app,
        repo_bg,
        stack,
        plan_record.epic_id.clone(),
        ava_db::models::HqPlanRecord {
            status: "executing".to_string(),
            ..plan_record
        },
        settings,
    ));

    Ok(Some(HqPlanDto {
        status: "executing".to_string(),
        ..parsed
    }))
}

#[tauri::command]
pub async fn reject_plan(
    plan_id: String,
    feedback: String,
    app: AppHandle,
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<Option<HqPlanDto>, String> {
    let repo = hq_repo(&app_state);
    let Some(plan_record) = repo.get_plan(&plan_id).await.map_err(to_string_error)? else {
        return Ok(None);
    };

    {
        let mut running = bridge.running.write().await;
        if *running {
            return Err("HQ is already running. Wait for the current action to finish or cancel it before revising the plan.".to_string());
        }
        *running = true;
    }

    repo.update_plan_status(&plan_id, "rejected", now_ms())
        .await
        .map_err(to_string_error)?;
    append_chat_message(
        &repo,
        "user",
        feedback.clone(),
        Some(&plan_record.epic_id),
        vec![],
    )
    .await;
    append_chat_message(
        &repo,
        "director",
        "Acknowledged. Revising the plan with your feedback.".to_string(),
        Some(&plan_record.epic_id),
        vec![],
    )
    .await;

    let epic = repo
        .get_epic(&plan_record.epic_id)
        .await
        .map_err(to_string_error)?
        .ok_or_else(|| "Epic for plan was not found.".to_string())?;
    let settings = load_settings(&bridge).await;
    let stack = bridge.stack.clone();
    tokio::spawn(plan_epic_background(
        app,
        repo.clone(),
        stack,
        plan_record.epic_id.clone(),
        epic.title.clone(),
        if feedback.trim().is_empty() {
            epic.description.clone()
        } else {
            format!("{}\n\nRevision feedback:\n{}", epic.description, feedback)
        },
        None,
        settings,
    ));

    let mut parsed: HqPlanDto =
        serde_json::from_str(&plan_record.plan_json).map_err(to_string_error)?;
    parsed.status = "rejected".to_string();
    Ok(Some(parsed))
}
