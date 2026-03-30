use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use ava_config::{HqAgentOverride, HqConfig};
use ava_db::models::{
    HqActivityRecord, HqAgentRecord, HqChatMessageRecord, HqEpicRecord, HqIssueRecord, HqPlanRecord,
};
use ava_db::HqRepository;
use ava_hq::{
    bootstrap_hq_memory, Budget, Director, DirectorConfig, Domain, HqMemoryBootstrapOptions, Task,
    TaskType,
};
use ava_llm::provider::LLMProvider;
use ava_platform::StandardPlatform;
use ava_types::MessageTier;
use axum::extract::{Path, State};
use axum::Json;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tokio::sync::mpsc;
use uuid::Uuid;

use super::state::WebState;

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HqEpicDto {
    pub id: String,
    pub title: String,
    pub description: String,
    pub status: String,
    pub progress: usize,
    pub issue_ids: Vec<String>,
    pub plan_id: Option<String>,
    pub created_at: i64,
}

#[derive(Clone, Serialize)]
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
    pub comments: Vec<Value>,
    pub files_changed: Vec<Value>,
    pub agent_progress: Option<AgentProgressDto>,
    pub agent_live_action: Option<String>,
    pub is_live: bool,
    pub created_at: i64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentProgressDto {
    pub turn: usize,
    pub max_turns: usize,
}

#[derive(Clone, Serialize)]
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
    pub turn: Option<usize>,
    pub max_turns: Option<usize>,
    pub transcript: Vec<Value>,
    pub assigned_issue_ids: Vec<String>,
    pub files_touched: Vec<String>,
    pub total_cost_usd: f64,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HqActivityEventDto {
    pub id: String,
    pub r#type: String,
    pub agent_name: Option<String>,
    pub message: String,
    pub color: String,
    pub timestamp: i64,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqDelegationCardDto {
    pub agent_name: String,
    pub task: String,
    pub status: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HqDirectorMessageDto {
    pub id: String,
    pub role: String,
    pub content: String,
    pub delegations: Vec<HqDelegationCardDto>,
    pub timestamp: i64,
}

#[derive(Clone, Serialize)]
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

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HqSettingsDto {
    pub director_model: String,
    pub tone_preference: String,
    pub auto_review: bool,
    pub show_costs: bool,
}

#[derive(Clone, Serialize, Deserialize)]
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

#[derive(Clone, Serialize, Deserialize)]
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

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqPlanDto {
    pub id: String,
    pub epic_id: String,
    pub title: String,
    pub status: String,
    pub director_description: String,
    pub board_review: Option<Value>,
    pub phases: Vec<HqPhaseDto>,
}

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
#[serde(rename_all = "snake_case")]
pub struct SendDirectorMessageRequest {
    pub message: String,
    pub epic_id: Option<String>,
    pub team_config: Option<TeamConfigPayload>,
}

#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct UpdateHqSettingsRequest {
    pub director_model: Option<String>,
    pub tone_preference: Option<String>,
    pub auto_review: Option<bool>,
    pub show_costs: Option<bool>,
}

#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct BootstrapHqWorkspaceRequest {
    pub director_model: Option<String>,
    #[serde(default)]
    pub force: bool,
}

#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct CreateEpicRequest {
    pub title: String,
    #[serde(default)]
    pub description: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct RejectPlanRequest {
    #[serde(default)]
    pub feedback: String,
}

fn repo(state: &WebState) -> HqRepository {
    HqRepository::new(state.inner.db.pool().clone())
}

fn now_ms() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis() as i64)
        .unwrap_or_default()
}

fn parse_json_vec<T: serde::de::DeserializeOwned>(raw: Option<&str>) -> Vec<T> {
    raw.and_then(|value| serde_json::from_str(value).ok())
        .unwrap_or_default()
}

fn complexity_label(complexity: &ava_hq::TaskComplexity) -> &'static str {
    match complexity {
        ava_hq::TaskComplexity::Simple => "simple",
        ava_hq::TaskComplexity::Medium => "medium",
        ava_hq::TaskComplexity::Complex => "complex",
    }
}

fn phase_execution_label(task_count: usize) -> &'static str {
    if task_count > 1 {
        "parallel"
    } else {
        "sequential"
    }
}

