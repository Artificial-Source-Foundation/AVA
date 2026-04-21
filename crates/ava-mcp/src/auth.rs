//! MCP OAuth discovery and enterprise auth support.
//!
//! Provides OAuth 2.0 authorization server discovery via RFC 8414
//! (`.well-known/oauth-authorization-server`), PKCE-based auth flow,
//! token exchange, refresh, and per-server token persistence.

use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};

use ava_types::{AvaError, Result};
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use tracing::{debug, info};

// ---------------------------------------------------------------------------
// OAuth metadata discovered from `.well-known/oauth-authorization-server`
// ---------------------------------------------------------------------------

/// OAuth 2.0 authorization server metadata (RFC 8414 subset).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OAuthMetadata {
    /// Authorization endpoint URL.
    pub authorization_endpoint: String,
    /// Token endpoint URL.
    pub token_endpoint: String,
    /// Supported scopes (empty if not advertised).
    #[serde(default)]
    pub scopes_supported: Vec<String>,
}

// ---------------------------------------------------------------------------
// Token set
// ---------------------------------------------------------------------------

/// Token set returned from an OAuth token exchange or refresh.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TokenSet {
    pub access_token: String,
    pub refresh_token: Option<String>,
    /// Unix timestamp (seconds) after which the access token expires.
    /// `None` if no expiry was provided by the server.
    pub expires_at: Option<u64>,
}

/// Returns `true` if the token set is expired (or will expire within 60s).
pub fn is_expired(token_set: &TokenSet) -> bool {
    match token_set.expires_at {
        None => false, // No expiry info — assume valid
        Some(exp) => {
            let now = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap_or_default()
                .as_secs();
            now + 60 >= exp
        }
    }
}

// ---------------------------------------------------------------------------
// Auth flow state (PKCE)
// ---------------------------------------------------------------------------

/// State for an in-progress PKCE authorization flow.
#[derive(Debug, Clone)]
pub struct AuthFlowState {
    /// The PKCE code verifier (needed for token exchange).
    pub code_verifier: String,
    /// The random state parameter for CSRF protection.
    pub state: String,
    /// The authorization URL the user should visit.
    pub auth_url: String,
    /// Client ID used for this flow.
    pub client_id: String,
    /// Redirect URI used for this flow.
    pub redirect_uri: String,
}

// ---------------------------------------------------------------------------
// McpOAuthProvider
// ---------------------------------------------------------------------------

/// OAuth 2.0 provider for MCP servers.
///
/// Handles discovery, PKCE auth flows, token exchange, and refresh for
/// MCP servers that require OAuth authentication.
pub struct McpOAuthProvider {
    server_name: String,
    client_id: String,
    redirect_port: u16,
}

impl McpOAuthProvider {
    pub fn new(server_name: impl Into<String>, client_id: impl Into<String>) -> Self {
        Self {
            server_name: server_name.into(),
            client_id: client_id.into(),
            redirect_port: 9876,
        }
    }

    /// Set a custom redirect port (default: 9876).
    pub fn with_redirect_port(mut self, port: u16) -> Self {
        self.redirect_port = port;
        self
    }

    /// The server name this provider is for.
    pub fn server_name(&self) -> &str {
        &self.server_name
    }

    /// The client ID used for OAuth.
    pub fn client_id(&self) -> &str {
        &self.client_id
    }

    /// The redirect port for PKCE callback.
    pub fn redirect_port(&self) -> u16 {
        self.redirect_port
    }

    /// Start an OAuth flow using this provider's configuration.
    pub async fn start_flow(&self, meta: &OAuthMetadata, scopes: &[String]) -> AuthFlowState {
        start_auth_flow(meta, &self.client_id, self.redirect_port, scopes).await
    }

    /// Exchange an authorization code using this provider's configuration.
    pub async fn exchange(
        &self,
        code: &str,
        state: &AuthFlowState,
        meta: &OAuthMetadata,
    ) -> Result<TokenSet> {
        exchange_code(code, state, meta).await
    }
}

// ---------------------------------------------------------------------------
// OAuth discovery
// ---------------------------------------------------------------------------

/// Discover OAuth metadata from a server's well-known endpoint.
///
/// Checks `{url}/.well-known/oauth-authorization-server` for RFC 8414 metadata.
/// Returns `None` if the endpoint does not exist or returns invalid data.
pub async fn discover_oauth(url: &str) -> Option<OAuthMetadata> {
    let well_known = format!(
        "{}/.well-known/oauth-authorization-server",
        url.trim_end_matches('/')
    );

    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(10))
        .build()
        .ok()?;

    let response = client.get(&well_known).send().await.ok()?;

    if !response.status().is_success() {
        debug!(
            url = %well_known,
            status = %response.status(),
            "OAuth discovery endpoint not available"
        );
        return None;
    }

    let metadata: OAuthMetadata = response.json().await.ok()?;
    info!(
        url = %url,
        auth_endpoint = %metadata.authorization_endpoint,
        token_endpoint = %metadata.token_endpoint,
        "Discovered OAuth metadata for MCP server"
    );
    Some(metadata)
}

