use tauri::State;
use uuid::Uuid;

use super::data::{hq_repo, now_ms, HqIssueDto};
use super::director_runtime::append_activity;
use super::issue_commands::get_issue_dto;
use crate::app_state::AppState;

#[tauri::command]
pub async fn add_comment(
    issue_id: String,
    content: String,
    app_state: State<'_, AppState>,
) -> Result<Option<HqIssueDto>, String> {
    if content.trim().is_empty() {
        return Err("Comment must not be empty.".to_string());
    }
    let repo = hq_repo(&app_state);
    repo.add_comment(&ava_db::models::HqCommentRecord {
        id: Uuid::new_v4().to_string(),
        issue_id: issue_id.clone(),
        author_name: "You".to_string(),
        author_role: "user".to_string(),
        author_icon: None,
        content,
        timestamp: now_ms(),
    })
    .await
    .map_err(super::data::to_string_error)?;
    append_activity(
        &repo,
        "comment",
        Some("You"),
        format!("Commented on {issue_id}"),
    )
    .await;
    get_issue_dto(&repo, &issue_id).await
}
