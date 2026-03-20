//! OAuth callback server.
//!
//! Starts a one-shot HTTP server on localhost to catch the OAuth redirect
//! after the user authorizes in their browser.

use std::collections::HashMap;

use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::TcpListener;
use tokio::time::{timeout, Duration};

use crate::AuthError;

/// Result from an OAuth callback.
#[derive(Debug, Clone)]
pub struct OAuthCallback {
    pub code: String,
    pub state: String,
}

const SUCCESS_HTML: &str = r#"<!DOCTYPE html>
<html><head><title>Authorization Complete</title>
<style>body{font-family:system-ui;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;background:#0a0a0f;color:#e4e4e7}
.card{text-align:center;padding:2rem}h1{font-size:1.25rem;margin-bottom:0.5rem}p{color:#71717a;font-size:0.875rem}</style>
</head><body><div class="card"><h1>Authorization successful</h1><p>You can close this tab and return to AVA.</p></div></body></html>"#;

/// Start a one-shot HTTP server on localhost to catch the OAuth redirect.
///
/// Binds to `127.0.0.1:{port}`, accepts a single GET request matching `{path}?code=X&state=Y`,
/// returns a success HTML page to the browser, and returns the authorization code and state.
///
/// Times out after `timeout_secs` seconds.
pub async fn listen_for_callback(
    port: u16,
    path: &str,
    timeout_secs: u64,
) -> Result<OAuthCallback, AuthError> {
    let listener = TcpListener::bind(format!("127.0.0.1:{port}"))
        .await
        .map_err(|e| AuthError::Other(format!("Bind error on port {port}: {e}")))?;

    let (mut stream, _) = timeout(Duration::from_secs(timeout_secs), listener.accept())
        .await
        .map_err(|_| AuthError::CallbackTimeout)?
        .map_err(|e| AuthError::Other(format!("Accept error: {e}")))?;

    let (reader_half, mut writer_half) = stream.split();
    let reader = BufReader::new(reader_half);
    let mut lines = reader.lines();

    let request_line = lines
        .next_line()
        .await
        .map_err(|e| AuthError::Other(format!("Read error: {e}")))?
        .ok_or_else(|| AuthError::Other("No request received".to_string()))?;

    // Parse "GET /auth/callback?code=X&state=Y HTTP/1.1"
    let request_path = request_line
        .split_whitespace()
        .nth(1)
        .ok_or_else(|| AuthError::Other("Invalid HTTP request".to_string()))?
        .to_string();

    // Validate path prefix matches expected
    let (actual_path, query) = request_path
        .split_once('?')
        .ok_or_else(|| AuthError::Other("No query parameters in callback".to_string()))?;

    if !path.is_empty() && actual_path != path {
        return Err(AuthError::Other(format!(
            "Unexpected callback path: {actual_path} (expected {path})"
        )));
    }

    let params: HashMap<String, String> = url::form_urlencoded::parse(query.as_bytes())
        .into_owned()
        .collect();

    // Check for OAuth error response (e.g., denied scopes, user cancelled)
    if let Some(error) = params.get("error") {
        let desc = params.get("error_description").cloned().unwrap_or_default();
        return Err(AuthError::Other(format!(
            "OAuth error: {error}{}",
            if desc.is_empty() {
                String::new()
            } else {
                format!(" — {desc}")
            }
        )));
    }

    let code = params
        .get("code")
        .ok_or_else(|| {
            let all_params: Vec<String> = params.keys().cloned().collect();
            AuthError::Other(format!(
                "Missing 'code' parameter in callback (got: {})",
                all_params.join(", ")
            ))
        })?
        .clone();
    let state = params
        .get("state")
        .ok_or_else(|| AuthError::Other("Missing 'state' parameter in callback".to_string()))?
        .clone();

    // Drain remaining HTTP headers before sending response
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

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn callback_server_receives_code_and_state() {
        // Bind to port 0 to get a random available port
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        drop(listener);

        let server = tokio::spawn(async move { listen_for_callback(port, "/callback", 5).await });

        // Give server a moment to bind
        tokio::time::sleep(Duration::from_millis(50)).await;

        // Simulate browser redirect
        let client = reqwest::Client::new();
        let _ = client
            .get(format!(
                "http://127.0.0.1:{port}/callback?code=test_code&state=test_state"
            ))
            .send()
            .await;

        let result = server.await.unwrap().unwrap();
        assert_eq!(result.code, "test_code");
        assert_eq!(result.state, "test_state");
    }

    #[tokio::test]
    async fn callback_server_times_out() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        drop(listener);

        let result = listen_for_callback(port, "/callback", 1).await;
        assert!(matches!(result, Err(AuthError::CallbackTimeout)));
    }
}
