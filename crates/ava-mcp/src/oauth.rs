//! OAuth 2.0 PKCE flow for remote MCP servers.
//!
//! Handles token acquisition, storage, refresh, and the 401-triggered re-auth
//! cycle. Tokens are persisted to `~/.ava/credentials.json` under the key
//! `mcp:{server_name}`.
//!
//! # Flow
//!
//! 1. Check credential store for an existing (non-expired) token.
//! 2. If missing or expired, run PKCE authorization code flow:
//!    a. Generate PKCE verifier + challenge + random state.
//!    b. Open browser to `auth_url?...&code_challenge=...`.
//!    c. Listen on `localhost:{redirect_port}/callback` for the redirect.
//!    d. Exchange code for tokens via POST to `token_url`.
//!    e. Store tokens in credential store.
//! 3. Return the `access_token` for use in `Authorization: Bearer` header.

use std::collections::HashMap;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use ava_types::{AvaError, Result};
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use tokio::net::TcpListener;
use tracing::{debug, info, warn};

use crate::config::McpOAuthConfig;

// ---------------------------------------------------------------------------
// Stored token structure
// ---------------------------------------------------------------------------

/// OAuth tokens stored for an MCP server.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct McpTokens {
    pub access_token: String,
    pub refresh_token: Option<String>,
    /// Unix timestamp (seconds) after which the access token is considered
    /// expired. `None` means no expiry information was provided by the server.
    pub expires_at: Option<u64>,
    /// The token URL used to obtain this token — used for refresh.
    pub token_url: String,
    /// Client ID used — needed for refresh requests.
    pub client_id: String,
}

impl McpTokens {
    /// Returns `true` if the access token is (probably) still valid.
    ///
    /// Tokens are considered expired 60 seconds before `expires_at` to avoid
    /// using a token that's just about to expire at the server.
    pub fn is_valid(&self) -> bool {
        match self.expires_at {
            None => true, // No expiry info — assume valid
            Some(exp) => {
                let now = SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
                now + 60 < exp
            }
        }
    }
}

// ---------------------------------------------------------------------------
// PKCE helpers
// ---------------------------------------------------------------------------

struct PkceParams {
    verifier: String,
    challenge: String,
    state: String,
}

fn generate_pkce() -> PkceParams {
    let verifier_bytes: [u8; 64] = rand::random();
    let verifier = URL_SAFE_NO_PAD.encode(verifier_bytes);

    let digest = Sha256::digest(verifier.as_bytes());
    let challenge = URL_SAFE_NO_PAD.encode(digest);

    let state_bytes: [u8; 32] = rand::random();
    let state = URL_SAFE_NO_PAD.encode(state_bytes);

    PkceParams {
        verifier,
        challenge,
        state,
    }
}

fn build_auth_url(config: &McpOAuthConfig, pkce: &PkceParams, redirect_port: u16) -> String {
    let redirect_uri = format!("http://localhost:{}/callback", redirect_port);
    let scope = config.scopes.join(" ");

    let mut params = vec![
        ("response_type", "code".to_string()),
        ("client_id", config.client_id.clone()),
        ("redirect_uri", redirect_uri),
        ("code_challenge", pkce.challenge.clone()),
        ("code_challenge_method", "S256".to_string()),
        ("state", pkce.state.clone()),
    ];
    if !scope.is_empty() {
        params.push(("scope", scope));
    }

    let query: String = params
        .iter()
        .map(|(k, v)| format!("{}={}", k, urlencoding::encode(v)))
        .collect::<Vec<_>>()
        .join("&");

    format!("{}?{}", config.auth_url, query)
}

// ---------------------------------------------------------------------------
// Local callback listener
// ---------------------------------------------------------------------------

/// Result of the OAuth redirect callback.
struct OAuthCallback {
    code: String,
    state: String,
}

