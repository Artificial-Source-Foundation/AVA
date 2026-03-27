//! Tauri commands for HQ multi-agent orchestration and persistence.

use std::collections::HashMap;
use std::sync::Arc;

use ava_agent::stack::AgentStack;
use ava_config::{HqAgentOverride, HqConfig as HqSettingsConfig};
use ava_db::models::{
    HqActivityRecord, HqAgentRecord, HqAgentTranscriptRecord, HqChatMessageRecord, HqCommentRecord,
    HqEpicRecord, HqIssueRecord, HqPlanRecord,
};
use ava_db::HqRepository;
use ava_hq::{Budget, Director, DirectorConfig, Domain, HqEvent, HqPlan, TaskComplexity};
use ava_platform::StandardPlatform;
use ava_types::MessageTier;
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager, State};
use tokio::sync::mpsc;
use tracing::{info, warn};
use uuid::Uuid;

use super::helpers::{parse_domain, resolve_model_spec};
use crate::app_state::AppState;
use crate::bridge::DesktopBridge;

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
    pub assignee_id: Option<String>,
    pub assignee_name: Option<String>,
    pub assignee_model: Option<String>,
    pub steps: Vec<String>,
    pub file_hints: Vec<String>,
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
pub struct HqPlanDto {
    pub id: String,
    pub epic_id: String,
    pub title: String,
    pub status: String,
    pub director_description: String,
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

fn now_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

fn to_string_error<E: std::fmt::Display>(error: E) -> String {
    error.to_string()
}

fn new_issue_number() -> i64 {
    let bytes = *Uuid::new_v4().as_bytes();
    let mut lower = [0_u8; 8];
    lower.copy_from_slice(&bytes[8..]);
    i64::from_be_bytes(lower).abs()
}

fn hq_repo(app: &AppState) -> HqRepository {
    HqRepository::new(app.database().pool().clone())
}

fn parse_json_vec<T: serde::de::DeserializeOwned>(raw: Option<&str>) -> Vec<T> {
    raw.and_then(|value| serde_json::from_str(value).ok())
        .unwrap_or_default()
}

fn serialize_json<T: serde::Serialize>(value: &T) -> Result<String, String> {
    serde_json::to_string(value).map_err(to_string_error)
}

fn director_settings_to_dto(settings: &HqSettingsConfig) -> HqSettingsDto {
    HqSettingsDto {
        director_model: settings.director_model.clone(),
        tone_preference: settings.tone_preference.clone(),
        auto_review: settings.auto_review,
        show_costs: settings.show_costs,
    }
}

fn phase_execution_label(task_count: usize) -> &'static str {
    if task_count > 1 {
        "parallel"
    } else {
        "sequential"
    }
}

fn complexity_label(complexity: &TaskComplexity) -> &'static str {
    match complexity {
        TaskComplexity::Simple => "simple",
        TaskComplexity::Medium => "medium",
        TaskComplexity::Complex => "complex",
    }
}

fn status_color(status: &str) -> &'static str {
    match status {
        "done" | "completed" => "var(--success)",
        "review" | "planning" => "#eab308",
        "in-progress" | "running" | "active" => "#06b6d4",
        _ => "var(--text-muted)",
    }
}

fn role_icon(tier: &str) -> &'static str {
    match tier {
        "director" => "crown",
        "lead" => "code",
        "scout" => "search",
        _ => "user",
    }
}

fn issue_from_record(record: HqIssueRecord, comments: Vec<HqCommentDto>) -> HqIssueDto {
    HqIssueDto {
        id: record.id,
        identifier: record.identifier,
        title: record.title,
        description: record.description,
        status: record.status,
        priority: record.priority,
        assignee_id: record.assignee_id,
        assignee_name: record.assignee_name,
        epic_id: record.epic_id,
        phase_label: record.phase_label,
        comments,
        files_changed: parse_json_vec(record.files_changed_json.as_deref()),
        agent_progress: match (record.agent_turn, record.agent_max_turns) {
            (Some(turn), Some(max_turns)) => Some(HqAgentProgressDto { turn, max_turns }),
            _ => None,
        },
        agent_live_action: record.agent_live_action,
        is_live: record.is_live != 0,
        created_at: record.created_at,
    }
}

