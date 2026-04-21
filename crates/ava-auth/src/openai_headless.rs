//! OpenAI headless ChatGPT subscription authentication.
//!
//! This mirrors OpenAI's device-style ChatGPT login used by OpenCode: request
//! a user code, poll for an authorization code + verifier, then exchange that
//! pair for OAuth tokens.

use serde::Deserialize;
use tokio::time::{sleep, Duration};

use crate::device_code::DeviceCodeResponse;
use crate::tokens::OAuthTokens;
use crate::{http_client, AuthError};

const USER_CODE_URL: &str = "https://auth.openai.com/api/accounts/deviceauth/usercode";
const DEVICE_TOKEN_URL: &str = "https://auth.openai.com/api/accounts/deviceauth/token";
const TOKEN_URL: &str = "https://auth.openai.com/oauth/token";
const DEVICE_CALLBACK_REDIRECT_URI: &str = "https://auth.openai.com/deviceauth/callback";
const VERIFICATION_URI: &str = "https://auth.openai.com/codex/device";
const POLL_SAFETY_MARGIN_SECS: u64 = 3;
const HEADLESS_TIMEOUT_SECS: u64 = 300;

#[derive(Debug, Deserialize)]
struct UserCodeResponse {
    device_auth_id: String,
    user_code: String,
    interval: Option<String>,
}

#[derive(Debug, Deserialize)]
struct PollResponse {
    authorization_code: String,
    code_verifier: String,
}

#[derive(Debug, Deserialize)]
struct TokenResponse {
    access_token: String,
    refresh_token: Option<String>,
    expires_in: Option<u64>,
    id_token: Option<String>,
}

pub async fn request_code(client_id: &str) -> Result<DeviceCodeResponse, AuthError> {
    let client = http_client()?;
    let response = client
        .post(USER_CODE_URL)
        .header("Content-Type", "application/json")
        .header("User-Agent", format!("ava/{}", env!("CARGO_PKG_VERSION")))
        .json(&serde_json::json!({ "client_id": client_id }))
        .send()
        .await
        .map_err(|e| AuthError::Network(format!("Headless auth start failed: {e}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        return Err(AuthError::Other(format!(
            "Headless auth start failed ({status}): {}",
            &body[..body.len().min(500)]
        )));
    }

    let raw: UserCodeResponse = response.json().await.map_err(|e| {
        AuthError::Other(format!("Failed to parse headless auth start response: {e}"))
    })?;

    let interval = raw
        .interval
        .as_deref()
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(5)
        .max(1);

    Ok(DeviceCodeResponse {
        device_code: raw.device_auth_id,
        user_code: raw.user_code,
        verification_uri: VERIFICATION_URI.to_string(),
        expires_in: HEADLESS_TIMEOUT_SECS,
        interval,
    })
}

pub async fn poll_code(
    client_id: &str,
    device_auth_id: &str,
    user_code: &str,
    interval: u64,
) -> Result<Option<OAuthTokens>, AuthError> {
    let client = http_client()?;
    let sleep_for = Duration::from_secs(interval.max(1) + POLL_SAFETY_MARGIN_SECS);
    let deadline = tokio::time::Instant::now() + Duration::from_secs(HEADLESS_TIMEOUT_SECS);

    loop {
        if tokio::time::Instant::now() >= deadline {
            return Ok(None);
        }

        let response = client
            .post(DEVICE_TOKEN_URL)
            .header("Content-Type", "application/json")
            .header("User-Agent", format!("ava/{}", env!("CARGO_PKG_VERSION")))
            .json(&serde_json::json!({
                "device_auth_id": device_auth_id,
                "user_code": user_code,
            }))
            .send()
            .await
            .map_err(|e| AuthError::Network(format!("Headless auth poll failed: {e}")))?;

        if response.status().is_success() {
            let raw: PollResponse = response.json().await.map_err(|e| {
                AuthError::Other(format!("Failed to parse headless auth poll response: {e}"))
            })?;

            let token_response = client
                .post(TOKEN_URL)
                .header("Content-Type", "application/x-www-form-urlencoded")
                .body(format!(
                    "grant_type=authorization_code&code={}&redirect_uri={}&client_id={}&code_verifier={}",
                    urlencoding::encode(&raw.authorization_code),
                    urlencoding::encode(DEVICE_CALLBACK_REDIRECT_URI),
                    urlencoding::encode(client_id),
                    urlencoding::encode(&raw.code_verifier),
                ))
                .send()
                .await
                .map_err(|e| AuthError::TokenExchange(format!("Request failed: {e}")))?;

            if !token_response.status().is_success() {
                let status = token_response.status();
                let body = token_response.text().await.unwrap_or_default();
                return Err(AuthError::TokenExchange(format!(
                    "Token endpoint returned {status}: {}",
                    &body[..body.len().min(500)]
                )));
            }

            let raw: TokenResponse = token_response.json().await.map_err(|e| {
                AuthError::TokenExchange(format!("Failed to parse token response: {e}"))
            })?;

            let now_secs = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();

            return Ok(Some(OAuthTokens {
                access_token: raw.access_token,
                refresh_token: raw.refresh_token,
                expires_at: raw.expires_in.map(|expires_in| now_secs + expires_in),
                id_token: raw.id_token,
            }));
        }

        let status = response.status();
        if status != reqwest::StatusCode::FORBIDDEN && status != reqwest::StatusCode::NOT_FOUND {
            let body = response.text().await.unwrap_or_default();
            return Err(AuthError::Other(format!(
                "Headless auth poll failed ({status}): {}",
                &body[..body.len().min(500)]
            )));
        }

        sleep(sleep_for).await;
    }
}
