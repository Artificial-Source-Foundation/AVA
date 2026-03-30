use ava_db::HqRepository;
use serde::{Deserialize, Serialize};

use crate::app_state::AppState;

#[allow(dead_code)]
#[derive(Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct LeadConfigPayload {
    pub domain: String,
    pub enabled: bool,
    #[serde(default)]
    pub model: String,
    #[serde(default = "default_max_workers")]
    pub max_workers: usize,
    #[serde(default)]
    pub custom_prompt: String,
}

fn default_max_workers() -> usize {
    3
}

#[allow(dead_code)]
#[derive(Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct TeamConfigPayload {
    #[serde(default)]
    pub default_director_model: String,
    #[serde(default)]
    pub default_lead_model: String,
    #[serde(default)]
    pub default_worker_model: String,
    #[serde(default)]
    pub default_scout_model: String,
    #[serde(default)]
    pub worker_names: Vec<String>,
    #[serde(default)]
    pub leads: Vec<LeadConfigPayload>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StartHqArgs {
    pub goal: String,
    #[serde(default)]
    #[allow(dead_code)]
    pub domain: Option<String>,
    #[serde(default)]
    pub team_config: Option<TeamConfigPayload>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HqStatus {
    pub running: bool,
    pub total_workers: usize,
    pub succeeded: usize,
    pub failed: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqFileChangeDto {
    pub path: String,
    pub additions: i64,
    pub deletions: i64,
    pub is_new: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqCommentDto {
    pub id: String,
    pub author_name: String,
    pub author_role: String,
    pub author_icon: Option<String>,
    pub content: String,
    pub timestamp: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqIssueDto {
    pub id: String,
    pub identifier: String,
    pub title: String,
    pub description: String,
    pub status: String,
    pub priority: String,
    pub assignee_id: Option<String>,
    pub assignee_name: Option<String>,
    pub epic_id: String,
    pub phase_label: Option<String>,
    pub comments: Vec<HqCommentDto>,
    pub files_changed: Vec<HqFileChangeDto>,
    pub agent_progress: Option<HqAgentProgressDto>,
    pub agent_live_action: Option<String>,
    pub is_live: bool,
    pub created_at: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqEpicDto {
    pub id: String,
    pub title: String,
    pub description: String,
    pub status: String,
    pub progress: i64,
    pub issue_ids: Vec<String>,
    pub plan_id: Option<String>,
    pub created_at: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqEpicDetailDto {
    #[serde(flatten)]
    pub epic: HqEpicDto,
    pub issues: Vec<HqIssueDto>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqAgentProgressDto {
    pub turn: i64,
    pub max_turns: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqPlanTaskDto {
    pub id: String,
    pub title: String,
    pub domain: String,
    pub complexity: String,
    pub dependencies: Vec<String>,
    pub assignee_id: Option<String>,
    pub assignee_name: Option<String>,
    pub assignee_model: Option<String>,
    pub steps: Vec<String>,
    pub file_hints: Vec<String>,
    pub budget_max_tokens: usize,
    pub budget_max_turns: usize,
    pub budget_max_cost_usd: f64,
    pub expanded: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqPhaseDto {
    pub id: String,
    pub number: i64,
    pub name: String,
    pub description: String,
    pub execution: String,
    pub depends_on: Vec<String>,
    pub tasks: Vec<HqPlanTaskDto>,
    pub review_enabled: bool,
    pub review_assignee: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqBoardOpinionDto {
    pub member_name: String,
    pub personality: String,
    pub recommendation: String,
    pub vote: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqBoardReviewDto {
    pub consensus: String,
    pub vote_summary: String,
    pub opinions: Vec<HqBoardOpinionDto>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqPlanDto {
    pub id: String,
    pub epic_id: String,
    pub title: String,
    pub status: String,
    pub director_description: String,
    pub board_review: Option<HqBoardReviewDto>,
    pub phases: Vec<HqPhaseDto>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqTranscriptEntryDto {
    pub id: String,
    #[serde(rename = "type")]
    pub kind: String,
    pub tool_name: Option<String>,
    pub tool_path: Option<String>,
    pub tool_status: Option<String>,
    pub content: String,
    pub timestamp: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqAgentDto {
    pub id: String,
    pub name: String,
    pub role: String,
    pub tier: String,
    pub model: String,
    pub status: String,
    pub icon: String,
    pub parent_id: Option<String>,
    pub current_task: Option<String>,
    pub current_issue_id: Option<String>,
    pub turn: Option<i64>,
    pub max_turns: Option<i64>,
    pub transcript: Vec<HqTranscriptEntryDto>,
    pub assigned_issue_ids: Vec<String>,
    pub files_touched: Vec<String>,
    pub total_cost_usd: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqActivityEventDto {
    pub id: String,
    #[serde(rename = "type")]
    pub kind: String,
    pub agent_name: Option<String>,
    pub message: String,
    pub color: String,
    pub timestamp: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqDashboardMetricsDto {
    pub agents_active: usize,
    pub agents_running: usize,
    pub agents_idle: usize,
    pub epics_in_progress: usize,
    pub issues_open: usize,
    pub issues_in_progress: usize,
    pub issues_in_review: usize,
    pub issues_done: usize,
    pub success_rate: usize,
    pub total_cost_usd: f64,
    pub payg_agents_tracked: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqDelegationCardDto {
    pub agent_name: String,
    pub task: String,
    pub status: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqDirectorMessageDto {
    pub id: String,
    pub role: String,
    pub content: String,
    pub delegations: Vec<HqDelegationCardDto>,
    pub timestamp: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqSettingsDto {
    pub director_model: String,
    pub tone_preference: String,
    pub auto_review: bool,
    pub show_costs: bool,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UpdateHqSettingsArgs {
    #[serde(default)]
    pub director_model: Option<String>,
    #[serde(default)]
    pub tone_preference: Option<String>,
    #[serde(default)]
    pub auto_review: Option<bool>,
    #[serde(default)]
    pub show_costs: Option<bool>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BootstrapHqWorkspaceArgs {
    #[serde(default)]
    pub director_model: Option<String>,
    #[serde(default)]
    pub force: bool,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HqWorkspaceBootstrapDto {
    pub project_root: String,
    pub hq_root: String,
    pub project_name: String,
    pub stack_summary: Vec<String>,
    pub created_files: Vec<String>,
    pub reused_existing: bool,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UpdateEpicArgs {
    pub id: String,
    #[serde(default)]
    pub title: Option<String>,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub status: Option<String>,
    #[serde(default)]
    pub progress: Option<i64>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UpdateIssueArgs {
    pub id: String,
    #[serde(default)]
    pub title: Option<String>,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub status: Option<String>,
    #[serde(default)]
    pub priority: Option<String>,
    #[serde(default)]
    pub assignee_id: Option<String>,
    #[serde(default)]
    pub assignee_name: Option<String>,
    #[serde(default)]
    pub phase_label: Option<String>,
}

pub(super) fn now_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

pub(super) fn to_string_error<E: std::fmt::Display>(error: E) -> String {
    error.to_string()
}

pub(super) fn hq_repo(app: &AppState) -> HqRepository {
    HqRepository::new(app.database().pool().clone())
}

pub(super) fn parse_json_vec<T: serde::de::DeserializeOwned>(raw: Option<&str>) -> Vec<T> {
    raw.and_then(|value| serde_json::from_str(value).ok())
        .unwrap_or_default()
}

pub(super) fn serialize_json<T: serde::Serialize>(value: &T) -> Result<String, String> {
    serde_json::to_string(value).map_err(to_string_error)
}