fn comment_from_record(record: HqCommentRecord) -> HqCommentDto {
    HqCommentDto {
        id: record.id,
        author_name: record.author_name,
        author_role: record.author_role,
        author_icon: record.author_icon,
        content: record.content,
        timestamp: record.timestamp,
    }
}

fn epic_from_record(record: HqEpicRecord, issue_ids: Vec<String>) -> HqEpicDto {
    HqEpicDto {
        id: record.id,
        title: record.title,
        description: record.description,
        status: record.status,
        progress: record.progress,
        issue_ids,
        plan_id: record.plan_id,
        created_at: record.created_at,
    }
}

fn transcript_from_record(record: HqAgentTranscriptRecord) -> HqTranscriptEntryDto {
    HqTranscriptEntryDto {
        id: record.id,
        kind: record.entry_type,
        tool_name: record.tool_name,
        tool_path: record.tool_path,
        tool_status: record.tool_status,
        content: record.content,
        timestamp: record.timestamp,
    }
}

fn activity_from_record(record: HqActivityRecord) -> HqActivityEventDto {
    HqActivityEventDto {
        id: record.id,
        kind: record.event_type,
        agent_name: record.agent_name,
        message: record.message,
        color: record.color,
        timestamp: record.timestamp,
    }
}

fn chat_from_record(record: HqChatMessageRecord) -> HqDirectorMessageDto {
    HqDirectorMessageDto {
        id: record.id,
        role: record.role,
        content: record.content,
        delegations: parse_json_vec(record.delegations_json.as_deref()),
        timestamp: record.timestamp,
    }
}

async fn agent_from_record(
    repo: &HqRepository,
    record: HqAgentRecord,
) -> Result<HqAgentDto, String> {
    let transcript = repo
        .list_agent_transcript(&record.id)
        .await
        .map_err(to_string_error)?
        .into_iter()
        .map(transcript_from_record)
        .collect();

    Ok(HqAgentDto {
        id: record.id,
        name: record.name,
        role: record.role,
        tier: record.tier,
        model: record.model,
        status: record.status,
        icon: record.icon,
        parent_id: record.parent_id,
        current_task: record.current_task,
        current_issue_id: record.current_issue_id,
        turn: record.turn,
        max_turns: record.max_turns,
        transcript,
        assigned_issue_ids: parse_json_vec(record.assigned_issue_ids_json.as_deref()),
        files_touched: parse_json_vec(record.files_touched_json.as_deref()),
        total_cost_usd: record.total_cost_usd,
    })
}

fn domain_assignee(domain: &Domain) -> (&'static str, &'static str, &'static str) {
    match domain {
        Domain::Frontend => ("frontend-lead", "Frontend Lead", "lead"),
        Domain::Backend => ("backend-lead", "Backend Lead", "lead"),
        Domain::QA => ("qa-lead", "QA Lead", "lead"),
        Domain::Research => ("research-lead", "Research Lead", "lead"),
        Domain::Debug => ("debug-lead", "Debug Lead", "lead"),
        Domain::DevOps => ("devops-lead", "DevOps Lead", "lead"),
        Domain::Fullstack => ("fullstack-lead", "Fullstack Lead", "lead"),
    }
}

fn build_director_description(goal: &str, plan: &HqPlan, settings: &HqSettingsConfig) -> String {
    let style = if settings.tone_preference == "simple" {
        "I split this into a small set of phases so the team can work in order without stepping on each other."
    } else {
        "I decomposed this goal into dependency-aware execution phases so leads can parallelize independent work and preserve a clean review path."
    };

    format!(
        "{style} Goal: {goal}. Planned {} task(s) across {} phase(s).",
        plan.tasks.len(),
        plan.execution_groups.len()
    )
}