/// Listen on `localhost:{port}/callback` for the OAuth redirect, with a
/// `timeout` deadline.
async fn listen_for_callback(port: u16, timeout: Duration) -> Result<OAuthCallback> {
    let listener = TcpListener::bind(format!("127.0.0.1:{port}"))
        .await
        .map_err(|e| {
            AvaError::ToolError(format!(
                "MCP OAuth: failed to bind callback port {port}: {e}"
            ))
        })?;

    let accept_fut = async {
        let (stream, _addr) = listener
            .accept()
            .await
            .map_err(|e| AvaError::ToolError(format!("MCP OAuth: callback accept error: {e}")))?;

        // Read the HTTP request line (GET /callback?code=...&state=... HTTP/1.1)
        let mut reader = tokio::io::BufReader::new(stream);
        let mut request_line = String::new();
        tokio::io::AsyncBufReadExt::read_line(&mut reader, &mut request_line)
            .await
            .map_err(|e| AvaError::ToolError(format!("MCP OAuth: callback read error: {e}")))?;

        // Send 200 OK response so the browser doesn't show an error
        let response_body = b"<html><body><h2>Authentication successful - you can close this window.</h2></body></html>";
        let response = format!(
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
            response_body.len()
        );
        let mut writer = reader.into_inner();
        use tokio::io::AsyncWriteExt;
        let _ = writer.write_all(response.as_bytes()).await;
        let _ = writer.write_all(response_body).await;

        // Parse GET /callback?code=...&state=... HTTP/1.1
        let path = request_line
            .split_whitespace()
            .nth(1)
            .unwrap_or("/callback");

        let query = path.split('?').nth(1).unwrap_or("");
        let params: HashMap<String, String> = query
            .split('&')
            .filter_map(|pair| {
                let mut parts = pair.splitn(2, '=');
                let k = parts.next()?.to_string();
                let v = urlencoding::decode(parts.next().unwrap_or(""))
                    .map(|s| s.into_owned())
                    .unwrap_or_default();
                Some((k, v))
            })
            .collect();

        let code = params.get("code").cloned().ok_or_else(|| {
            AvaError::ToolError("MCP OAuth: callback missing 'code' parameter".to_string())
        })?;
        let state = params.get("state").cloned().ok_or_else(|| {
            AvaError::ToolError("MCP OAuth: callback missing 'state' parameter".to_string())
        })?;

        Ok::<OAuthCallback, AvaError>(OAuthCallback { code, state })
    };

    tokio::time::timeout(timeout, accept_fut)
        .await
        .map_err(|_| {
            AvaError::ToolError("MCP OAuth: callback timed out after 2 minutes".to_string())
        })?
}

// ---------------------------------------------------------------------------
// Token exchange
// ---------------------------------------------------------------------------

#[derive(Deserialize)]
struct TokenResponse {
    access_token: String,
    refresh_token: Option<String>,
    expires_in: Option<u64>,
}

