use tauri::{AppHandle, State};
use uuid::Uuid;

use super::data::{hq_repo, now_ms, to_string_error, HqEpicDetailDto, HqEpicDto, UpdateEpicArgs};
use super::director_runtime::{append_chat_message, ensure_director_agent, load_settings};
use super::issue_commands::list_issue_dtos;
use super::mappings::epic_from_record;
use super::plan_persistence::{list_epic_dtos, plan_epic_background};
use crate::app_state::AppState;
use crate::bridge::DesktopBridge;

#[tauri::command]
pub async fn create_epic(
    title: String,
    description: String,
    app: AppHandle,
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<HqEpicDto, String> {
    let title = title.trim().to_string();
    if title.is_empty() {
        return Err("Epic title must not be empty.".to_string());
    }

    {
        let mut running = bridge.running.write().await;
        if *running {
            return Err(
                "HQ is already running. Wait for the current action to finish or cancel it."
                    .to_string(),
            );
        }
        *running = true;
    }

    let repo = hq_repo(&app_state);
    ensure_director_agent(&repo, &bridge.stack).await?;

    let now = now_ms();
    let epic_record = ava_db::models::HqEpicRecord {
        id: Uuid::new_v4().to_string(),
        title: title.clone(),
        description: description.clone(),
        status: "planning".to_string(),
        progress: 5,
        plan_id: None,
        created_at: now,
        updated_at: now,
    };
    repo.create_epic(&epic_record)
        .await
        .map_err(to_string_error)?;

    append_chat_message(&repo, "user", title.clone(), Some(&epic_record.id), vec![]).await;

    let epic_dto = epic_from_record(epic_record.clone(), vec![]);
    let app_handle = app.clone();
    let repo_bg = repo.clone();
    let stack = bridge.stack.clone();
    let settings = load_settings(&bridge).await;
    tokio::spawn(plan_epic_background(
        app_handle,
        repo_bg,
        stack,
        epic_record.id.clone(),
        title,
        description,
        None,
        settings,
    ));

    Ok(epic_dto)
}

#[tauri::command]
pub async fn list_epics(app_state: State<'_, AppState>) -> Result<Vec<HqEpicDto>, String> {
    list_epic_dtos(&hq_repo(&app_state)).await
}

#[tauri::command]
pub async fn get_epic(
    id: String,
    app_state: State<'_, AppState>,
) -> Result<Option<HqEpicDetailDto>, String> {
    let repo = hq_repo(&app_state);
    let Some(epic) = repo.get_epic(&id).await.map_err(to_string_error)? else {
        return Ok(None);
    };
    let issues = list_issue_dtos(&repo, Some(&id)).await?;
    let issue_ids = issues.iter().map(|issue| issue.id.clone()).collect();
    Ok(Some(HqEpicDetailDto {
        epic: epic_from_record(epic, issue_ids),
        issues,
    }))
}

#[tauri::command]
pub async fn update_epic(
    args: UpdateEpicArgs,
    app_state: State<'_, AppState>,
) -> Result<Option<HqEpicDto>, String> {
    let repo = hq_repo(&app_state);
    let Some(mut epic) = repo.get_epic(&args.id).await.map_err(to_string_error)? else {
        return Ok(None);
    };
    if let Some(title) = args.title {
        epic.title = title;
    }
    if let Some(description) = args.description {
        epic.description = description;
    }
    if let Some(status) = args.status {
        epic.status = status;
    }
    if let Some(progress) = args.progress {
        epic.progress = progress.clamp(0, 100);
    }
    epic.updated_at = now_ms();
    repo.update_epic(&epic).await.map_err(to_string_error)?;
    let issues = repo
        .list_issues(Some(&args.id))
        .await
        .map_err(to_string_error)?;
    Ok(Some(epic_from_record(
        epic,
        issues.into_iter().map(|issue| issue.id).collect(),
    )))
}