fn domain_assignee(domain: &Domain) -> (&'static str, &'static str) {
    match domain {
        Domain::Frontend => ("frontend-lead", "Luna"),
        Domain::Backend => ("backend-lead", "Pedro"),
        Domain::QA => ("qa-lead", "Kai"),
        Domain::Research => ("research-lead", "Scout Lead"),
        Domain::Debug => ("debug-lead", "Sofia"),
        Domain::Fullstack => ("fullstack-lead", "Sofia"),
        Domain::DevOps => ("devops-lead", "Rio"),
    }
}

fn convert_plan(epic_id: &str, plan_id: &str, status: &str, plan: &ava_hq::HqPlan) -> HqPlanDto {
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
                    let (assignee_id, assignee_name) = domain_assignee(&task.domain);
                    HqPlanTaskDto {
                        id: task.id.clone(),
                        title: task.description.clone(),
                        domain: format!("{:?}", task.domain).to_ascii_lowercase(),
                        complexity: complexity_label(&task.complexity).to_string(),
                        dependencies: task.dependencies.clone(),
                        assignee_id: Some(assignee_id.to_string()),
                        assignee_name: Some(assignee_name.to_string()),
                        assignee_model: None,
                        steps: vec![task.description.clone()],
                        file_hints: task.files_hint.clone(),
                        budget_max_tokens: task.budget.max_tokens,
                        budget_max_turns: task.budget.max_turns,
                        budget_max_cost_usd: task.budget.max_cost_usd,
                        expanded: matches!(task.complexity, ava_hq::TaskComplexity::Complex),
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
                review_enabled: true,
                review_assignee: Some("QA Lead".to_string()),
            }
        })
        .collect();

    HqPlanDto {
        id: plan_id.to_string(),
        epic_id: epic_id.to_string(),
        title: plan.goal.clone(),
        status: status.to_string(),
        director_description: format!(
            "Director prepared a phased plan with {} phase(s) and {} task(s).",
            plan.execution_groups.len(),
            plan.tasks.len()
        ),
        board_review: None,
        phases,
    }
}

async fn create_plan_issues(
    repo: &HqRepository,
    epic_id: &str,
    plan: &HqPlanDto,
) -> Result<(), String> {
    for phase in &plan.phases {
        for task in &phase.tasks {
            let issue_number = repo
                .next_issue_number()
                .await
                .map_err(|error| error.to_string())?;
            repo.create_issue(&HqIssueRecord {
                id: Uuid::new_v4().to_string(),
                issue_number,
                identifier: format!("HQ-{issue_number}"),
                title: task.title.clone(),
                description: task.steps.join("\n"),
                status: "backlog".to_string(),
                priority: match task.complexity.as_str() {
                    "complex" => "high".to_string(),
                    "medium" => "medium".to_string(),
                    _ => "low".to_string(),
                },
                assignee_id: task.assignee_id.clone(),
                assignee_name: task.assignee_name.clone(),
                epic_id: epic_id.to_string(),
                phase_label: Some(phase.name.clone()),
                agent_turn: None,
                agent_max_turns: None,
                agent_live_action: None,
                is_live: 0,
                files_changed_json: Some("[]".to_string()),
                created_at: now_ms(),
                updated_at: now_ms(),
            })
            .await
            .map_err(|error| error.to_string())?;
        }
    }
    Ok(())
}

fn should_consult_board(plan: &ava_hq::HqPlan) -> bool {
    plan.tasks.len() >= 4
        || plan
            .tasks
            .iter()
            .any(|task| task.complexity == ava_hq::TaskComplexity::Complex)
}

fn fallback_hq_plan(goal: &str) -> ava_hq::HqPlan {
    ava_hq::HqPlan {
        goal: goal.to_string(),
        tasks: vec![ava_hq::HqTask {
            id: "t1".to_string(),
            description: goal.lines().next().unwrap_or(goal).trim().to_string(),
            domain: Domain::Fullstack,
            complexity: ava_hq::TaskComplexity::Medium,
            dependencies: vec![],
            budget: Budget::interactive(20, 2.0),
            files_hint: vec![],
        }],
        execution_groups: vec![ava_hq::ExecutionGroup {
            task_ids: vec!["t1".to_string()],
            label: "Phase 1: Implementation".to_string(),
        }],
        total_budget: Budget::interactive(20, 2.0),
    }
}

