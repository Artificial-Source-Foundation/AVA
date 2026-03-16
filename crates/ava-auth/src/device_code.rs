//! Device code authentication flow.
//!
//! Used for providers like GitHub Copilot where the user enters a code
//! on a website to authorize the application.

use serde::Deserialize;
use tokio::time::{sleep, Duration, Instant};

use crate::config::OAuthConfig;
use crate::tokens::OAuthTokens;
use crate::{http_client, AuthError};

/// Response from requesting a device code.
#[derive(Debug, Clone)]
pub struct DeviceCodeResponse {
    /// Opaque device code for polling.
    pub device_code: String,
    /// Code the user must enter on the verification page.
    pub user_code: String,
    /// URL where the user enters the code.
    pub verification_uri: String,
    /// Seconds until the device code expires.
    pub expires_in: u64,
    /// Polling interval in seconds.
    pub interval: u64,
}

#[derive(Deserialize)]
struct RawDeviceCodeResponse {
    device_code: String,
    user_code: String,
    verification_uri: String,
    expires_in: u64,
    interval: Option<u64>,
}

#[derive(Deserialize)]
struct RawPollResponse {
    access_token: Option<String>,
    refresh_token: Option<String>,
    expires_in: Option<u64>,
    error: Option<String>,
}

/// Request a device code from the provider.
pub async fn request_device_code(config: &OAuthConfig) -> Result<DeviceCodeResponse, AuthError> {
    let client = http_client()?;
    let scope = config.scopes.join(" ");

    let response = client
        .post(config.authorization_url)
        .header("Accept", "application/json")
        .form(&[("client_id", config.client_id), ("scope", &scope)])
        .send()
        .await
        .map_err(|e| AuthError::Network(format!("Device code request failed: {e}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        return Err(AuthError::Other(format!(
            "Device code request failed ({status}): {}",
            &body[..body.len().min(500)]
        )));
    }

    let raw: RawDeviceCodeResponse = response
        .json()
        .await
        .map_err(|e| AuthError::Other(format!("Failed to parse device code response: {e}")))?;

    Ok(DeviceCodeResponse {
        device_code: raw.device_code,
        user_code: raw.user_code,
        verification_uri: raw.verification_uri,
        expires_in: raw.expires_in,
        interval: raw.interval.unwrap_or(5),
    })
}

/// Maximum time to wait for device code authorization (5 minutes).
const DEVICE_CODE_POLL_TIMEOUT_SECS: u64 = 300;

/// Poll for authorization after the user enters the device code.
///
/// Returns `Ok(Some(tokens))` on success, `Ok(None)` if the code expired,
/// or `Err` on unrecoverable errors. Enforces a 5-minute overall timeout
/// independent of the server-reported expiry.
pub async fn poll_device_code(
    config: &OAuthConfig,
    device_code: &str,
    interval: u64,
    expires_in: u64,
) -> Result<Option<OAuthTokens>, AuthError> {
    let timeout_secs = expires_in.min(DEVICE_CODE_POLL_TIMEOUT_SECS);
    tokio::time::timeout(
        Duration::from_secs(timeout_secs),
        poll_device_code_inner(config, device_code, interval, expires_in),
    )
    .await
    .unwrap_or(Ok(None))
}

/// Inner polling loop for device code authorization.
async fn poll_device_code_inner(
    config: &OAuthConfig,
    device_code: &str,
    interval: u64,
    expires_in: u64,
) -> Result<Option<OAuthTokens>, AuthError> {
    let client = http_client()?;
    let start = Instant::now();
    let max_wait = Duration::from_secs(expires_in);
    let mut current_interval = interval;

    loop {
        sleep(Duration::from_secs(current_interval)).await;

        if start.elapsed() >= max_wait {
            return Ok(None);
        }

        let response = client
            .post(config.token_url)
            .header("Accept", "application/json")
            .form(&[
                ("client_id", config.client_id),
                ("device_code", device_code),
                ("grant_type", "urn:ietf:params:oauth:grant-type:device_code"),
            ])
            .send()
            .await
            .map_err(|e| AuthError::Network(format!("Device code poll failed: {e}")))?;

        if !response.status().is_success() {
            let status = response.status();
            let body = response.text().await.unwrap_or_default();
            return Err(AuthError::Other(format!(
                "Device code poll failed ({status}): {}",
                &body[..body.len().min(500)]
            )));
        }

        let raw: RawPollResponse = response
            .json()
            .await
            .map_err(|e| AuthError::Other(format!("Failed to parse poll response: {e}")))?;

        if let Some(access_token) = raw.access_token {
            let now_secs = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();

            return Ok(Some(OAuthTokens {
                access_token,
                refresh_token: raw.refresh_token,
                expires_at: raw.expires_in.map(|ei| now_secs + ei),
                id_token: None,
            }));
        }

        match raw.error.as_deref() {
            Some("authorization_pending") => continue,
            Some("slow_down") => {
                current_interval += 5;
                continue;
            }
            Some("expired_token") => return Ok(None),
            Some(other) => {
                return Err(AuthError::Other(format!("Device code error: {other}")));
            }
            None => continue,
        }
    }
}