fn hq_override<'a>(settings: &'a HqSettingsConfig, id: &str) -> Option<&'a HqAgentOverride> {
    settings
        .agent_overrides
        .iter()
        .find(|override_item| override_item.id == id)
}

fn commander_planning_context(
    settings: &HqSettingsConfig,
    context: Option<&str>,
) -> Option<String> {
    let commander_prompt = hq_override(settings, "commander")
        .filter(|override_item| override_item.enabled)
        .map(|override_item| override_item.system_prompt.trim())
        .filter(|prompt| !prompt.is_empty());

    match (
        commander_prompt,
        context.map(str::trim).filter(|value| !value.is_empty()),
    ) {
        (Some(prompt), Some(existing)) => {
            Some(format!("{existing}\n\n## Commander Instructions\n{prompt}"))
        }
        (Some(prompt), None) => Some(format!("## Commander Instructions\n{prompt}")),
        (None, Some(existing)) => Some(existing.to_string()),
        (None, None) => None,
    }
}

async fn provider_from_override(
    stack: &AgentStack,
    override_item: Option<&HqAgentOverride>,
) -> Option<Arc<dyn ava_llm::provider::LLMProvider>> {
    let model_spec = override_item
        .map(|item| item.model_spec.trim())
        .unwrap_or("");
    resolve_model_spec(stack, model_spec).await
}

fn convert_plan(
    epic_id: &str,
    plan_id: &str,
    status: &str,
    plan: &HqPlan,
    settings: &HqSettingsConfig,
) -> HqPlanDto {
    let mut task_lookup = HashMap::new();
    for task in &plan.tasks {
        task_lookup.insert(task.id.clone(), task.clone());
    }

    let phases = plan
        .execution_groups
        .iter()
        .enumerate()
        .map(|(index, group)| {
            let tasks: Vec<HqPlanTaskDto> = group
                .task_ids
                .iter()
                .filter_map(|task_id| task_lookup.get(task_id))
                .map(|task| {
                    let (assignee_id, assignee_name, _) = domain_assignee(&task.domain);
                    HqPlanTaskDto {
                        id: task.id.clone(),
                        title: task.description.clone(),
                        domain: format!("{:?}", task.domain).to_ascii_lowercase(),
                        complexity: complexity_label(&task.complexity).to_string(),
                        assignee_id: Some(assignee_id.to_string()),
                        assignee_name: Some(assignee_name.to_string()),
                        assignee_model: None,
                        steps: vec![task.description.clone()],
                        file_hints: task.files_hint.clone(),
                        expanded: matches!(task.complexity, TaskComplexity::Complex),
                    }
                })
                .collect();

            HqPhaseDto {
                id: format!("phase-{}", index + 1),
                number: (index + 1) as i64,
                name: group.label.clone(),
                description: format!("Execute {} planned task(s).", tasks.len()),
                execution: phase_execution_label(tasks.len()).to_string(),
                depends_on: if index == 0 {
                    vec![]
                } else {
                    vec![format!("phase-{index}")]
                },
                tasks,
                review_enabled: settings.auto_review,
                review_assignee: settings.auto_review.then(|| "QA Lead".to_string()),
            }
        })
        .collect();

    HqPlanDto {
        id: plan_id.to_string(),
        epic_id: epic_id.to_string(),
        title: plan.goal.clone(),
        status: status.to_string(),
        director_description: build_director_description(&plan.goal, plan, settings),
        phases,
    }
}

async fn ensure_director_agent(repo: &HqRepository, stack: &AgentStack) -> Result<(), String> {
    let now = now_ms();
    let (_, model_name) = stack.current_model().await;
    repo.upsert_agent(&HqAgentRecord {
        id: "director".to_string(),
        name: "Director".to_string(),
        role: "Director of Engineering".to_string(),
        tier: "director".to_string(),
        model: if model_name.is_empty() {
            "auto".to_string()
        } else {
            model_name
        },
        status: "active".to_string(),
        icon: "crown".to_string(),
        parent_id: None,
        current_task: Some("Supervising HQ".to_string()),
        current_issue_id: None,
        turn: None,
        max_turns: None,
        assigned_issue_ids_json: Some("[]".to_string()),
        files_touched_json: Some("[]".to_string()),
        total_cost_usd: 0.0,
        created_at: now,
        updated_at: now,
    })
    .await
    .map_err(to_string_error)
}

