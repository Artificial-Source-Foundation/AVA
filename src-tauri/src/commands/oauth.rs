use serde::Serialize;

#[derive(Serialize)]
pub struct OAuthCallback {
    pub code: String,
    pub state: String,
}

#[derive(Debug, Serialize)]
pub struct CopilotDeviceCodeResponse {
    pub device_code: String,
    pub user_code: String,
    pub verification_uri: String,
    pub expires_in: u64,
    pub interval: Option<u64>,
}

#[derive(Debug, Serialize)]
pub struct CopilotDevicePollResponse {
    pub access_token: Option<String>,
    pub refresh_token: Option<String>,
    pub expires_in: Option<u64>,
    pub error: Option<String>,
}

/// Start a one-shot HTTP server on the given port to catch an OAuth callback.
/// Delegates to the shared `ava-auth` crate.
#[tauri::command]
pub async fn oauth_listen(port: u16) -> Result<OAuthCallback, String> {
    let cb = ava_auth::callback::listen_for_callback(port, "/auth/callback", 120)
        .await
        .map_err(|e| e.to_string())?;

    Ok(OAuthCallback {
        code: cb.code,
        state: cb.state,
    })
}

#[tauri::command]
pub async fn oauth_copilot_device_start(
    _client_id: String,
    _scope: String,
) -> Result<CopilotDeviceCodeResponse, String> {
    let config = ava_auth::config::oauth_config("copilot")
        .ok_or_else(|| "No OAuth config for copilot".to_string())?;

    let device = ava_auth::device_code::request_device_code(config)
        .await
        .map_err(|e| e.to_string())?;

    Ok(CopilotDeviceCodeResponse {
        device_code: device.device_code,
        user_code: device.user_code,
        verification_uri: device.verification_uri,
        expires_in: device.expires_in,
        interval: Some(device.interval),
    })
}

#[tauri::command]
pub async fn oauth_copilot_device_poll(
    _client_id: String,
    device_code: String,
) -> Result<CopilotDevicePollResponse, String> {
    let config = ava_auth::config::oauth_config("copilot")
        .ok_or_else(|| "No OAuth config for copilot".to_string())?;

    let result = ava_auth::device_code::poll_device_code(config, &device_code, 5, 900)
        .await
        .map_err(|e| e.to_string())?;

    match result {
        Some(tokens) => Ok(CopilotDevicePollResponse {
            access_token: Some(tokens.access_token),
            refresh_token: tokens.refresh_token,
            expires_in: tokens.expires_at.map(|at| {
                let now = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
                at.saturating_sub(now)
            }),
            error: None,
        }),
        None => Ok(CopilotDevicePollResponse {
            access_token: None,
            refresh_token: None,
            expires_in: None,
            error: Some("expired_token".to_string()),
        }),
    }
}