fn board_review_value(result: &ava_hq::BoardResult) -> Value {
    json!({
        "consensus": result.consensus,
        "voteSummary": result.vote_summary,
        "opinions": result.opinions.iter().map(|opinion| json!({
            "memberName": opinion.member_name,
            "personality": opinion.personality.to_string(),
            "recommendation": opinion.recommendation,
            "vote": opinion.vote.to_string(),
        })).collect::<Vec<_>>()
    })
}

async fn ensure_director_agent(state: &WebState, repo: &HqRepository) -> Result<(), String> {
    if repo
        .get_agent("director")
        .await
        .map_err(|error| error.to_string())?
        .is_some()
    {
        return Ok(());
    }

    let now = now_ms();
    let (_, model_name) = state.inner.stack.current_model().await;
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
    .map_err(|error| error.to_string())
}

async fn purge_stale_director_chat(repo: &HqRepository) {
    let _ = repo
        .delete_chat_messages_by_content(&[
            "Understood. I am kicking off HQ work now.",
            "Steering received. I forwarded it to the active HQ run.",
            "Understood. Web HQ preview stored your note; desktop runtime execution is available in the Tauri app.",
        ])
        .await;
}

fn extract_director_reply(session: &ava_types::Session) -> String {
    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .map(|message| message.content.trim())
        .filter(|content| !content.is_empty())
        .collect();

    if !assistant_messages.is_empty() {
        return assistant_messages.join("\n\n");
    }

    session
        .messages
        .iter()
        .rev()
        .find(|message| {
            !matches!(message.role, ava_types::Role::System) && !message.content.trim().is_empty()
        })
        .map(|message| message.content.trim().to_string())
        .unwrap_or_else(|| "HQ finished without a textual reply.".to_string())
}

fn parse_domain(s: &str) -> Option<Domain> {
    match s.to_lowercase().as_str() {
        "frontend" => Some(Domain::Frontend),
        "backend" => Some(Domain::Backend),
        "qa" => Some(Domain::QA),
        "research" => Some(Domain::Research),
        "debug" => Some(Domain::Debug),
        "fullstack" => Some(Domain::Fullstack),
        "devops" => Some(Domain::DevOps),
        _ => None,
    }
}

fn hq_override<'a>(settings: &'a HqConfig, id: &str) -> Option<&'a HqAgentOverride> {
    settings
        .agent_overrides
        .iter()
        .find(|override_item| override_item.id == id)
}

async fn resolve_model_spec(state: &WebState, model_spec: &str) -> Option<Arc<dyn LLMProvider>> {
    if model_spec.is_empty() {
        return None;
    }

    let (provider_name, model_name) = if let Some(idx) = model_spec.find('/') {
        let prov = &model_spec[..idx];
        let mdl = &model_spec[idx + 1..];
        (prov.to_string(), mdl.to_string())
    } else {
        let (cur_prov, _) = state.inner.stack.current_model().await;
        (cur_prov, model_spec.to_string())
    };

    state
        .inner
        .stack
        .router
        .route_required(&provider_name, &model_name)
        .await
        .ok()
}

async fn provider_from_override(
    state: &WebState,
    override_item: Option<&HqAgentOverride>,
) -> Option<Arc<dyn LLMProvider>> {
    let model_spec = override_item
        .map(|item| item.model_spec.trim())
        .unwrap_or("");
    resolve_model_spec(state, model_spec).await
}

