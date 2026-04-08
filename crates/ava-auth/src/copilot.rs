//! Copilot-specific token exchange and management.
//!
//! Flow: GitHub OAuth access_token → GET api.github.com/copilot_internal/v2/token
//!     → { token, expires_at, endpoints: { api: "<https://api.individual.githubcopilot.com>" } }

use serde::Deserialize;
use url::Url;

use crate::{http_client, AuthError};

/// Default Copilot API endpoint (individual plan).
const DEFAULT_API_ENDPOINT: &str = "https://api.individual.githubcopilot.com";

/// Token exchange endpoint on GitHub API.
const TOKEN_EXCHANGE_URL: &str = "https://api.github.com/copilot_internal/v2/token";

/// A short-lived Copilot API token obtained by exchanging a GitHub OAuth token.
#[derive(Debug, Clone)]
pub struct CopilotToken {
    /// The Copilot API token (NOT the GitHub OAuth token).
    pub token: String,
    /// Unix timestamp (seconds) when this token expires.
    pub expires_at: u64,
    /// Regional API endpoint extracted from token response.
    pub api_endpoint: String,
}

impl CopilotToken {
    /// Whether this token has expired (with 30s safety margin).
    pub fn is_expired(&self) -> bool {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        now + 30 >= self.expires_at
    }
}

#[derive(Deserialize)]
struct TokenResponse {
    token: String,
    expires_at: u64,
    endpoints: Option<Endpoints>,
}

#[derive(Deserialize)]
struct Endpoints {
    api: Option<String>,
}

/// Extract the API endpoint from a Copilot token string.
///
/// Token format contains `proxy-ep=proxy.individual.githubcopilot.com`.
/// We extract that and convert `proxy.*` → `api.*`.
fn extract_endpoint_from_token(token: &str) -> Option<String> {
    let proxy_ep = token
        .split(';')
        .find(|part| part.starts_with("proxy-ep="))?
        .strip_prefix("proxy-ep=")?;

    if proxy_ep.is_empty() {
        return None;
    }

    let api_host = if let Some(rest) = proxy_ep.strip_prefix("proxy.") {
        format!("api.{rest}")
    } else {
        proxy_ep.to_string()
    };

    Some(format!("https://{api_host}"))
}

/// Allowed Copilot API host suffixes for endpoint validation.
const ALLOWED_COPILOT_HOSTS: &[&str] = &[
    "api.github.com",
    "githubcopilot.com",
    "githubusercontent.com",
];

/// Validate that a Copilot API endpoint points to a trusted host.
fn validate_copilot_endpoint(endpoint: &str) -> Result<(), AuthError> {
    let url = Url::parse(endpoint)
        .map_err(|e| AuthError::Other(format!("Invalid Copilot endpoint URL '{endpoint}': {e}")))?;
    if let Some(host) = url.host_str() {
        if !ALLOWED_COPILOT_HOSTS
            .iter()
            .any(|allowed| host == *allowed || host.ends_with(&format!(".{allowed}")))
        {
            return Err(AuthError::Other(format!("Untrusted Copilot host: {host}")));
        }
    } else {
        return Err(AuthError::Other("Copilot endpoint has no host".to_string()));
    }
    Ok(())
}