async fn exchange_code_for_tokens(
    config: &McpOAuthConfig,
    code: &str,
    pkce: &PkceParams,
    redirect_port: u16,
    http_client: &reqwest::Client,
) -> Result<McpTokens> {
    let redirect_uri = format!("http://localhost:{}/callback", redirect_port);

    let body = format!(
        "grant_type=authorization_code&code={}&redirect_uri={}&client_id={}&code_verifier={}",
        urlencoding::encode(code),
        urlencoding::encode(&redirect_uri),
        urlencoding::encode(&config.client_id),
        urlencoding::encode(&pkce.verifier),
    );

    let response = http_client
        .post(&config.token_url)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .body(body)
        .send()
        .await
        .map_err(|e| AvaError::ToolError(format!("MCP OAuth token exchange failed: {e}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let text = response.text().await.unwrap_or_default();
        return Err(AvaError::ToolError(format!(
            "MCP OAuth token endpoint returned {status}: {}",
            &text[..text.len().min(300)]
        )));
    }

    let raw: TokenResponse = response
        .json()
        .await
        .map_err(|e| AvaError::SerializationError(format!("MCP OAuth token parse failed: {e}")))?;

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    Ok(McpTokens {
        access_token: raw.access_token,
        refresh_token: raw.refresh_token,
        expires_at: raw.expires_in.map(|ei| now + ei),
        token_url: config.token_url.clone(),
        client_id: config.client_id.clone(),
    })
}

async fn refresh_access_token(
    tokens: &McpTokens,
    http_client: &reqwest::Client,
) -> Result<McpTokens> {
    let refresh_tok = tokens.refresh_token.as_deref().ok_or_else(|| {
        AvaError::ToolError(
            "MCP OAuth: no refresh token available — re-authorize required".to_string(),
        )
    })?;

    let body = format!(
        "grant_type=refresh_token&client_id={}&refresh_token={}",
        urlencoding::encode(&tokens.client_id),
        urlencoding::encode(refresh_tok),
    );

    let response = http_client
        .post(&tokens.token_url)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .body(body)
        .send()
        .await
        .map_err(|e| AvaError::ToolError(format!("MCP OAuth token refresh failed: {e}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let text = response.text().await.unwrap_or_default();
        return Err(AvaError::ToolError(format!(
            "MCP OAuth refresh endpoint returned {status}: {}",
            &text[..text.len().min(300)]
        )));
    }

    let raw: TokenResponse = response.json().await.map_err(|e| {
        AvaError::SerializationError(format!("MCP OAuth refresh parse failed: {e}"))
    })?;

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    Ok(McpTokens {
        access_token: raw.access_token,
        refresh_token: raw.refresh_token.or_else(|| tokens.refresh_token.clone()),
        expires_at: raw.expires_in.map(|ei| now + ei),
        token_url: tokens.token_url.clone(),
        client_id: tokens.client_id.clone(),
    })
}

// ---------------------------------------------------------------------------
// Credential store helpers
// ---------------------------------------------------------------------------

/// Key used in `~/.ava/credentials.json` for MCP server tokens.
fn cred_key(server_name: &str) -> String {
    format!("mcp:{server_name}")
}

/// Load stored tokens from `~/.ava/credentials.json`.
///
/// Returns `None` if no tokens are stored for this server, the file doesn't
/// exist, or the stored JSON is unreadable.
pub fn load_stored_tokens(server_name: &str) -> Option<McpTokens> {
    let path = dirs::home_dir()?.join(".ava").join("credentials.json");
    let text = std::fs::read_to_string(&path).ok()?;
    let obj: serde_json::Value = serde_json::from_str(&text).ok()?;
    let key = cred_key(server_name);
    let tokens_val = obj.get("mcp_tokens")?.get(&key)?;
    serde_json::from_value(tokens_val.clone()).ok()
}

/// Persist tokens to `~/.ava/credentials.json` under `mcp_tokens.{key}`.
pub fn store_tokens(server_name: &str, tokens: &McpTokens) -> Result<()> {
    let home = dirs::home_dir()
        .ok_or_else(|| AvaError::ToolError("MCP OAuth: cannot find home directory".to_string()))?;
    let ava_dir = home.join(".ava");
    std::fs::create_dir_all(&ava_dir)
        .map_err(|e| AvaError::IoError(format!("MCP OAuth: failed to create ~/.ava: {e}")))?;

    let path = ava_dir.join("credentials.json");
    let mut obj: serde_json::Value = if path.exists() {
        let text = std::fs::read_to_string(&path).map_err(|e| {
            AvaError::IoError(format!("MCP OAuth: failed to read credentials.json: {e}"))
        })?;
        serde_json::from_str(&text).unwrap_or_else(|_| serde_json::json!({}))
    } else {
        serde_json::json!({})
    };

    let mcp_tokens = obj
        .as_object_mut()
        .ok_or_else(|| {
            AvaError::SerializationError("credentials.json is not an object".to_string())
        })?
        .entry("mcp_tokens")
        .or_insert_with(|| serde_json::json!({}));

    let key = cred_key(server_name);
    mcp_tokens[&key] =
        serde_json::to_value(tokens).map_err(|e| AvaError::SerializationError(e.to_string()))?;

    let json = serde_json::to_string_pretty(&obj)
        .map_err(|e| AvaError::SerializationError(e.to_string()))?;
    std::fs::write(&path, json).map_err(|e| {
        AvaError::IoError(format!("MCP OAuth: failed to write credentials.json: {e}"))
    })?;

    info!("MCP OAuth: stored tokens for server '{server_name}'");
    Ok(())
}

// ---------------------------------------------------------------------------
// McpOAuthManager — high-level entry point
// ---------------------------------------------------------------------------

/// Manages OAuth token lifecycle for a remote MCP server.
///
/// Use `get_access_token()` to obtain a valid Bearer token before each request.
/// If the token is expired or absent, this will trigger a PKCE browser flow.
pub struct McpOAuthManager {
    server_name: String,
    oauth_config: McpOAuthConfig,
    cached_tokens: Option<McpTokens>,
    http_client: reqwest::Client,
}

impl McpOAuthManager {
    pub fn new(server_name: impl Into<String>, oauth_config: McpOAuthConfig) -> Self {
        let http_client = reqwest::Client::builder()
            .timeout(Duration::from_secs(30))
            .build()
            .expect("failed to build OAuth HTTP client");

        let server_name = server_name.into();
        let cached_tokens = load_stored_tokens(&server_name);

        Self {
            server_name,
            oauth_config,
            cached_tokens,
            http_client,
        }
    }

    /// Get a valid access token, running the OAuth flow if needed.
    ///
    /// This is the primary entry point. Call before each HTTP request to the
    /// MCP server. If the token is still valid, returns immediately.
    /// If expired, attempts refresh first. If refresh fails (or no refresh token),
    /// runs the full PKCE browser flow.
    pub async fn get_access_token(&mut self) -> Result<String> {
        // 1. Check in-memory cache
        if let Some(ref tokens) = self.cached_tokens {
            if tokens.is_valid() {
                return Ok(tokens.access_token.clone());
            }
            // Try refresh
            match refresh_access_token(tokens, &self.http_client).await {
                Ok(new_tokens) => {
                    info!("MCP OAuth: refreshed token for '{}'", self.server_name);
                    let _ = store_tokens(&self.server_name, &new_tokens);
                    self.cached_tokens = Some(new_tokens.clone());
                    return Ok(new_tokens.access_token);
                }
                Err(e) => {
                    debug!(
                        "MCP OAuth: refresh failed for '{}': {e} — running full flow",
                        self.server_name
                    );
                    // Fall through to full PKCE flow
                }
            }
        }

        // 2. Full PKCE authorization code flow
        let tokens = self.run_pkce_flow().await?;
        let _ = store_tokens(&self.server_name, &tokens);
        let access_token = tokens.access_token.clone();
        self.cached_tokens = Some(tokens);
        Ok(access_token)
    }

    /// Force-expire the cached token and get a fresh one on the next call.
    /// Called when the server returns 401 to handle race conditions.
    pub fn invalidate_token(&mut self) {
        self.cached_tokens = None;
    }

    async fn run_pkce_flow(&self) -> Result<McpTokens> {
        let port = self.oauth_config.redirect_port;
        let pkce = generate_pkce();
        let auth_url = build_auth_url(&self.oauth_config, &pkce, port);

        info!(
            "MCP OAuth: opening browser for server '{}' — {}",
            self.server_name, auth_url
        );

        // Best-effort browser open; fall back to printing the URL if it fails
        if let Err(e) = open::that(&auth_url) {
            warn!("MCP OAuth: could not open browser: {e}");
            eprintln!(
                "\nMCP OAuth: open this URL to authenticate with '{}':\n  {}\n",
                self.server_name, auth_url
            );
        }

        let callback = listen_for_callback(port, Duration::from_secs(120)).await?;

        // Validate CSRF state
        if callback.state != pkce.state {
            return Err(AvaError::ToolError(
                "MCP OAuth: CSRF state mismatch — possible attack, aborting".to_string(),
            ));
        }

        let tokens = exchange_code_for_tokens(
            &self.oauth_config,
            &callback.code,
            &pkce,
            port,
            &self.http_client,
        )
        .await?;

        info!(
            "MCP OAuth: authorization successful for '{}'",
            self.server_name
        );
        Ok(tokens)
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pkce_generates_valid_lengths() {
        let p = generate_pkce();
        assert_eq!(p.verifier.len(), 86);
        assert_eq!(p.challenge.len(), 43);
        assert_eq!(p.state.len(), 43);
    }

    #[test]
    fn pkce_challenge_matches_verifier() {
        let p = generate_pkce();
        let digest = Sha256::digest(p.verifier.as_bytes());
        let expected = URL_SAFE_NO_PAD.encode(digest);
        assert_eq!(p.challenge, expected);
    }

    #[test]
    fn pkce_uses_url_safe_chars() {
        let p = generate_pkce();
        let safe = |s: &str| {
            s.chars()
                .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
        };
        assert!(safe(&p.verifier));
        assert!(safe(&p.challenge));
        assert!(safe(&p.state));
    }

    #[test]
    fn build_auth_url_contains_required_params() {
        let config = McpOAuthConfig {
            auth_url: "https://example.com/authorize".to_string(),
            token_url: "https://example.com/token".to_string(),
            client_id: "test-client".to_string(),
            scopes: vec!["read".to_string(), "write".to_string()],
            redirect_port: 9876,
        };
        let pkce = generate_pkce();
        let url = build_auth_url(&config, &pkce, 9876);

        assert!(url.starts_with("https://example.com/authorize?"));
        assert!(url.contains("response_type=code"));
        assert!(url.contains("client_id=test-client"));
        assert!(url.contains("code_challenge_method=S256"));
        assert!(url.contains(&format!("state={}", pkce.state)));
        assert!(url.contains(&format!("code_challenge={}", pkce.challenge)));
        assert!(url.contains("redirect_uri="));
    }

    #[test]
    fn mcp_tokens_validity() {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        let valid = McpTokens {
            access_token: "tok".to_string(),
            refresh_token: None,
            expires_at: Some(now + 3600),
            token_url: "https://example.com/token".to_string(),
            client_id: "client".to_string(),
        };
        assert!(valid.is_valid());

        let expired = McpTokens {
            access_token: "tok".to_string(),
            refresh_token: None,
            expires_at: Some(now - 1),
            token_url: "https://example.com/token".to_string(),
            client_id: "client".to_string(),
        };
        assert!(!expired.is_valid());

        let no_expiry = McpTokens {
            access_token: "tok".to_string(),
            refresh_token: None,
            expires_at: None,
            token_url: "https://example.com/token".to_string(),
            client_id: "client".to_string(),
        };
        assert!(no_expiry.is_valid());
    }

    #[test]
    fn cred_key_format() {
        assert_eq!(cred_key("slack"), "mcp:slack");
        assert_eq!(cred_key("linear"), "mcp:linear");
    }

    #[test]
    fn store_and_load_tokens() {
        let dir = tempfile::tempdir().unwrap();
        // We can't easily override the home dir, so test the JSON logic directly.
        let tokens = McpTokens {
            access_token: "access-tok".to_string(),
            refresh_token: Some("refresh-tok".to_string()),
            expires_at: Some(9999999999),
            token_url: "https://example.com/token".to_string(),
            client_id: "my-client".to_string(),
        };

        // Simulate what store_tokens writes into a JSON object
        let mut obj = serde_json::json!({});
        let key = cred_key("test-server");
        obj["mcp_tokens"] = serde_json::json!({});
        obj["mcp_tokens"][&key] = serde_json::to_value(&tokens).unwrap();

        let json_path = dir.path().join("credentials.json");
        std::fs::write(&json_path, serde_json::to_string_pretty(&obj).unwrap()).unwrap();

        // Now simulate what load_stored_tokens reads
        let text = std::fs::read_to_string(&json_path).unwrap();
        let loaded_obj: serde_json::Value = serde_json::from_str(&text).unwrap();
        let loaded: McpTokens =
            serde_json::from_value(loaded_obj["mcp_tokens"][&key].clone()).unwrap();

        assert_eq!(loaded.access_token, "access-tok");
        assert_eq!(loaded.refresh_token.as_deref(), Some("refresh-tok"));
        assert_eq!(loaded.client_id, "my-client");
        assert!(loaded.is_valid());
    }
}
