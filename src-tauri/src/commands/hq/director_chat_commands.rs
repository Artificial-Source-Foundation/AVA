use ava_types::MessageTier;
use tauri::{AppHandle, State};

use super::data::{hq_repo, HqDirectorMessageDto, TeamConfigPayload};
use super::director_runtime::{
    append_chat_message, ensure_director_agent, purge_stale_director_chat, spawn_simple_hq_run,
};
use super::mappings::chat_from_record;
use crate::app_state::AppState;
use crate::bridge::DesktopBridge;

#[tauri::command]
pub async fn get_director_chat(
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<HqDirectorMessageDto>, String> {
    let repo = hq_repo(&app_state);
    ensure_director_agent(&repo, &bridge.stack).await?;
    purge_stale_director_chat(&repo).await;
    Ok(repo
        .list_chat_messages(500)
        .await
        .map_err(super::data::to_string_error)?
        .into_iter()
        .map(chat_from_record)
        .collect())
}

#[tauri::command]
pub async fn send_director_message(
    message: String,
    epic_id: Option<String>,
    team_config: Option<TeamConfigPayload>,
    app: AppHandle,
    app_state: State<'_, AppState>,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    let message = message.trim().to_string();
    if message.is_empty() {
        return Err("Message must not be empty.".to_string());
    }
    let repo = hq_repo(&app_state);
    ensure_director_agent(&repo, &bridge.stack).await?;
    append_chat_message(&repo, "user", message.clone(), epic_id.as_deref(), vec![]).await;

    let mut running = bridge.running.write().await;
    if *running {
        bridge
            .send_message(message.clone(), MessageTier::Steering)
            .await?;
        drop(running);
        append_chat_message(
            &repo,
            "director",
            "Steering note received for the active HQ run.".to_string(),
            epic_id.as_deref(),
            vec![],
        )
        .await;
        Ok(())
    } else {
        *running = true;
        let cancel = bridge.new_cancel_token().await;
        let stack = bridge.stack.clone();
        let repo_for_run = repo.clone();
        let epic_id_for_run = epic_id.clone();
        drop(running);
        spawn_simple_hq_run(
            app,
            stack,
            cancel,
            message,
            ava_hq::TaskType::Chat,
            team_config,
            Some(repo_for_run),
            epic_id_for_run,
        );
        Ok(())
    }
}
