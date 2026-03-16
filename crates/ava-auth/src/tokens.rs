//! Token exchange, refresh, and JWT utilities.

use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use serde::{Deserialize, Serialize};
use tracing::{debug, error, info};

use crate::config::OAuthConfig;
use crate::pkce::PkceParams;
use crate::AuthError;

/// OAuth tokens returned from authorization.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OAuthTokens {
    pub access_token: String,
    pub refresh_token: Option<String>,
    /// Expiry as Unix timestamp in seconds.
    pub expires_at: Option<u64>,
    pub id_token: Option<String>,
}

#[derive(Deserialize)]
struct TokenResponse {
    access_token: String,
    refresh_token: Option<String>,
    expires_in: Option<u64>,
    id_token: Option<String>,
}

/// Exchange an authorization code for tokens (PKCE flow).
pub async fn exchange_code_for_tokens(
    config: &OAuthConfig,
    code: &str,
    pkce: &PkceParams,
) -> Result<OAuthTokens, AuthError> {
    let redirect_uri = format!(
        "http://localhost:{}{}",
        config.redirect_port, config.redirect_path
    );

    let client = reqwest::Client::new();
    let response = client
        .post(config.token_url)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .body(format!(
            "grant_type=authorization_code&code={}&redirect_uri={}&client_id={}&code_verifier={}",
            urlencoding::encode(code),
            urlencoding::encode(&redirect_uri),
            urlencoding::encode(config.client_id),
            urlencoding::encode(&pkce.verifier),
        ))
        .send()
        .await
        .map_err(|e| AuthError::TokenExchange(format!("Request failed: {e}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        error!("Token exchange failed: {status}");
        return Err(AuthError::TokenExchange(format!(
            "Token endpoint returned {status}: {}",
            &body[..body.len().min(500)]
        )));
    }

    let raw: TokenResponse = response
        .json()
        .await
        .map_err(|e| AuthError::TokenExchange(format!("Failed to parse token response: {e}")))?;

    let now_secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    info!("OAuth token exchange succeeded");
    Ok(OAuthTokens {
        access_token: raw.access_token,
        refresh_token: raw.refresh_token,
        expires_at: raw.expires_in.map(|ei| now_secs + ei),
        id_token: raw.id_token,
    })
}

/// Refresh an expired access token.
pub async fn refresh_token(
    config: &OAuthConfig,
    refresh_tok: &str,
) -> Result<OAuthTokens, AuthError> {
    debug!("Refreshing OAuth token");
    let client = reqwest::Client::new();
    let response = client
        .post(config.token_url)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .body(format!(
            "grant_type=refresh_token&client_id={}&refresh_token={}",
            urlencoding::encode(config.client_id),
            urlencoding::encode(refresh_tok),
        ))
        .send()
        .await
        .map_err(|e| AuthError::RefreshFailed(format!("Request failed: {e}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        error!("OAuth token refresh failed: {status}");
        return Err(AuthError::RefreshFailed(format!(
            "Token endpoint returned {status}: {}",
            &body[..body.len().min(500)]
        )));
    }

    let raw: TokenResponse = response
        .json()
        .await
        .map_err(|e| AuthError::RefreshFailed(format!("Failed to parse response: {e}")))?;

    let now_secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    info!("OAuth token refreshed successfully");
    Ok(OAuthTokens {
        access_token: raw.access_token,
        refresh_token: raw.refresh_token,
        expires_at: raw.expires_in.map(|ei| now_secs + ei),
        id_token: raw.id_token,
    })
}

/// Decode a JWT payload without signature verification.
///
/// Safe for extracting claims from id_tokens that were already validated
/// by the authorization server during the token exchange.
pub fn decode_jwt_payload(jwt: &str) -> Result<serde_json::Value, AuthError> {
    let parts: Vec<&str> = jwt.split('.').collect();
    if parts.len() != 3 {
        return Err(AuthError::Other("Invalid JWT format".to_string()));
    }

    let payload_bytes = URL_SAFE_NO_PAD
        .decode(parts[1])
        .or_else(|_| {
            // Try with standard base64 padding added
            let padded = match parts[1].len() % 4 {
                2 => format!("{}==", parts[1]),
                3 => format!("{}=", parts[1]),
                _ => parts[1].to_string(),
            };
            URL_SAFE_NO_PAD.decode(padded)
        })
        .map_err(|e| AuthError::Other(format!("Failed to decode JWT payload: {e}")))?;

    serde_json::from_slice(&payload_bytes)
        .map_err(|e| AuthError::Other(format!("Failed to parse JWT payload: {e}")))
}

/// Extract ChatGPT account ID from an id_token.
///
/// Checks top-level `chatgpt_account_id` first, then `organizations[0].id`.
pub fn extract_account_id(id_token: &str) -> Option<String> {
    let payload = decode_jwt_payload(id_token).ok()?;

    // Check top-level field
    if let Some(id) = payload.get("chatgpt_account_id").and_then(|v| v.as_str()) {
        return Some(id.to_string());
    }

    // Check organizations array
    if let Some(orgs) = payload.get("organizations").and_then(|v| v.as_array()) {
        if let Some(first) = orgs.first() {
            if let Some(id) = first.get("id").and_then(|v| v.as_str()) {
                return Some(id.to_string());
            }
        }
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::{AsyncReadExt, AsyncWriteExt};
    use tokio::net::TcpListener;

    fn make_jwt(payload: &serde_json::Value) -> String {
        let header = URL_SAFE_NO_PAD.encode(b"{}");
        let payload_b64 = URL_SAFE_NO_PAD.encode(serde_json::to_vec(payload).unwrap());
        let sig = URL_SAFE_NO_PAD.encode(b"sig");
        format!("{header}.{payload_b64}.{sig}")
    }

    #[test]
    fn decode_jwt_payload_works() {
        let payload = serde_json::json!({"sub": "user123", "name": "Test"});
        let jwt = make_jwt(&payload);
        let decoded = decode_jwt_payload(&jwt).unwrap();
        assert_eq!(decoded["sub"], "user123");
        assert_eq!(decoded["name"], "Test");
    }

    #[test]
    fn extract_account_id_from_top_level() {
        let payload = serde_json::json!({"chatgpt_account_id": "acct-123"});
        let jwt = make_jwt(&payload);
        assert_eq!(extract_account_id(&jwt), Some("acct-123".to_string()));
    }

    #[test]
    fn extract_account_id_from_organizations() {
        let payload = serde_json::json!({
            "organizations": [{"id": "org-456", "name": "Test Org"}]
        });
        let jwt = make_jwt(&payload);
        assert_eq!(extract_account_id(&jwt), Some("org-456".to_string()));
    }

    #[test]
    fn extract_account_id_returns_none_for_missing() {
        let payload = serde_json::json!({"sub": "user"});
        let jwt = make_jwt(&payload);
        assert_eq!(extract_account_id(&jwt), None);
    }

    #[test]
    fn decode_invalid_jwt_fails() {
        assert!(
            decode_jwt_payload("not.a.valid-jwt-but-three-parts").is_err()
                || decode_jwt_payload("not-a-jwt").is_err()
        );
    }

    #[tokio::test]
    async fn refresh_token_posts_refresh_grant_and_parses_tokens() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let addr = listener.local_addr().unwrap();

        let server = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.unwrap();
            let mut buf = vec![0; 4096];
            let read = socket.read(&mut buf).await.unwrap();
            let request = String::from_utf8_lossy(&buf[..read]);
            assert!(request.contains("grant_type=refresh_token"));
            assert!(request.contains("refresh_token=refresh-123"));
            assert!(request.contains("client_id=test-client"));

            let body =
                r#"{"access_token":"new-access","refresh_token":"new-refresh","expires_in":1200}"#;
            let response = format!(
                "HTTP/1.1 200 OK\r\ncontent-type: application/json\r\ncontent-length: {}\r\nconnection: close\r\n\r\n{}",
                body.len(),
                body
            );
            socket.write_all(response.as_bytes()).await.unwrap();
        });

        let config = OAuthConfig {
            client_id: "test-client",
            authorization_url: "https://example.com/auth",
            token_url: Box::leak(format!("http://{addr}/token").into_boxed_str()),
            scopes: &[],
            redirect_port: 0,
            redirect_path: "",
            extra_params: &[],
            flow: crate::config::AuthFlow::Pkce,
        };

        let tokens = refresh_token(&config, "refresh-123").await.unwrap();
        assert_eq!(tokens.access_token, "new-access");
        assert_eq!(tokens.refresh_token.as_deref(), Some("new-refresh"));
        assert!(tokens.expires_at.is_some());

        server.await.unwrap();
    }
}
