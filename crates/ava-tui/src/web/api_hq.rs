use std::collections::HashMap;
use std::sync::Arc;

use ava_db::models::{
    HqActivityRecord, HqAgentRecord, HqChatMessageRecord, HqEpicRecord, HqIssueRecord,
};
use ava_db::HqRepository;
use ava_hq::{Budget, Director, DirectorConfig, Task, TaskType};
use ava_platform::StandardPlatform;
use ava_types::MessageTier;
use axum::extract::{Path, State};
use axum::Json;
use serde::{Deserialize, Serialize};
use serde_json::Value;
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

#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct SendDirectorMessageRequest {
    pub message: String,
    pub epic_id: Option<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "snake_case")]
pub struct UpdateHqSettingsRequest {
    pub director_model: Option<String>,
    pub tone_preference: Option<String>,
    pub auto_review: Option<bool>,
    pub show_costs: Option<bool>,
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

async fn build_director(state: &WebState) -> Result<Director, String> {
    let (provider_name, model_name) = state.inner.stack.current_model().await;
    let default_provider = state
        .inner
        .stack
        .router
        .route_required(&provider_name, &model_name)
        .await
        .map_err(|error| error.to_string())?;

    Ok(Director::new(DirectorConfig {
        budget: Budget::interactive(40, 5.0),
        default_provider,
        domain_providers: HashMap::new(),
        platform: Some(Arc::new(StandardPlatform)),
        scout_provider: None,
        board_providers: Vec::new(),
        worker_names: Vec::new(),
        enabled_leads: Vec::new(),
        lead_prompts: HashMap::new(),
        worker_provider: None,
    }))
}

fn spawn_simple_hq_run_web(
    state: WebState,
    repo: HqRepository,
    goal: String,
    epic_id: Option<String>,
    task_type: TaskType,
) {
    tokio::spawn(async move {
        let cancel = state.new_cancel_token().await;
        let director = match build_director(&state).await {
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
