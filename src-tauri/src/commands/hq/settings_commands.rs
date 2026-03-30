use ava_hq::{bootstrap_hq_memory, HqMemoryBootstrapOptions};
use tauri::State;

use super::data::{
    to_string_error, BootstrapHqWorkspaceArgs, HqSettingsDto, HqWorkspaceBootstrapDto,
    UpdateHqSettingsArgs,
};
use super::director_runtime::load_settings;
use super::mappings::{director_settings_to_dto, hq_bootstrap_to_dto};
use crate::bridge::DesktopBridge;

#[tauri::command]
pub async fn get_hq_settings(bridge: State<'_, DesktopBridge>) -> Result<HqSettingsDto, String> {
    Ok(director_settings_to_dto(&load_settings(&bridge).await))
}

#[tauri::command]
pub async fn bootstrap_hq_workspace(
    args: Option<BootstrapHqWorkspaceArgs>,
) -> Result<HqWorkspaceBootstrapDto, String> {
    let project_root = std::env::current_dir().map_err(to_string_error)?;
    let options = HqMemoryBootstrapOptions {
        director_model: args.as_ref().and_then(|value| value.director_model.clone()),
        force: args.as_ref().is_some_and(|value| value.force),
    };

    let result = bootstrap_hq_memory(&project_root, &options)
        .await
        .map_err(to_string_error)?;
    Ok(hq_bootstrap_to_dto(result))
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