// ---------------------------------------------------------------------------
// PKCE helpers
// ---------------------------------------------------------------------------

fn generate_pkce() -> (String, String, String) {
    let verifier_bytes: [u8; 64] = rand::random();
    let verifier = URL_SAFE_NO_PAD.encode(verifier_bytes);

    let digest = Sha256::digest(verifier.as_bytes());
    let challenge = URL_SAFE_NO_PAD.encode(digest);

    let state_bytes: [u8; 32] = rand::random();
    let state = URL_SAFE_NO_PAD.encode(state_bytes);

    (verifier, challenge, state)
}

// ---------------------------------------------------------------------------
// Auth flow
// ---------------------------------------------------------------------------

/// Start a PKCE authorization flow.
///
/// Generates PKCE parameters and builds the authorization URL. The caller
/// should redirect the user to `AuthFlowState.auth_url` and then call
/// `exchange_code()` with the received authorization code.
pub async fn start_auth_flow(
    meta: &OAuthMetadata,
    client_id: &str,
    redirect_port: u16,
    scopes: &[String],
) -> AuthFlowState {
    let (verifier, challenge, state) = generate_pkce();
    let redirect_uri = format!("http://localhost:{redirect_port}/callback");
    let scope = scopes.join(" ");

    let mut params = vec![
        ("response_type", "code".to_string()),
        ("client_id", client_id.to_string()),
        ("redirect_uri", redirect_uri.clone()),
        ("code_challenge", challenge),
        ("code_challenge_method", "S256".to_string()),
        ("state", state.clone()),
    ];
    if !scope.is_empty() {
        params.push(("scope", scope));
    }

    let query: String = params
        .iter()
        .map(|(k, v)| format!("{}={}", k, urlencoding::encode(v)))
        .collect::<Vec<_>>()
        .join("&");

    let auth_url = format!("{}?{}", meta.authorization_endpoint, query);

    AuthFlowState {
        code_verifier: verifier,
        state,
        auth_url,
        client_id: client_id.to_string(),
        redirect_uri,
    }
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

/// Exchange an authorization code for tokens.
pub async fn exchange_code(
    code: &str,
    state: &AuthFlowState,
    meta: &OAuthMetadata,
) -> Result<TokenSet> {
    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(30))
        .build()
        .map_err(|e| AvaError::ToolError(format!("MCP OAuth: HTTP client error: {e}")))?;

    let body = format!(
        "grant_type=authorization_code&code={}&redirect_uri={}&client_id={}&code_verifier={}",
        urlencoding::encode(code),
        urlencoding::encode(&state.redirect_uri),
        urlencoding::encode(&state.client_id),
        urlencoding::encode(&state.code_verifier),
    );

    let response = client
        .post(&meta.token_endpoint)
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

    Ok(TokenSet {
        access_token: raw.access_token,
        refresh_token: raw.refresh_token,
        expires_at: raw.expires_in.map(|ei| now + ei),
    })
}

/// Refresh an access token using a refresh token.
pub async fn refresh_token(token: &str, meta: &OAuthMetadata) -> Result<TokenSet> {
    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(30))
        .build()
        .map_err(|e| AvaError::ToolError(format!("MCP OAuth: HTTP client error: {e}")))?;

    let body = format!(
        "grant_type=refresh_token&refresh_token={}",
        urlencoding::encode(token),
    );

    let response = client
        .post(&meta.token_endpoint)
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

    Ok(TokenSet {
        access_token: raw.access_token,
        refresh_token: raw.refresh_token,
        expires_at: raw.expires_in.map(|ei| now + ei),
    })
}

// ---------------------------------------------------------------------------
// Token persistence: $XDG_DATA_HOME/ava/mcp-tokens/{server_name}.json
// ---------------------------------------------------------------------------

/// Directory for MCP token storage.
fn mcp_tokens_dir() -> Option<PathBuf> {
    let preferred = dirs::data_dir()?.join("ava").join("mcp-tokens");
    if preferred.exists() {
        return Some(preferred);
    }

    let legacy = dirs::home_dir()?.join(".ava").join("mcp-tokens");
    if legacy.exists() {
        return Some(legacy);
    }

    Some(preferred)
}

/// Load stored tokens for an MCP server.
///
/// Reads from `$XDG_DATA_HOME/ava/mcp-tokens/{server_name}.json`.
/// Returns `None` if no stored tokens exist or the file is unreadable.
pub fn load_mcp_tokens(server_name: &str) -> Option<TokenSet> {
    let path = mcp_tokens_dir()?.join(format!("{server_name}.json"));
    let text = std::fs::read_to_string(path).ok()?;
    serde_json::from_str(&text).ok()
}