async fn build_director(
    state: &WebState,
    settings: &HqConfig,
    team_config: Option<TeamConfigPayload>,
) -> Result<Director, String> {
    let (provider_name, model_name) = state.inner.stack.current_model().await;
    let default_provider = state
        .inner
        .stack
        .router
        .route_required(&provider_name, &model_name)
        .await
        .map_err(|error| error.to_string())?;

    let mut domain_providers = HashMap::new();
    let mut enabled_leads = Vec::new();
    let mut lead_prompts = HashMap::new();
    let mut worker_names = Vec::new();

    let commander_override = hq_override(settings, "commander").filter(|item| item.enabled);
    let coder_override = hq_override(settings, "coder").filter(|item| item.enabled);
    let researcher_override = hq_override(settings, "researcher")
        .or_else(|| hq_override(settings, "explorer"))
        .filter(|item| item.enabled);

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
                if let Some(provider) = resolve_model_spec(state, model_spec).await {
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
            if let Some(provider) = provider_from_override(state, lead_override).await {
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
        if let Some(provider) = provider_from_override(state, commander_override).await {
            provider
        } else if !settings.director_model.is_empty() {
            resolve_model_spec(state, &settings.director_model)
                .await
                .unwrap_or_else(|| default_provider.clone())
        } else if let Some(ref team) = team_config {
            resolve_model_spec(state, &team.default_director_model)
                .await
                .unwrap_or_else(|| default_provider.clone())
        } else {
            default_provider.clone()
        };

    let scout_provider =
        if let Some(provider) = provider_from_override(state, researcher_override).await {
            Some(provider)
        } else if let Some(ref team) = team_config {
            resolve_model_spec(state, &team.default_scout_model).await
        } else {
            None
        };

    let worker_provider =
        if let Some(provider) = provider_from_override(state, coder_override).await {
            Some(provider)
        } else if let Some(ref team) = team_config {
            resolve_model_spec(state, &team.default_worker_model).await
        } else {
            None
        };

    Ok(Director::new(DirectorConfig {
        budget: Budget::interactive(40, 5.0),
        default_provider: director_provider,
        domain_providers,
        platform: Some(Arc::new(StandardPlatform)),
        scout_provider,
        board_providers: Vec::new(),
        worker_names,
        enabled_leads,
        lead_prompts,
        worker_provider,
    }))
}

fn spawn_simple_hq_run_web(
    state: WebState,
    repo: HqRepository,
    goal: String,
    epic_id: Option<String>,
    task_type: TaskType,
    team_config: Option<TeamConfigPayload>,
) {
    tokio::spawn(async move {
        let cancel = state.new_cancel_token().await;
        let settings = state.inner.stack.config.get().await.hq;
        let director = match build_director(&state, &settings, team_config).await {
            Ok(director) => director,
            Err(error) => {
                let _ = repo
                    .add_chat_message(&HqChatMessageRecord {
                        id: Uuid::new_v4().to_string(),
                        role: "director".to_string(),
                        content: format!("HQ setup failed: {error}"),
                        delegations_json: None,
                        epic_id,
                        timestamp: now_ms(),
                    })
                    .await;
                *state.inner.running.write().await = false;
                return;
            }
        };

        let mut director = director;
        let worker = match director.delegate(Task {
            description: goal,
            task_type,
            files: vec![],
        }) {
            Ok(worker) => worker,
            Err(error) => {
                let _ = repo
                    .add_chat_message(&HqChatMessageRecord {
                        id: Uuid::new_v4().to_string(),
                        role: "director".to_string(),
                        content: format!("HQ delegation failed: {error}"),
                        delegations_json: None,
                        epic_id,
                        timestamp: now_ms(),
                    })
                    .await;
                *state.inner.running.write().await = false;
                return;
            }
        };

        let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel();
        let event_tx = state.inner.event_tx.clone();
        let drain = tokio::spawn(async move {
            while let Some(event) = rx.recv().await {
                let _ = event_tx.send(super::state::WebEvent::Hq(event));
            }
        });
        let result = director.coordinate(vec![worker], cancel, tx).await;
        let _ = drain.await;

        let content = match result {
            Ok(session) => extract_director_reply(&session),
            Err(error) => format!("HQ hit an error while working on that: {error}"),
        };

        let _ = repo
            .add_chat_message(&HqChatMessageRecord {
                id: Uuid::new_v4().to_string(),
                role: "director".to_string(),
                content,
                delegations_json: None,
                epic_id,
                timestamp: now_ms(),
            })
            .await;
        *state.inner.running.write().await = false;
    });
}

fn map_agent(record: HqAgentRecord) -> HqAgentDto {
    HqAgentDto {
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
        turn: record.turn.map(|value| value as usize),
        max_turns: record.max_turns.map(|value| value as usize),
        transcript: Vec::new(),
        assigned_issue_ids: parse_json_vec(record.assigned_issue_ids_json.as_deref()),
        files_touched: parse_json_vec(record.files_touched_json.as_deref()),
        total_cost_usd: record.total_cost_usd,
    }
}

fn map_chat(record: HqChatMessageRecord) -> HqDirectorMessageDto {
    HqDirectorMessageDto {
        id: record.id,
        role: record.role,
        content: record.content,
        delegations: parse_json_vec(record.delegations_json.as_deref()),
        timestamp: record.timestamp,
    }
}

fn map_activity(record: HqActivityRecord) -> HqActivityEventDto {
    HqActivityEventDto {
        id: record.id,
        r#type: record.event_type,
        agent_name: record.agent_name,
        message: record.message,
        color: record.color,
        timestamp: record.timestamp,
    }
}

fn map_epic(record: HqEpicRecord) -> HqEpicDto {
    map_epic_with_issues(record, Vec::new())
}

fn map_epic_with_issues(record: HqEpicRecord, issue_ids: Vec<String>) -> HqEpicDto {
    HqEpicDto {
        id: record.id,
        title: record.title,
        description: record.description,
        status: record.status,
        progress: record.progress as usize,
        issue_ids,
        plan_id: record.plan_id,
        created_at: record.created_at,
    }
}

fn map_issue(record: HqIssueRecord) -> HqIssueDto {
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
        comments: Vec::new(),
        files_changed: parse_json_vec(record.files_changed_json.as_deref()),
        agent_progress: match (record.agent_turn, record.agent_max_turns) {
            (Some(turn), Some(max_turns)) => Some(AgentProgressDto {
                turn: turn as usize,
                max_turns: max_turns as usize,
            }),
            _ => None,
        },
        agent_live_action: record.agent_live_action,
        is_live: record.is_live != 0,
        created_at: record.created_at,
    }
}

async fn plan_epic_background_web(
    state: WebState,
    repo: HqRepository,
    epic_id: String,
    title: String,
    description: String,
    team_config: Option<TeamConfigPayload>,
) {
    let settings = state.inner.stack.config.get().await.hq;
    let goal = if description.trim().is_empty() {
        title.clone()
    } else {
        format!("{title}\n\nAdditional context:\n{description}")
    };

    let result = async {
        let director = build_director(&state, &settings, team_config.clone()).await?;
        let raw_plan = match tokio::time::timeout(Duration::from_secs(20), director.plan(&goal, None)).await {
            Ok(Ok(plan)) => plan,
            Ok(Err(_)) | Err(_) => fallback_hq_plan(&goal),
        };
        let board_review = if should_consult_board(&raw_plan) {
            let (tx, rx) = mpsc::unbounded_channel();
            drop(rx);
            director
                .consult_board(&goal, &[], tx)
                .await
                .map_err(|error| error.to_string())?
                .as_ref()
                .map(board_review_value)
        } else {
            None
        };
        let plan_id = repo
            .get_epic(&epic_id)
            .await
            .map_err(|error| error.to_string())?
            .and_then(|epic| epic.plan_id)
            .unwrap_or_else(|| Uuid::new_v4().to_string());
        let mut plan = convert_plan(&epic_id, &plan_id, "awaiting-approval", &raw_plan);
        plan.board_review = board_review;

        repo.save_plan(&HqPlanRecord {
            id: plan.id.clone(),
            epic_id: epic_id.clone(),
            title: plan.title.clone(),
            status: plan.status.clone(),
            director_description: plan.director_description.clone(),
            plan_json: serde_json::to_string(&plan).map_err(|error| error.to_string())?,
            created_at: now_ms(),
            updated_at: now_ms(),
        })
        .await
        .map_err(|error| error.to_string())?;

        repo.delete_issues_by_epic(&epic_id)
            .await
            .map_err(|error| error.to_string())?;
        create_plan_issues(&repo, &epic_id, &plan).await?;

        if let Some(mut epic) = repo.get_epic(&epic_id).await.map_err(|error| error.to_string())? {
            epic.plan_id = Some(plan_id.clone());
            epic.status = "planning".to_string();
            epic.progress = 15;
            epic.updated_at = now_ms();
            repo.update_epic(&epic)
                .await
                .map_err(|error| error.to_string())?;
        }

        repo.add_chat_message(&HqChatMessageRecord {
            id: Uuid::new_v4().to_string(),
            role: "director".to_string(),
            content: format!(
                "Plan ready: {} phase(s), {} task(s). Review it in the Plan screen before execution.",
                plan.phases.len(),
                raw_plan.tasks.len()
            ),
            delegations_json: None,
            epic_id: Some(epic_id.clone()),
            timestamp: now_ms(),
        })
        .await
        .map_err(|error| error.to_string())?;
        Ok::<(), String>(())
    }
    .await;

    *state.inner.running.write().await = false;
    if let Err(error) = result {
        let _ = repo
            .create_activity(&HqActivityRecord {
                id: Uuid::new_v4().to_string(),
                event_type: "error".to_string(),
                agent_name: Some("Director".to_string()),
                message: format!("Planning failed for epic '{title}': {error}"),
                color: "#FF453A".to_string(),
                timestamp: now_ms(),
            })
            .await;
    }
}

pub async fn list_epics(State(state): State<WebState>) -> Result<Json<Vec<HqEpicDto>>, String> {
    let repo = repo(&state);
    let epics = repo.list_epics().await.map_err(|error| error.to_string())?;
    let issues = repo
        .list_issues(None)
        .await
        .map_err(|error| error.to_string())?;
    Ok(Json(
        epics
            .into_iter()
            .map(|epic| {
                let issue_ids = issues
                    .iter()
                    .filter(|issue| issue.epic_id == epic.id)
                    .map(|issue| issue.id.clone())
                    .collect();
                map_epic_with_issues(epic, issue_ids)
            })
            .collect(),
    ))
}

pub async fn create_epic(
    State(state): State<WebState>,
    Json(payload): Json<CreateEpicRequest>,
) -> Result<Json<HqEpicDto>, String> {
    let title = payload.title.trim().to_string();
    if title.is_empty() {
        return Err("Epic title must not be empty.".to_string());
    }

    let repo = repo(&state);
    ensure_director_agent(&state, &repo).await?;
    {
        let mut running = state.inner.running.write().await;
        if *running {
            return Err(
                "HQ is already running. Wait for the current action to finish or cancel it."
                    .to_string(),
            );
        }
        *running = true;
    }

    let now = now_ms();
    let epic = HqEpicRecord {
        id: Uuid::new_v4().to_string(),
        title: title.clone(),
        description: payload.description.clone(),
        status: "planning".to_string(),
        progress: 5,
        plan_id: None,
        created_at: now,
        updated_at: now,
    };
    repo.create_epic(&epic)
        .await
        .map_err(|error| error.to_string())?;

    tokio::spawn(plan_epic_background_web(
        state.clone(),
        repo.clone(),
        epic.id.clone(),
        title,
        payload.description,
        None,
    ));

    Ok(Json(HqEpicDto {
        id: epic.id,
        title: epic.title,
        description: epic.description,
        status: epic.status,
        progress: epic.progress as usize,
        issue_ids: vec![],
        plan_id: epic.plan_id,
        created_at: epic.created_at,
    }))
}

pub async fn get_plan(
    Path(epic_id): Path<String>,
    State(state): State<WebState>,
) -> Result<Json<Option<HqPlanDto>>, String> {
    let repo = repo(&state);
    let Some(record) = repo
        .get_plan_by_epic(&epic_id)
        .await
        .map_err(|error| error.to_string())?
    else {
        return Ok(Json(None));
    };
    Ok(Json(Some(
        serde_json::from_str(&record.plan_json).map_err(|error| error.to_string())?,
    )))
}

pub async fn approve_plan(
    Path(plan_id): Path<String>,
    State(state): State<WebState>,
) -> Result<Json<Option<HqPlanDto>>, String> {
    let repo = repo(&state);
    let Some(record) = repo
        .get_plan(&plan_id)
        .await
        .map_err(|error| error.to_string())?
    else {
        return Ok(Json(None));
    };
    repo.update_plan_status(&plan_id, "executing", now_ms())
        .await
        .map_err(|error| error.to_string())?;
    let mut parsed: HqPlanDto =
        serde_json::from_str(&record.plan_json).map_err(|error| error.to_string())?;
    parsed.status = "executing".to_string();
    Ok(Json(Some(parsed)))
}

pub async fn reject_plan(
    Path(plan_id): Path<String>,
    State(state): State<WebState>,
    Json(payload): Json<RejectPlanRequest>,
) -> Result<Json<Option<HqPlanDto>>, String> {
    let repo = repo(&state);
    let Some(record) = repo
        .get_plan(&plan_id)
        .await
        .map_err(|error| error.to_string())?
    else {
        return Ok(Json(None));
    };
    repo.update_plan_status(&plan_id, "rejected", now_ms())
        .await
        .map_err(|error| error.to_string())?;
    if let Some(epic) = repo
        .get_epic(&record.epic_id)
        .await
        .map_err(|error| error.to_string())?
    {
        let mut running = state.inner.running.write().await;
        *running = true;
        drop(running);
        tokio::spawn(plan_epic_background_web(
            state.clone(),
            repo.clone(),
            record.epic_id.clone(),
            epic.title,
            if payload.feedback.trim().is_empty() {
                epic.description
            } else {
                format!(
                    "{}\n\nRevision feedback:\n{}",
                    epic.description, payload.feedback
                )
            },
            None,
        ));
    }
    let mut parsed: HqPlanDto =
        serde_json::from_str(&record.plan_json).map_err(|error| error.to_string())?;
    parsed.status = "rejected".to_string();
    Ok(Json(Some(parsed)))
}

pub async fn list_issues(State(state): State<WebState>) -> Result<Json<Vec<HqIssueDto>>, String> {
    let repo = repo(&state);
    Ok(Json(
        repo.list_issues(None)
            .await
            .map_err(|error| error.to_string())?
            .into_iter()
            .map(map_issue)
            .collect(),
    ))
}

pub async fn get_agents(State(state): State<WebState>) -> Result<Json<Vec<HqAgentDto>>, String> {
    let repo = repo(&state);
    ensure_director_agent(&state, &repo).await?;
    Ok(Json(
        repo.list_agents()
            .await
            .map_err(|error| error.to_string())?
            .into_iter()
            .map(map_agent)
            .collect(),
    ))
}

pub async fn get_agent(
    Path(id): Path<String>,
    State(state): State<WebState>,
) -> Result<Json<Option<HqAgentDto>>, String> {
    let repo = repo(&state);
    ensure_director_agent(&state, &repo).await?;
    Ok(Json(
        repo.get_agent(&id)
            .await
            .map_err(|error| error.to_string())?
            .map(map_agent),
    ))
}

pub async fn get_activity_feed(
    State(state): State<WebState>,
) -> Result<Json<Vec<HqActivityEventDto>>, String> {
    let repo = repo(&state);
    Ok(Json(
        repo.list_activity(100)
            .await
            .map_err(|error| error.to_string())?
            .into_iter()
            .map(map_activity)
            .collect(),
    ))
}

pub async fn get_dashboard_metrics(
    State(state): State<WebState>,
) -> Result<Json<HqDashboardMetricsDto>, String> {
    let repo = repo(&state);
    ensure_director_agent(&state, &repo).await?;
    let agents = repo
        .list_agents()
        .await
        .map_err(|error| error.to_string())?;
    let epics = repo.list_epics().await.map_err(|error| error.to_string())?;
    let issues = repo
        .list_issues(None)
        .await
        .map_err(|error| error.to_string())?;
    let issues_done = issues.iter().filter(|issue| issue.status == "done").count();
    Ok(Json(HqDashboardMetricsDto {
        agents_active: agents
            .iter()
            .filter(|agent| matches!(agent.status.as_str(), "active" | "running"))
            .count(),
        agents_running: agents
            .iter()
            .filter(|agent| agent.status == "running")
            .count(),
        agents_idle: agents.iter().filter(|agent| agent.status == "idle").count(),
        epics_in_progress: epics
            .iter()
            .filter(|epic| epic.status == "in-progress")
            .count(),
        issues_open: issues.iter().filter(|issue| issue.status != "done").count(),
        issues_in_progress: issues
            .iter()
            .filter(|issue| issue.status == "in-progress")
            .count(),
        issues_in_review: issues
            .iter()
            .filter(|issue| issue.status == "review")
            .count(),
        issues_done,
        success_rate: if issues.is_empty() {
            100
        } else {
            ((issues_done as f64 / issues.len() as f64) * 100.0).round() as usize
        },
        total_cost_usd: agents.iter().map(|agent| agent.total_cost_usd).sum(),
        payg_agents_tracked: agents
            .iter()
            .filter(|agent| agent.total_cost_usd > 0.0)
            .count(),
    }))
}

pub async fn get_director_chat(
    State(state): State<WebState>,
) -> Result<Json<Vec<HqDirectorMessageDto>>, String> {
    let repo = repo(&state);
    ensure_director_agent(&state, &repo).await?;
    purge_stale_director_chat(&repo).await;
    Ok(Json(
        repo.list_chat_messages(500)
            .await
            .map_err(|error| error.to_string())?
            .into_iter()
            .map(map_chat)
            .collect(),
    ))
}

pub async fn send_director_message(
    State(state): State<WebState>,
    Json(payload): Json<SendDirectorMessageRequest>,
) -> Result<Json<()>, String> {
    let message = payload.message.trim().to_string();
    if message.is_empty() {
        return Err("Message must not be empty.".to_string());
    }
    let repo = repo(&state);
    ensure_director_agent(&state, &repo).await?;

    repo.add_chat_message(&HqChatMessageRecord {
        id: Uuid::new_v4().to_string(),
        role: "user".to_string(),
        content: message.clone(),
        delegations_json: None,
        epic_id: payload.epic_id.clone(),
        timestamp: now_ms(),
    })
    .await
    .map_err(|error| error.to_string())?;

    let queue = state.inner.message_queue.read().await.clone();
    let mut running = state.inner.running.write().await;
    if *running {
        if let Some(queue) = queue.as_ref() {
            let _ = queue.send(ava_types::QueuedMessage {
                text: message,
                tier: MessageTier::Steering,
            });
        }
        drop(running);
        repo.add_chat_message(&HqChatMessageRecord {
            id: Uuid::new_v4().to_string(),
            role: "director".to_string(),
            content: "Steering note received for the active HQ run.".to_string(),
            delegations_json: None,
            epic_id: payload.epic_id,
            timestamp: now_ms(),
        })
        .await
        .map_err(|error| error.to_string())?;
        return Ok(Json(()));
    } else {
        *running = true;
        let epic_id = payload.epic_id.clone();
        drop(running);
        spawn_simple_hq_run_web(
            state.clone(),
            repo.clone(),
            message,
            epic_id,
            TaskType::Chat,
            payload.team_config,
        );
        return Ok(Json(()));
    }
}

pub async fn get_hq_settings(State(state): State<WebState>) -> Result<Json<HqSettingsDto>, String> {
    let settings = state.inner.stack.config.get().await.hq;
    Ok(Json(HqSettingsDto {
        director_model: settings.director_model,
        tone_preference: settings.tone_preference,
        auto_review: settings.auto_review,
        show_costs: settings.show_costs,
    }))
}

pub async fn bootstrap_hq_workspace(
    Json(request): Json<BootstrapHqWorkspaceRequest>,
) -> Result<Json<ava_hq::HqMemoryBootstrapResult>, String> {
    let project_root = std::env::current_dir().map_err(|error| error.to_string())?;
    let result = bootstrap_hq_memory(
        &project_root,
        &HqMemoryBootstrapOptions {
            director_model: request.director_model,
            force: request.force,
        },
    )
    .await
    .map_err(|error| error.to_string())?;
    Ok(Json(result))
}

pub async fn update_hq_settings(
    State(state): State<WebState>,
    Json(payload): Json<UpdateHqSettingsRequest>,
) -> Result<Json<HqSettingsDto>, String> {
    state
        .inner
        .stack
        .config
        .update(|config| {
            if let Some(director_model) = &payload.director_model {
                config.hq.director_model = director_model.clone();
            }
            if let Some(tone_preference) = &payload.tone_preference {
                config.hq.tone_preference = tone_preference.clone();
            }
            if let Some(auto_review) = payload.auto_review {
                config.hq.auto_review = auto_review;
            }
            if let Some(show_costs) = payload.show_costs {
                config.hq.show_costs = show_costs;
            }
        })
        .await
        .map_err(|error| error.to_string())?;
    state
        .inner
        .stack
        .config
        .save()
        .await
        .map_err(|error| error.to_string())?;
    get_hq_settings(State(state)).await
}