/// Exchange a GitHub OAuth access token for a Copilot API token.
pub async fn exchange_copilot_token(github_token: &str) -> Result<CopilotToken, AuthError> {
    let client = http_client()?;
    let response = client
        .get(TOKEN_EXCHANGE_URL)
        .header("Authorization", format!("token {github_token}"))
        .header("Accept", "application/json")
        .header("User-Agent", "GitHubCopilotChat/0.35.0")
        .header("Editor-Version", "vscode/1.107.0")
        .header("Editor-Plugin-Version", "copilot-chat/0.35.0")
        .header("Copilot-Integration-Id", "vscode-chat")
        .send()
        .await
        .map_err(|e| AuthError::TokenExchange(format!("Copilot token exchange failed: {e}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        return Err(AuthError::TokenExchange(format!(
            "Copilot token exchange returned {status}: {}",
            &body[..body.len().min(500)]
        )));
    }

    let raw: TokenResponse = response
        .json()
        .await
        .map_err(|e| AuthError::TokenExchange(format!("Failed to parse Copilot token: {e}")))?;

    // Resolve API endpoint: response.endpoints.api > token string > default
    let api_endpoint = raw
        .endpoints
        .and_then(|ep| ep.api)
        .or_else(|| extract_endpoint_from_token(&raw.token))
        .unwrap_or_else(|| DEFAULT_API_ENDPOINT.to_string());

    // Validate the resolved endpoint against known Copilot hosts
    validate_copilot_endpoint(&api_endpoint)?;

    Ok(CopilotToken {
        token: raw.token,
        expires_at: raw.expires_at,
        api_endpoint,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn is_expired_with_future_timestamp() {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();

        let token = CopilotToken {
            token: "test".to_string(),
            expires_at: now + 600, // 10 minutes from now
            api_endpoint: DEFAULT_API_ENDPOINT.to_string(),
        };
        assert!(!token.is_expired());
    }

    #[test]
    fn is_expired_with_past_timestamp() {
        let token = CopilotToken {
            token: "test".to_string(),
            expires_at: 1000, // Long past
            api_endpoint: DEFAULT_API_ENDPOINT.to_string(),
        };
        assert!(token.is_expired());
    }

    #[test]
    fn is_expired_within_safety_margin() {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();

        let token = CopilotToken {
            token: "test".to_string(),
            expires_at: now + 15, // Within 30s margin
            api_endpoint: DEFAULT_API_ENDPOINT.to_string(),
        };
        assert!(token.is_expired());
    }

    #[test]
    fn extract_endpoint_from_token_with_proxy() {
        let token = "tid=abc;exp=123;proxy-ep=proxy.individual.githubcopilot.com;sku=monthly";
        assert_eq!(
            extract_endpoint_from_token(token),
            Some("https://api.individual.githubcopilot.com".to_string())
        );
    }

    #[test]
    fn extract_endpoint_from_token_without_proxy_prefix() {
        let token = "tid=abc;proxy-ep=custom.endpoint.com";
        assert_eq!(
            extract_endpoint_from_token(token),
            Some("https://custom.endpoint.com".to_string())
        );
    }

    #[test]
    fn extract_endpoint_from_token_missing() {
        let token = "tid=abc;exp=123;sku=monthly";
        assert_eq!(extract_endpoint_from_token(token), None);
    }

    #[test]
    fn extract_endpoint_from_token_empty_value() {
        let token = "tid=abc;proxy-ep=;sku=monthly";
        assert_eq!(extract_endpoint_from_token(token), None);
    }

    #[test]
    fn default_api_endpoint_value() {
        assert_eq!(
            DEFAULT_API_ENDPOINT,
            "https://api.individual.githubcopilot.com"
        );
    }

    #[test]
    fn validate_copilot_endpoint_allows_known_hosts() {
        assert!(validate_copilot_endpoint("https://api.github.com/v1").is_ok());
        assert!(validate_copilot_endpoint("https://api.individual.githubcopilot.com").is_ok());
        assert!(validate_copilot_endpoint("https://copilot-proxy.githubusercontent.com").is_ok());
    }

    #[test]
    fn validate_copilot_endpoint_rejects_unknown_hosts() {
        assert!(validate_copilot_endpoint("https://evil.example.com").is_err());
        assert!(validate_copilot_endpoint("https://not-github.com").is_err());
        assert!(validate_copilot_endpoint("https://api.github.com.evil.com").is_err());
    }

    #[test]
    fn validate_copilot_endpoint_rejects_invalid_urls() {
        assert!(validate_copilot_endpoint("not-a-url").is_err());
    }
}
