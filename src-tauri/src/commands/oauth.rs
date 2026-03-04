use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::TcpListener;
use tokio::time::{timeout, Duration};

#[derive(Serialize)]
pub struct OAuthCallback {
    pub code: String,
    pub state: String,
}

const SUCCESS_HTML: &str = r#"<!DOCTYPE html>
<html><head><title>Authorization Complete</title>
<style>body{font-family:system-ui;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;background:#0a0a0f;color:#e4e4e7}
.card{text-align:center;padding:2rem}h1{font-size:1.25rem;margin-bottom:0.5rem}p{color:#71717a;font-size:0.875rem}</style>
</head><body><div class="card"><h1>Authorization successful</h1><p>You can close this tab and return to AVA.</p></div></body></html>"#;

const TIMEOUT_SECS: u64 = 120;

#[derive(Debug, Serialize, Deserialize)]
pub struct CopilotDeviceCodeResponse {
    pub device_code: String,
    pub user_code: String,
    pub verification_uri: String,
    pub expires_in: u64,
    pub interval: Option<u64>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CopilotDevicePollResponse {
    pub access_token: Option<String>,
    pub refresh_token: Option<String>,
    pub expires_in: Option<u64>,
    pub error: Option<String>,
}

/// Start a one-shot HTTP server on the given port to catch an OAuth callback.
/// Returns the `code` and `state` query params from the redirect.
#[tauri::command]
pub async fn oauth_listen(port: u16) -> Result<OAuthCallback, String> {
    let listener = TcpListener::bind(format!("127.0.0.1:{port}"))
        .await
        .map_err(|e| format!("Bind error on port {port}: {e}"))?;

    // Wait for a single connection with timeout
    let (mut stream, _) = timeout(Duration::from_secs(TIMEOUT_SECS), listener.accept())
        .await
        .map_err(|_| "OAuth callback timed out. Please try again.".to_string())?
        .map_err(|e| format!("Accept error: {e}"))?;

    // Read the HTTP request line
    let (reader_half, mut writer_half) = stream.split();
    let reader = BufReader::new(reader_half);
    let mut lines = reader.lines();

    let request_line = lines
        .next_line()
        .await
        .map_err(|e| format!("Read error: {e}"))?
        .ok_or("No request received")?;

    // Parse query params from "GET /callback?code=X&state=Y HTTP/1.1"
    let path = request_line
        .split_whitespace()
        .nth(1)
        .ok_or("Invalid HTTP request")?
        .to_string();

    let query = path
        .split('?')
        .nth(1)
        .ok_or("No query parameters in callback")?;

    let params: HashMap<String, String> = query
        .split('&')
        .filter_map(|pair| {
            let mut parts = pair.splitn(2, '=');
            Some((parts.next()?.to_string(), parts.next()?.to_string()))
        })
        .collect();

    let code = params
        .get("code")
        .ok_or("Missing 'code' parameter in callback")?
        .clone();
    let state = params
        .get("state")
        .ok_or("Missing 'state' parameter in callback")?
        .clone();

    // Drain remaining headers before writing response
    loop {
        match lines.next_line().await {
            Ok(Some(line)) if !line.is_empty() => continue,
            _ => break,
        }
    }

    // Send success response
    let response = format!(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        SUCCESS_HTML.len(),
        SUCCESS_HTML
    );
    let _ = writer_half.write_all(response.as_bytes()).await;
    let _ = writer_half.flush().await;

    Ok(OAuthCallback { code, state })
}

#[tauri::command]
pub async fn oauth_copilot_device_start(
    client_id: String,
    scope: String,
) -> Result<CopilotDeviceCodeResponse, String> {
    let client = reqwest::Client::new();
    let response = client
        .post("https://github.com/login/device/code")
        .header("Accept", "application/json")
        .form(&[("client_id", client_id), ("scope", scope)])
        .send()
        .await
        .map_err(|e| format!("Copilot device code request failed: {e}"))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        return Err(format!(
            "Copilot device code request failed ({status}): {body}"
        ));
    }

    response
        .json::<CopilotDeviceCodeResponse>()
        .await
        .map_err(|e| format!("Failed to parse Copilot device code response: {e}"))
}

#[tauri::command]
pub async fn oauth_copilot_device_poll(
    client_id: String,
    device_code: String,
) -> Result<CopilotDevicePollResponse, String> {
    let client = reqwest::Client::new();
    let response = client
        .post("https://github.com/login/oauth/access_token")
        .header("Accept", "application/json")
        .form(&[
            ("client_id", client_id),
            ("device_code", device_code),
            (
                "grant_type",
                "urn:ietf:params:oauth:grant-type:device_code".to_string(),
            ),
        ])
        .send()
        .await
        .map_err(|e| format!("Copilot device poll request failed: {e}"))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        return Err(format!("Copilot device poll failed ({status}): {body}"));
    }

    response
        .json::<CopilotDevicePollResponse>()
        .await
        .map_err(|e| format!("Failed to parse Copilot device poll response: {e}"))
}
