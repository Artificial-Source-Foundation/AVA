use tauri::State;

use super::data::{
    hq_repo, to_string_error, HqActivityEventDto, HqAgentDto, HqDashboardMetricsDto,
};
use super::director_runtime::ensure_director_agent;
use super::mappings::{activity_from_record, agent_from_record};
use crate::app_state::AppState;
use crate::bridge::DesktopBridge;

#[tauri::command]
pub async fn get_agents(
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<HqAgentDto>, String> {
    let repo = hq_repo(&app_state);
    ensure_director_agent(&repo, &bridge.stack).await?;
    let records = repo.list_agents().await.map_err(to_string_error)?;
    let mut out = Vec::with_capacity(records.len());
    for record in records {
        out.push(agent_from_record(&repo, record).await?);
    }
    Ok(out)
}

#[tauri::command]
pub async fn get_agent(
    id: String,
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<Option<HqAgentDto>, String> {
    let repo = hq_repo(&app_state);
    ensure_director_agent(&repo, &bridge.stack).await?;
    let Some(record) = repo.get_agent(&id).await.map_err(to_string_error)? else {
        return Ok(None);
    };
    agent_from_record(&repo, record).await.map(Some)
}

#[tauri::command]
pub async fn get_dashboard_metrics(
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<HqDashboardMetricsDto, String> {
    let repo = hq_repo(&app_state);
    ensure_director_agent(&repo, &bridge.stack).await?;
    let agents = repo.list_agents().await.map_err(to_string_error)?;
    let epics = repo.list_epics().await.map_err(to_string_error)?;
    let issues = repo.list_issues(None).await.map_err(to_string_error)?;

    let agents_running = agents
        .iter()
        .filter(|agent| agent.status == "running")
        .count();
    let agents_active = agents
        .iter()
        .filter(|agent| matches!(agent.status.as_str(), "active" | "running"))
        .count();
    let agents_idle = agents.iter().filter(|agent| agent.status == "idle").count();
    let issues_open = issues.iter().filter(|issue| issue.status != "done").count();
    let issues_in_progress = issues
        .iter()
        .filter(|issue| issue.status == "in-progress")
        .count();
    let issues_in_review = issues
        .iter()
        .filter(|issue| issue.status == "review")
        .count();
    let issues_done = issues.iter().filter(|issue| issue.status == "done").count();
    let success_rate = if issues.is_empty() {
        100
    } else {
        ((issues_done as f64 / issues.len() as f64) * 100.0).round() as usize
    };

    Ok(HqDashboardMetricsDto {
        agents_active,
        agents_running,
        agents_idle,
        epics_in_progress: epics
            .iter()
            .filter(|epic| epic.status == "in-progress")
            .count(),
        issues_open,
        issues_in_progress,
        issues_in_review,
        issues_done,
        success_rate,
        total_cost_usd: agents.iter().map(|agent| agent.total_cost_usd).sum(),
        payg_agents_tracked: agents
            .iter()
            .filter(|agent| agent.total_cost_usd > 0.0)
            .count(),
    })
}

#[tauri::command]
pub async fn get_activity_feed(
    app_state: State<'_, AppState>,
) -> Result<Vec<HqActivityEventDto>, String> {
    Ok(hq_repo(&app_state)
        .list_activity(100)
        .await
        .map_err(to_string_error)?
        .into_iter()
        .map(activity_from_record)
        .collect())
}
