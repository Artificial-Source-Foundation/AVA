use tauri::State;
use uuid::Uuid;

use super::data::{hq_repo, now_ms, to_string_error, HqIssueDto, UpdateIssueArgs};
use super::director_runtime::append_activity;
use super::mappings::{comment_from_record, issue_from_record};
use crate::app_state::AppState;

pub(super) async fn get_issue_dto(
    repo: &ava_db::HqRepository,
    id: &str,
) -> Result<Option<HqIssueDto>, String> {
    let Some(issue) = repo.get_issue(id).await.map_err(to_string_error)? else {
        return Ok(None);
    };
    let comments = repo
        .list_comments(id)
        .await
        .map_err(to_string_error)?
        .into_iter()
        .map(comment_from_record)
        .collect();
    Ok(Some(issue_from_record(issue, comments)))
}

pub(super) async fn list_issue_dtos(
    repo: &ava_db::HqRepository,
    epic_id: Option<&str>,
) -> Result<Vec<HqIssueDto>, String> {
    let issues = repo.list_issues(epic_id).await.map_err(to_string_error)?;
    let mut out = Vec::with_capacity(issues.len());
    for issue in issues {
        let comments = repo
            .list_comments(&issue.id)
            .await
            .map_err(to_string_error)?
            .into_iter()
            .map(comment_from_record)
            .collect();
        out.push(issue_from_record(issue, comments));
    }
    Ok(out)
}

#[tauri::command]
pub async fn create_issue(
    epic_id: String,
    title: String,
    description: String,
    app_state: State<'_, AppState>,
) -> Result<HqIssueDto, String> {
    let repo = hq_repo(&app_state);
    let issue_number = repo.next_issue_number().await.map_err(to_string_error)?;
    let now = now_ms();
    let record = ava_db::models::HqIssueRecord {
        id: Uuid::new_v4().to_string(),
        issue_number,
        identifier: format!("HQ-{issue_number}"),
        title,
        description,
        status: "backlog".to_string(),
        priority: "medium".to_string(),
        assignee_id: None,
        assignee_name: None,
        epic_id,
        phase_label: None,
        agent_turn: None,
        agent_max_turns: None,
        agent_live_action: None,
        is_live: 0,
        files_changed_json: Some("[]".to_string()),
        created_at: now,
        updated_at: now,
    };
    repo.create_issue(&record).await.map_err(to_string_error)?;
    Ok(issue_from_record(record, vec![]))
}

#[tauri::command]
pub async fn list_issues(
    epic_id: Option<String>,
    app_state: State<'_, AppState>,
) -> Result<Vec<HqIssueDto>, String> {
    list_issue_dtos(&hq_repo(&app_state), epic_id.as_deref()).await
}

#[tauri::command]
pub async fn get_issue(
    id: String,
    app_state: State<'_, AppState>,
) -> Result<Option<HqIssueDto>, String> {
    get_issue_dto(&hq_repo(&app_state), &id).await
}

#[tauri::command]
pub async fn update_issue(
    args: UpdateIssueArgs,
    app_state: State<'_, AppState>,
) -> Result<Option<HqIssueDto>, String> {
    let repo = hq_repo(&app_state);
    let Some(mut issue) = repo.get_issue(&args.id).await.map_err(to_string_error)? else {
        return Ok(None);
    };
    if let Some(title) = args.title {
        issue.title = title;
    }
    if let Some(description) = args.description {
        issue.description = description;
    }
    if let Some(status) = args.status {
        issue.status = status;
    }
    if let Some(priority) = args.priority {
        issue.priority = priority;
    }
    if let Some(assignee_id) = args.assignee_id {
        issue.assignee_id = Some(assignee_id);
    }
    if let Some(assignee_name) = args.assignee_name {
        issue.assignee_name = Some(assignee_name);
    }
    if let Some(phase_label) = args.phase_label {
        issue.phase_label = Some(phase_label);
    }
    issue.updated_at = now_ms();
    repo.update_issue(&issue).await.map_err(to_string_error)?;
    get_issue_dto(&repo, &args.id).await
}

#[tauri::command]
pub async fn move_issue(
    id: String,
    status: String,
    app_state: State<'_, AppState>,
) -> Result<Option<HqIssueDto>, String> {
    let repo = hq_repo(&app_state);
    repo.move_issue(&id, &status, now_ms())
        .await
        .map_err(to_string_error)?;
    append_activity(
        &repo,
        "status-change",
        None,
        format!("Moved issue {id} to {status}"),
    )
    .await;
    get_issue_dto(&repo, &id).await
}
