use std::collections::HashMap;
use std::sync::Arc;

use ava_agent::stack::AgentStack;
use ava_config::HqConfig as HqSettingsConfig;
use ava_db::models::{HqActivityRecord, HqAgentRecord, HqChatMessageRecord};
use ava_db::HqRepository;
use ava_hq::{Budget, Director, DirectorConfig, Domain};
use ava_platform::StandardPlatform;
use tauri::{AppHandle, Emitter, Manager};
use tokio::sync::mpsc;
use uuid::Uuid;

use super::data::{
    now_ms, serialize_json, to_string_error, HqDelegationCardDto, TeamConfigPayload,
};
use super::mappings::{collect_board_providers, hq_override, provider_from_override, status_color};
use crate::bridge::DesktopBridge;
use crate::commands::helpers::{parse_domain, resolve_model_spec};

pub(super) async fn ensure_director_agent(
    repo: &HqRepository,
    stack: &AgentStack,
) -> Result<(), String> {
    if repo
        .get_agent("director")
        .await
        .map_err(to_string_error)?
        .is_some()
    {
        return Ok(());
    }

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

pub(super) async fn purge_stale_director_chat(repo: &HqRepository) {
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

pub(super) fn spawn_simple_hq_run(
    app_handle: AppHandle,
    stack: Arc<AgentStack>,
    cancel: tokio_util::sync::CancellationToken,
    goal: String,
    task_type: ava_hq::TaskType,
    team_config: Option<TeamConfigPayload>,
    repo: Option<HqRepository>,
    epic_id: Option<String>,
) {
    tokio::spawn(async move {
        let settings = stack.config.get().await.hq;
        let director = match build_director(stack.clone(), team_config, &settings).await {
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
            description: goal,
            task_type,
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

        match result {
            Ok(session) => {
                if let Some(repo) = repo.as_ref() {
                    append_chat_message(
                        repo,
                        "director",
                        extract_director_reply(&session),
                        epic_id.as_deref(),
                        vec![],
                    )
                    .await;
                }
            }
            Err(error) => {
                if let Some(repo) = repo.as_ref() {
                    append_chat_message(
                        repo,
                        "director",
                        format!("HQ hit an error while working on that: {error}"),
                        epic_id.as_deref(),
                        vec![],
                    )
                    .await;
                }
                let _ = app_handle.emit(
                    "agent-event",
                    crate::events::AgentEvent::Error {
                        message: format!("HQ coordination failed: {error}"),
                    },
                );
            }
        }

        let bridge_ref = app_handle.state::<DesktopBridge>();
        *bridge_ref.running.write().await = false;
    });
}

pub(super) async fn append_activity(
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

pub(super) async fn append_chat_message(
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

pub(super) async fn load_settings(bridge: &DesktopBridge) -> HqSettingsConfig {
    bridge.stack.config.get().await.hq
}

pub(super) async fn build_director(
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
    let board_providers = collect_board_providers(
        &stack,
        settings,
        team_config.as_ref(),
        &provider_name,
        &model_name,
    )
    .await;
    Ok(Director::new(DirectorConfig {
        budget: Budget::interactive(200, 10.0),
        default_provider: director_provider,
        domain_providers,
        platform: Some(Arc::new(StandardPlatform)),
        scout_provider,
        board_providers,
        worker_names,
        enabled_leads,
        lead_prompts,
        worker_provider,
        role_resolver: None,
    }))
}
