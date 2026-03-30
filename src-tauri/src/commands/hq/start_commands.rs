use ava_types::MessageTier;
use tauri::{AppHandle, State};
use tracing::info;

use super::data::{hq_repo, to_string_error, HqStatus, StartHqArgs};
use super::director_runtime::spawn_simple_hq_run;
use crate::app_state::AppState;
use crate::bridge::DesktopBridge;

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
    spawn_simple_hq_run(
        app,
        stack,
        cancel,
        args.goal,
        ava_hq::TaskType::Simple,
        args.team_config,
        None,
        None,
    );

    Ok(())
}

#[tauri::command]
pub async fn get_hq_status(
    bridge: State<'_, DesktopBridge>,
    app_state: State<'_, AppState>,
) -> Result<HqStatus, String> {
    let running = *bridge.running.read().await;
    let agents = hq_repo(&app_state)
        .list_agents()
        .await
        .map_err(to_string_error)?;
    let worker_agents: Vec<_> = agents
        .into_iter()
        .filter(|agent| matches!(agent.tier.as_str(), "worker" | "scout"))
        .collect();

    Ok(HqStatus {
        running,
        total_workers: worker_agents.len(),
        succeeded: worker_agents
            .iter()
            .filter(|agent| agent.status == "idle" && agent.turn.unwrap_or_default() > 0)
            .count(),
        failed: worker_agents
            .iter()
            .filter(|agent| agent.status == "error")
            .count(),
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