async fn append_activity(
    repo: &HqRepository,
    event_type: &str,
    agent_name: Option<&str>,
    message: String,
) {
    let _ = repo
        .create_activity(&HqActivityRecord {
            id: Uuid::new_v4().to_string(),
            event_type: event_type.to_string(),
            agent_name: agent_name.map(ToOwned::to_owned),
            color: status_color(event_type).to_string(),
            message,
            timestamp: now_ms(),
        })
        .await;
}

async fn append_chat_message(
    repo: &HqRepository,
    role: &str,
    content: String,
    epic_id: Option<&str>,
    delegations: Vec<HqDelegationCardDto>,
) {
    let delegations_json = if delegations.is_empty() {
        None
    } else {
        serialize_json(&delegations).ok()
    };

    let _ = repo
        .add_chat_message(&HqChatMessageRecord {
            id: Uuid::new_v4().to_string(),
            role: role.to_string(),
            content,
            delegations_json,
            epic_id: epic_id.map(ToOwned::to_owned),
            timestamp: now_ms(),
        })
        .await;
}

async fn load_settings(bridge: &DesktopBridge) -> HqSettingsConfig {
    bridge.stack.config.get().await.hq
}

async fn build_director(
    stack: Arc<AgentStack>,
    team_config: Option<TeamConfigPayload>,
    settings: &HqSettingsConfig,
) -> Result<Director, String> {
    let (provider_name, model_name) = stack.current_model().await;
    let default_provider = stack
        .router
        .route_required(&provider_name, &model_name)
        .await
        .map_err(to_string_error)?;

    let mut domain_providers = HashMap::new();
    let mut enabled_leads = Vec::new();
    let mut lead_prompts = HashMap::new();
    let mut worker_names = Vec::new();

    let commander_override =
        hq_override(settings, "commander").filter(|override_item| override_item.enabled);
    let coder_override =
        hq_override(settings, "coder").filter(|override_item| override_item.enabled);
    let researcher_override = hq_override(settings, "researcher")
        .or_else(|| hq_override(settings, "explorer"))
        .filter(|override_item| override_item.enabled);

    if let Some(ref team) = team_config {
        worker_names = team.worker_names.clone();
        for lead_cfg in &team.leads {
            if !lead_cfg.enabled {
                continue;
            }
            if let Some(domain) = parse_domain(&lead_cfg.domain) {
                let override_id = format!("{}-lead", lead_cfg.domain.to_lowercase());
                let lead_override = hq_override(settings, &override_id);
                if matches!(lead_override, Some(override_item) if !override_item.enabled) {
                    continue;
                }
                enabled_leads.push(domain.clone());
                let model_spec = if let Some(override_item) = lead_override {
                    override_item.model_spec.as_str()
                } else if lead_cfg.model.is_empty() {
                    &team.default_lead_model
                } else {
                    &lead_cfg.model
                };
                if let Some(provider) = resolve_model_spec(&stack, model_spec).await {
                    domain_providers.insert(domain.clone(), provider);
                }
                if let Some(override_item) = lead_override {
                    if !override_item.system_prompt.trim().is_empty() {
                        lead_prompts.insert(domain.clone(), override_item.system_prompt.clone());
                        continue;
                    }
                }
                if !lead_cfg.custom_prompt.is_empty() {
                    lead_prompts.insert(domain, lead_cfg.custom_prompt.clone());
                }
            }
        }
    } else {
        for (override_id, domain) in [
            ("frontend-lead", Domain::Frontend),
            ("backend-lead", Domain::Backend),
            ("qa-lead", Domain::QA),
            ("research-lead", Domain::Research),
            ("debug-lead", Domain::Debug),
            ("fullstack-lead", Domain::Fullstack),
            ("devops-lead", Domain::DevOps),
        ] {
            let lead_override = hq_override(settings, override_id);
            if matches!(lead_override, Some(override_item) if !override_item.enabled) {
                continue;
            }
            enabled_leads.push(domain.clone());
            if let Some(provider) = provider_from_override(&stack, lead_override).await {
                domain_providers.insert(domain.clone(), provider);
            }
            if let Some(override_item) = lead_override {
                if !override_item.system_prompt.trim().is_empty() {
                    lead_prompts.insert(domain, override_item.system_prompt.clone());
                }
            }
        }
    }

    let director_provider =
        if let Some(provider) = provider_from_override(&stack, commander_override).await {
            provider
        } else if !settings.director_model.is_empty() {
            resolve_model_spec(&stack, &settings.director_model)
                .await
                .unwrap_or_else(|| default_provider.clone())
        } else if let Some(ref team) = team_config {
            resolve_model_spec(&stack, &team.default_director_model)
                .await
                .unwrap_or_else(|| default_provider.clone())
        } else {
            default_provider.clone()
        };

    let scout_provider =
        if let Some(provider) = provider_from_override(&stack, researcher_override).await {
            Some(provider)
        } else if let Some(ref team) = team_config {
            resolve_model_spec(&stack, &team.default_scout_model).await
        } else {
            None
        };

    let worker_provider =
        if let Some(provider) = provider_from_override(&stack, coder_override).await {
            Some(provider)
        } else if let Some(ref team) = team_config {
            resolve_model_spec(&stack, &team.default_worker_model).await
        } else {
            None
        };
    Ok(Director::new(DirectorConfig {
        budget: Budget::interactive(200, 10.0),
        default_provider: director_provider,
        domain_providers,
        platform: Some(Arc::new(StandardPlatform)),
        scout_provider,
        board_providers: vec![],
        worker_names,
        enabled_leads,
        lead_prompts,
        worker_provider,
    }))
}

