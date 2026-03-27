//! Tauri commands for subscription usage tracking.
//!
//! Queries provider APIs for plan type, usage windows, and remaining credits
//! using stored OAuth tokens and API keys.

use tauri::State;

use crate::bridge::DesktopBridge;

/// Fetch subscription usage from all configured providers.
#[tauri::command]
pub async fn get_subscription_usage(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<ava_llm::usage::SubscriptionUsage>, String> {
    let credentials = bridge.stack.router.credentials_snapshot().await;
    Ok(ava_llm::usage::fetch_all_subscription_usage(&credentials).await)
}
