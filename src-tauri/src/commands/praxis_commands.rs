//! Tauri commands for Praxis multi-agent orchestration.

use ava_types::MessageTier;
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager, State};
use tokio::sync::mpsc;
use tracing::info;

use crate::bridge::DesktopBridge;
use super::helpers::{parse_domain, resolve_model_spec};

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
#[serde(rename_all = "camelCase")]
pub struct StartPraxisArgs {
    pub goal: String,
    /// Domain hint for task routing (auto-detected if None). Reserved for future use.
    #[serde(default)]
    #[allow(dead_code)]
    pub domain: Option<String>,
    /// Team configuration from the frontend settings.
    #[serde(default)]
    pub team_config: Option<TeamConfigPayload>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PraxisStatus {
    pub running: bool,
    pub total_workers: usize,
    pub succeeded: usize,
    pub failed: usize,
}

/// Start a Praxis multi-agent task. Spawns a Director that delegates to
/// domain-specific leads and streams PraxisEvents to the frontend.
#[tauri::command]
pub async fn start_praxis(
    args: StartPraxisArgs,
    app: AppHandle,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    // Prevent concurrent runs (shares the running flag with single-agent)
    {
        let running = bridge.running.read().await;
        if *running {
            return Err("Agent is already running. Cancel first.".to_string());
        }
    }
    *bridge.running.write().await = true;

    let cancel = bridge.new_cancel_token().await;
    let stack = bridge.stack.clone();

    // Resolve the current provider (used as default fallback)
    let (provider_name, model_name) = stack.current_model().await;
    let default_provider = stack
        .router
        .route_required(&provider_name, &model_name)
        .await
        .map_err(|e| e.to_string())?;

    let platform = std::sync::Arc::new(ava_platform::StandardPlatform);

    // Build DirectorConfig from the team settings
    let team_config = args.team_config.clone();

    // Resolve director provider: prefer team config director model, else default
    let director_provider = if let Some(ref tc) = team_config {
        resolve_model_spec(&stack, &tc.default_director_model)
            .await
            .unwrap_or_else(|| default_provider.clone())
    } else {
        default_provider.clone()
    };

    // Resolve scout provider
    let scout_provider = if let Some(ref tc) = team_config {
        resolve_model_spec(&stack, &tc.default_scout_model).await
    } else {
        None
    };

    // Resolve per-domain lead providers
    let mut domain_providers = std::collections::HashMap::new();
    if let Some(ref tc) = team_config {
        for lead_cfg in &tc.leads {
            if !lead_cfg.enabled {
                continue;
            }
            // Per-lead model override takes priority, then default_lead_model
            let model_spec = if lead_cfg.model.is_empty() {
                &tc.default_lead_model
            } else {
                &lead_cfg.model
            };
            if let Some(domain) = parse_domain(&lead_cfg.domain) {
                if let Some(provider) = resolve_model_spec(&stack, model_spec).await {
                    domain_providers.insert(domain, provider);
                }
            }
        }
    }

    // Determine enabled leads
    let enabled_leads: Vec<ava_praxis::Domain> = if let Some(ref tc) = team_config {
        tc.leads
            .iter()
            .filter(|l| l.enabled)
            .filter_map(|l| parse_domain(&l.domain))
            .collect()
    } else {
        vec![] // empty = all enabled
    };

    // Collect per-lead custom prompts
    let mut lead_prompts = std::collections::HashMap::new();
    if let Some(ref tc) = team_config {
        for lead_cfg in &tc.leads {
            if !lead_cfg.custom_prompt.is_empty() {
                if let Some(domain) = parse_domain(&lead_cfg.domain) {
                    lead_prompts.insert(domain, lead_cfg.custom_prompt.clone());
                }
            }
        }
    }

    // Worker names
    let worker_names: Vec<String> = team_config
        .as_ref()
        .map(|tc| tc.worker_names.clone())
        .unwrap_or_default();

    // Clone the app handle so we can access bridge state from the spawned task
    let app_handle = app.clone();

    tokio::spawn(async move {
        let mut director = ava_praxis::Director::new(ava_praxis::DirectorConfig {
            budget: ava_praxis::Budget::interactive(200, 10.0),
            default_provider: director_provider,
            domain_providers,
            platform: Some(platform),
            scout_provider,
            board_providers: vec![],
            worker_names,
            enabled_leads,
            lead_prompts,
        });

        let worker = match director.delegate(ava_praxis::Task {
            description: args.goal.clone(),
            task_type: ava_praxis::TaskType::Simple,
            files: vec![],
        }) {
            Ok(worker) => worker,
            Err(err) => {
                let _ = app_handle.emit(
                    "agent-event",
                    crate::events::AgentEvent::Error {
                        message: format!("Praxis delegation failed: {err}"),
                    },
                );
                let bridge_ref = app_handle.state::<DesktopBridge>();
                *bridge_ref.running.write().await = false;
                return;
            }
        };

        let (tx, mut rx) = mpsc::unbounded_channel();

        // Spawn a forwarder that converts PraxisEvents to Tauri events
        let app_fwd = app_handle.clone();
        let forwarder = tokio::spawn(async move {
            while let Some(event) = rx.recv().await {
                crate::events::emit_praxis_event(&app_fwd, &event);
            }
        });

        let result = director.coordinate(vec![worker], cancel, tx).await;
        let _ = forwarder.await;

        match &result {
            Ok(_session) => {
                info!(goal = %args.goal, "Praxis task completed successfully");
            }
            Err(e) => {
                let _ = app_handle.emit(
                    "agent-event",
                    crate::events::AgentEvent::Error {
                        message: format!("Praxis coordination failed: {e}"),
                    },
                );
            }
        }

        let bridge_ref = app_handle.state::<DesktopBridge>();
        *bridge_ref.running.write().await = false;
    });

    Ok(())
}

/// Get the current Praxis status (running state).
#[tauri::command]
pub async fn get_praxis_status(
    bridge: State<'_, DesktopBridge>,
) -> Result<PraxisStatus, String> {
    let running = *bridge.running.read().await;
    Ok(PraxisStatus {
        running,
        total_workers: 0,
        succeeded: 0,
        failed: 0,
    })
}

/// Cancel a running Praxis task (uses the same cancel token as single-agent).
#[tauri::command]
pub async fn cancel_praxis(
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    bridge.cancel().await;
    Ok(())
}

/// Send a steering message to a specific Praxis lead (currently forwards to
/// the shared message queue — individual lead steering requires tracking
/// per-lead channels which will be added when the Director supports it).
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
    bridge
        .send_message(message, MessageTier::Steering)
        .await
}