async fn create_plan_issues(
    repo: &HqRepository,
    epic_id: &str,
    plan: &HqPlanDto,
) -> Result<Vec<HqIssueDto>, String> {
    let mut created = Vec::new();
    for phase in &plan.phases {
        for task in &phase.tasks {
            let issue_number = new_issue_number();
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

            created.push(issue_from_record(issue_record, vec![]));
        }
    }
    Ok(created)
}

async fn save_plan_for_epic(
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
        plan_json: serialize_json(plan)?,
        created_at: now,
        updated_at: now,
    })
    .await
    .map_err(to_string_error)
}

async fn get_issue_dto(repo: &HqRepository, id: &str) -> Result<Option<HqIssueDto>, String> {
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

async fn list_issue_dtos(
    repo: &HqRepository,
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

async fn list_epic_dtos(repo: &HqRepository) -> Result<Vec<HqEpicDto>, String> {
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

async fn plan_epic_background(
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
        let planning_context = commander_planning_context(&settings, (!description.trim().is_empty()).then_some(description.as_str()));
        let raw_plan = director
            .plan(&goal, planning_context.as_deref())
            .await
            .map_err(to_string_error)?;
        let plan_id = Uuid::new_v4().to_string();
        let plan_dto = convert_plan(&epic_id, &plan_id, "awaiting-approval", &raw_plan, &settings);
        save_plan_for_epic(&repo, &epic_id, &plan_dto).await?;
        let _ = create_plan_issues(&repo, &epic_id, &plan_dto).await?;

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
            format!(
                "Plan ready: {} phase(s), {} task(s). Review it in the Plan screen before execution.",
                plan_dto.phases.len(),
                raw_plan.tasks.len()
            ),
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

        crate::events::emit_hq_event(
            &app,
            &HqEvent::PlanCreated {
                plan: raw_plan.clone(),
            },
        );

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

    let bridge = app.state::<DesktopBridge>();
    *bridge.running.write().await = false;
}

async fn persist_runtime_event(repo: &HqRepository, epic_id: &str, event: &HqEvent) {
    match event {
        HqEvent::WorkerStarted {
            worker_id,
            lead,
            task_description,
        } => {
            let issues = repo.list_issues(Some(epic_id)).await.unwrap_or_default();
            let matched_issue = issues
                .into_iter()
                .find(|issue| issue.title == *task_description);
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
            let issues = repo.list_issues(Some(epic_id)).await.unwrap_or_default();
            let matched_issue = issues
                .into_iter()
                .find(|issue| issue.title == *task_description);
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

async fn start_execution_background(
    app: AppHandle,
    repo: HqRepository,
    stack: Arc<AgentStack>,
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
            let bridge = app.state::<DesktopBridge>();
            *bridge.running.write().await = false;
            return;
        }
    };

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
                dependencies: vec![],
                budget: Budget::interactive(40, 2.0),
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
            let bridge = app.state::<DesktopBridge>();
            *bridge.running.write().await = false;
            return;
        }
    };

    let cancel = {
        let bridge = app.state::<DesktopBridge>();
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

    let bridge = app.state::<DesktopBridge>();
    *bridge.running.write().await = false;
}

#[tauri::command]
pub async fn start_hq(
    args: StartHqArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    {
        let mut running = bridge.running.write().await;
        if *running {
            return Err("Agent is already running. Cancel first.".to_string());
        }
        *running = true;
    }

    let cancel = bridge.new_cancel_token().await;
    let stack = bridge.stack.clone();
    let app_handle = app.clone();

    tokio::spawn(async move {
        let settings = stack.config.get().await.hq;
        let director =
            match build_director(stack.clone(), args.team_config.clone(), &settings).await {
                Ok(director) => director,
                Err(error) => {
                    let _ = app_handle.emit(
                        "agent-event",
                        crate::events::AgentEvent::Error {
                            message: format!("HQ setup failed: {error}"),
                        },
                    );
                    let bridge_ref = app_handle.state::<DesktopBridge>();
                    *bridge_ref.running.write().await = false;
                    return;
                }
            };

        let mut director = director;
        let worker = match director.delegate(ava_hq::Task {
            description: args.goal.clone(),
            task_type: ava_hq::TaskType::Simple,
            files: vec![],
        }) {
            Ok(worker) => worker,
            Err(err) => {
                let _ = app_handle.emit(
                    "agent-event",
                    crate::events::AgentEvent::Error {
                        message: format!("HQ delegation failed: {err}"),
                    },
                );
                let bridge_ref = app_handle.state::<DesktopBridge>();
                *bridge_ref.running.write().await = false;
                return;
            }
        };

        let (tx, mut rx) = mpsc::unbounded_channel();
        let app_fwd = app_handle.clone();
        let forwarder = tokio::spawn(async move {
            while let Some(event) = rx.recv().await {
                crate::events::emit_hq_event(&app_fwd, &event);
            }
        });

        let result = director.coordinate(vec![worker], cancel, tx).await;
        let _ = forwarder.await;

        if let Err(error) = result {
            let _ = app_handle.emit(
                "agent-event",
                crate::events::AgentEvent::Error {
                    message: format!("HQ coordination failed: {error}"),
                },
            );
        }

        let bridge_ref = app_handle.state::<DesktopBridge>();
        *bridge_ref.running.write().await = false;
    });

    Ok(())
}

#[tauri::command]
pub async fn get_hq_status(bridge: State<'_, DesktopBridge>) -> Result<HqStatus, String> {
    let running = *bridge.running.read().await;
    Ok(HqStatus {
        running,
        total_workers: 0,
        succeeded: 0,
        failed: 0,
    })
}

#[tauri::command]
pub async fn cancel_hq(bridge: State<'_, DesktopBridge>) -> Result<(), String> {
    bridge.cancel().await;
    Ok(())
}

#[tauri::command]
pub async fn steer_lead(
    lead_id: String,
    message: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    if message.is_empty() {
        return Err("Steering message must not be empty.".to_string());
    }
    info!(lead_id = %lead_id, message = %message, "steer_lead: forwarding as steering message");
    bridge.send_message(message, MessageTier::Steering).await
}

#[tauri::command]
pub async fn get_hq_settings(bridge: State<'_, DesktopBridge>) -> Result<HqSettingsDto, String> {
    Ok(director_settings_to_dto(&load_settings(&bridge).await))
}

#[tauri::command]
pub async fn update_hq_settings(
    args: UpdateHqSettingsArgs,
    bridge: State<'_, DesktopBridge>,
) -> Result<HqSettingsDto, String> {
    bridge
        .stack
        .config
        .update(|config| {
            if let Some(director_model) = &args.director_model {
                config.hq.director_model = director_model.clone();
            }
            if let Some(tone_preference) = &args.tone_preference {
                config.hq.tone_preference = tone_preference.clone();
            }
            if let Some(auto_review) = args.auto_review {
                config.hq.auto_review = auto_review;
            }
            if let Some(show_costs) = args.show_costs {
                config.hq.show_costs = show_costs;
            }
        })
        .await
        .map_err(to_string_error)?;
    bridge.stack.config.save().await.map_err(to_string_error)?;
    Ok(director_settings_to_dto(&load_settings(&bridge).await))
}

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
    let epic_record = HqEpicRecord {
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

#[tauri::command]
pub async fn create_issue(
    epic_id: String,
    title: String,
    description: String,
    app_state: State<'_, AppState>,
) -> Result<HqIssueDto, String> {
    let repo = hq_repo(&app_state);
    let issue_number = new_issue_number();
    let now = now_ms();
    let record = HqIssueRecord {
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
    repo.add_comment(&HqCommentRecord {
        id: Uuid::new_v4().to_string(),
        issue_id: issue_id.clone(),
        author_name: "You".to_string(),
        author_role: "user".to_string(),
        author_icon: None,
        content,
        timestamp: now_ms(),
    })
    .await
    .map_err(to_string_error)?;
    append_activity(
        &repo,
        "comment",
        Some("You"),
        format!("Commented on {issue_id}"),
    )
    .await;
    get_issue_dto(&repo, &issue_id).await
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
        HqPlanRecord {
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

#[tauri::command]
pub async fn get_agents(app_state: State<'_, AppState>) -> Result<Vec<HqAgentDto>, String> {
    let repo = hq_repo(&app_state);
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
) -> Result<Option<HqAgentDto>, String> {
    let repo = hq_repo(&app_state);
    let Some(record) = repo.get_agent(&id).await.map_err(to_string_error)? else {
        return Ok(None);
    };
    agent_from_record(&repo, record).await.map(Some)
}

#[tauri::command]
pub async fn get_dashboard_metrics(
    app_state: State<'_, AppState>,
) -> Result<HqDashboardMetricsDto, String> {
    let repo = hq_repo(&app_state);
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

#[tauri::command]
pub async fn get_director_chat(
    app_state: State<'_, AppState>,
) -> Result<Vec<HqDirectorMessageDto>, String> {
    Ok(hq_repo(&app_state)
        .list_chat_messages(500)
        .await
        .map_err(to_string_error)?
        .into_iter()
        .map(chat_from_record)
        .collect())
}

#[tauri::command]
pub async fn send_director_message(
    message: String,
    epic_id: Option<String>,
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    let message = message.trim().to_string();
    if message.is_empty() {
        return Err("Message must not be empty.".to_string());
    }
    let repo = hq_repo(&app_state);
    append_chat_message(&repo, "user", message.clone(), epic_id.as_deref(), vec![]).await;

    let running = *bridge.running.read().await;
    if running {
        bridge
            .send_message(message.clone(), MessageTier::Steering)
            .await?;
        append_chat_message(
            &repo,
            "director",
            "Steering received. I forwarded it to the active HQ run.".to_string(),
            epic_id.as_deref(),
            vec![],
        )
        .await;
    }

    Ok(())
}