/// Store tokens for an MCP server.
///
/// Writes to `$XDG_DATA_HOME/ava/mcp-tokens/{server_name}.json`.
pub fn save_mcp_tokens(server_name: &str, tokens: &TokenSet) -> Result<()> {
    let dir = mcp_tokens_dir().ok_or_else(|| {
        AvaError::IoError("MCP OAuth: cannot determine data directory".to_string())
    })?;

    std::fs::create_dir_all(&dir).map_err(|e| {
        AvaError::IoError(format!(
            "MCP OAuth: failed to create token directory {}: {e}",
            dir.display()
        ))
    })?;

    let path = dir.join(format!("{server_name}.json"));
    let json = serde_json::to_string_pretty(tokens)
        .map_err(|e| AvaError::SerializationError(e.to_string()))?;

    std::fs::write(&path, json).map_err(|e| {
        AvaError::IoError(format!(
            "MCP OAuth: failed to write token file {}: {e}",
            path.display()
        ))
    })?;

    info!(server = %server_name, "MCP OAuth: stored tokens");
    Ok(())
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn token_expiry_detection() {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        let valid = TokenSet {
            access_token: "tok".to_string(),
            refresh_token: None,
            expires_at: Some(now + 3600),
        };
        assert!(!is_expired(&valid));

        let expired = TokenSet {
            access_token: "tok".to_string(),
            refresh_token: None,
            expires_at: Some(now - 1),
        };
        assert!(is_expired(&expired));

        let about_to_expire = TokenSet {
            access_token: "tok".to_string(),
            refresh_token: None,
            expires_at: Some(now + 30), // within 60s window
        };
        assert!(is_expired(&about_to_expire));

        let no_expiry = TokenSet {
            access_token: "tok".to_string(),
            refresh_token: None,
            expires_at: None,
        };
        assert!(!is_expired(&no_expiry));
    }

    #[test]
    fn pkce_challenge_generation() {
        let (verifier, challenge, state) = generate_pkce();

        // Verify lengths (64 bytes -> 86 chars, 32 bytes SHA256 -> 43 chars, 32 bytes -> 43 chars)
        assert_eq!(verifier.len(), 86);
        assert_eq!(challenge.len(), 43);
        assert_eq!(state.len(), 43);

        // Verify challenge matches verifier
        let digest = Sha256::digest(verifier.as_bytes());
        let expected = URL_SAFE_NO_PAD.encode(digest);
        assert_eq!(challenge, expected);

        // Verify URL-safe characters only
        let is_url_safe = |s: &str| {
            s.chars()
                .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
        };
        assert!(is_url_safe(&verifier));
        assert!(is_url_safe(&challenge));
        assert!(is_url_safe(&state));
    }

    #[test]
    fn token_storage_roundtrip() {
        let dir = tempfile::tempdir().unwrap();
        let token_dir = dir.path().join("mcp-tokens");
        std::fs::create_dir_all(&token_dir).unwrap();

        let tokens = TokenSet {
            access_token: "access-123".to_string(),
            refresh_token: Some("refresh-456".to_string()),
            expires_at: Some(9999999999),
        };

        // Write directly (can't override home_dir, so test the JSON logic)
        let path = token_dir.join("test-server.json");
        let json = serde_json::to_string_pretty(&tokens).unwrap();
        std::fs::write(&path, &json).unwrap();

        // Read back
        let text = std::fs::read_to_string(&path).unwrap();
        let loaded: TokenSet = serde_json::from_str(&text).unwrap();

        assert_eq!(loaded.access_token, "access-123");
        assert_eq!(loaded.refresh_token.as_deref(), Some("refresh-456"));
        assert_eq!(loaded.expires_at, Some(9999999999));
        assert!(!is_expired(&loaded));
    }

    #[tokio::test]
    async fn start_auth_flow_builds_valid_url() {
        let meta = OAuthMetadata {
            authorization_endpoint: "https://auth.example.com/authorize".to_string(),
            token_endpoint: "https://auth.example.com/token".to_string(),
            scopes_supported: vec!["read".to_string()],
        };

        let flow = start_auth_flow(&meta, "my-client", 9999, &["read".to_string()]).await;

        assert!(flow
            .auth_url
            .starts_with("https://auth.example.com/authorize?"));
        assert!(flow.auth_url.contains("response_type=code"));
        assert!(flow.auth_url.contains("client_id=my-client"));
        assert!(flow.auth_url.contains("code_challenge_method=S256"));
        assert!(flow.auth_url.contains(&format!("state={}", flow.state)));
        assert!(flow.auth_url.contains("redirect_uri="));
        assert!(flow.auth_url.contains("scope=read"));
        assert!(!flow.code_verifier.is_empty());
        assert_eq!(flow.client_id, "my-client");
    }

    #[test]
    fn oauth_metadata_deserialization() {
        let json = r#"{
            "authorization_endpoint": "https://example.com/auth",
            "token_endpoint": "https://example.com/token",
            "scopes_supported": ["read", "write"]
        }"#;

        let meta: OAuthMetadata = serde_json::from_str(json).unwrap();
        assert_eq!(meta.authorization_endpoint, "https://example.com/auth");
        assert_eq!(meta.token_endpoint, "https://example.com/token");
        assert_eq!(meta.scopes_supported, vec!["read", "write"]);
    }

    #[test]
    fn oauth_metadata_deserialization_minimal() {
        let json = r#"{
            "authorization_endpoint": "https://example.com/auth",
            "token_endpoint": "https://example.com/token"
        }"#;

        let meta: OAuthMetadata = serde_json::from_str(json).unwrap();
        assert!(meta.scopes_supported.is_empty());
    }
}
