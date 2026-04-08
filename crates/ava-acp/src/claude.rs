use std::env;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use tokio::process::Command;

const AUTH_STATUS_TTL: Duration = Duration::from_secs(60);
const AUTH_STATUS_FAILURE_TTL: Duration = Duration::from_secs(5);
const AUTH_STATUS_COMMAND_TIMEOUT: Duration = Duration::from_secs(5);
const OAUTH_TOKEN_URL: &str = "https://platform.claude.com/v1/oauth/token";
const OAUTH_CLIENT_ID: &str = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
#[cfg(target_os = "macos")]
const KEYCHAIN_SERVICE: &str = "Claude Code-credentials";

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ClaudeAuthStatus {
    #[serde(default, alias = "logged_in")]
    pub logged_in: bool,
    #[serde(default, alias = "subscription_type")]
    pub subscription_type: Option<String>,
    #[serde(default)]
    pub email: Option<String>,
}

#[derive(Debug, Default)]
struct AuthStatusCache {
    current: Option<ClaudeAuthStatus>,
    last_known_good: Option<ClaudeAuthStatus>,
    last_checked_at: Option<Instant>,
    last_check_failed: bool,
}

fn auth_status_cache() -> &'static Mutex<AuthStatusCache> {
    static CACHE: OnceLock<Mutex<AuthStatusCache>> = OnceLock::new();
    CACHE.get_or_init(|| Mutex::new(AuthStatusCache::default()))
}

fn refresh_lock() -> &'static tokio::sync::Mutex<()> {
    static LOCK: OnceLock<tokio::sync::Mutex<()>> = OnceLock::new();
    LOCK.get_or_init(|| tokio::sync::Mutex::new(()))
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct ClaudeCredentialsFile {
    #[serde(alias = "claude_ai_oauth")]
    claude_ai_oauth: ClaudeOAuthCredentials,
    #[serde(flatten)]
    other: serde_json::Map<String, serde_json::Value>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
struct ClaudeOAuthCredentials {
    #[serde(alias = "access_token")]
    access_token: String,
    #[serde(alias = "refresh_token")]
    refresh_token: String,
    #[serde(alias = "expires_at")]
    expires_at: u64,
    #[serde(default)]
    scopes: Option<Vec<String>>,
    #[serde(default, alias = "subscription_type")]
    subscription_type: Option<String>,
    #[serde(default, alias = "rate_limit_tier")]
    rate_limit_tier: Option<String>,
}

#[derive(Debug, Deserialize)]
struct RefreshTokenResponse {
    access_token: String,
    #[serde(default)]
    refresh_token: Option<String>,
    #[serde(default)]
    expires_in: Option<u64>,
    #[serde(default)]
    expires_at: Option<u64>,
}

pub async fn get_cached_auth_status() -> Option<ClaudeAuthStatus> {
    {
        let cache = auth_status_cache()
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        if let Some(last_checked_at) = cache.last_checked_at {
            let ttl = if cache.last_check_failed {
                AUTH_STATUS_FAILURE_TTL
            } else {
                AUTH_STATUS_TTL
            };
            if last_checked_at.elapsed() < ttl {
                return cache
                    .current
                    .clone()
                    .or_else(|| cache.last_known_good.clone());
            }
        }
    }

    let status = fetch_auth_status().await;
    let mut cache = auth_status_cache()
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    cache.last_checked_at = Some(Instant::now());
    cache.last_check_failed = status.is_none();
    cache.current = status.clone();
    if let Some(ref ok) = status {
        cache.last_known_good = Some(ok.clone());
    }

    status.or_else(|| cache.last_known_good.clone())
}

async fn fetch_auth_status() -> Option<ClaudeAuthStatus> {
    let output = tokio::time::timeout(
        AUTH_STATUS_COMMAND_TIMEOUT,
        Command::new("claude")
            .args(["auth", "status"])
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .output(),
    )
    .await
    .ok()?
    .ok()?;
    if !output.status.success() {
        return None;
    }

    serde_json::from_slice::<ClaudeAuthStatus>(&output.stdout).ok()
}

pub async fn refresh_oauth_token() -> Result<bool> {
    let _guard = refresh_lock().lock().await;
    let credentials = load_claude_credentials().await?;
    let Some(mut credentials) = credentials else {
        return Ok(false);
    };
    if credentials.claude_ai_oauth.refresh_token.trim().is_empty() {
        return Ok(false);
    }

    let client = reqwest::Client::builder()
        .timeout(Duration::from_secs(15))
        .build()
        .map_err(|error| {
            AvaError::ConfigError(format!("Failed to build Claude OAuth client: {error}"))
        })?;

    let response = client
        .post(OAUTH_TOKEN_URL)
        .json(&serde_json::json!({
            "grant_type": "refresh_token",
            "client_id": OAUTH_CLIENT_ID,
            "refresh_token": credentials.claude_ai_oauth.refresh_token,
        }))
        .send()
        .await
        .map_err(|error| AvaError::ConfigError(format!("Claude OAuth refresh failed: {error}")))?;

    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        return Err(AvaError::ConfigError(format!(
            "Claude OAuth refresh failed with status {status}: {body}"
        )));
    }

    let token_data = response
        .json::<RefreshTokenResponse>()
        .await
        .map_err(|error| {
            AvaError::ConfigError(format!(
                "Failed to parse Claude OAuth refresh response: {error}"
            ))
        })?;

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    credentials.claude_ai_oauth.access_token = token_data.access_token;
    if let Some(refresh_token) = token_data.refresh_token {
        credentials.claude_ai_oauth.refresh_token = refresh_token;
    }
    credentials.claude_ai_oauth.expires_at = token_data
        .expires_at
        .or_else(|| token_data.expires_in.map(|secs| now + secs))
        .unwrap_or(now + 8 * 60 * 60);

    save_claude_credentials(&credentials).await?;

    let mut cache = auth_status_cache()
        .lock()
        .unwrap_or_else(|e| e.into_inner());
    cache.last_checked_at = None;
    cache.last_check_failed = false;

    Ok(true)
}

async fn load_claude_credentials() -> Result<Option<ClaudeCredentialsFile>> {
    #[cfg(target_os = "macos")]
    {
        load_keychain_credentials().await
    }

    #[cfg(not(target_os = "macos"))]
    {
        load_file_credentials().await
    }
}

async fn save_claude_credentials(credentials: &ClaudeCredentialsFile) -> Result<()> {
    #[cfg(target_os = "macos")]
    {
        save_keychain_credentials(credentials).await
    }

    #[cfg(not(target_os = "macos"))]
    {
        save_file_credentials(credentials).await
    }
}

#[cfg(target_os = "macos")]
async fn load_keychain_credentials() -> Result<Option<ClaudeCredentialsFile>> {
    let user = env::var("USER").unwrap_or_default();
    let output = Command::new("/usr/bin/security")
        .args([
            "find-generic-password",
            "-s",
            KEYCHAIN_SERVICE,
            "-a",
            user.as_str(),
            "-w",
        ])
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .output()
        .await
        .map_err(|error| {
            AvaError::ConfigError(format!(
                "Failed to read Claude Keychain credentials: {error}"
            ))
        })?;

    if !output.status.success() {
        return Ok(None);
    }

    let raw = String::from_utf8_lossy(&output.stdout);
    parse_credentials_blob(raw.trim()).map(Some)
}

#[cfg(target_os = "macos")]
async fn save_keychain_credentials(credentials: &ClaudeCredentialsFile) -> Result<()> {
    let user = env::var("USER").unwrap_or_default();
    let value = serde_json::to_string_pretty(credentials).map_err(|error| {
        AvaError::ConfigError(format!("Failed to serialize Claude credentials: {error}"))
    })?;
    let output = Command::new("/usr/bin/security")
        .args([
            "add-generic-password",
            "-U",
            "-s",
            KEYCHAIN_SERVICE,
            "-a",
            user.as_str(),
            "-w",
            value.as_str(),
        ])
        .stdout(Stdio::null())
        .stderr(Stdio::piped())
        .output()
        .await
        .map_err(|error| {
            AvaError::ConfigError(format!(
                "Failed to write Claude Keychain credentials: {error}"
            ))
        })?;

    if output.status.success() {
        Ok(())
    } else {
        Err(AvaError::ConfigError(
            String::from_utf8_lossy(&output.stderr).trim().to_string(),
        ))
    }
}

async fn load_file_credentials() -> Result<Option<ClaudeCredentialsFile>> {
    let path = credentials_file_path();
    if !path.exists() {
        return Ok(None);
    }

    let content = tokio::fs::read_to_string(&path).await.map_err(|error| {
        AvaError::ConfigError(format!(
            "Failed to read Claude credentials at {}: {error}",
            path.display()
        ))
    })?;
    parse_credentials_blob(&content).map(Some)
}

async fn save_file_credentials(credentials: &ClaudeCredentialsFile) -> Result<()> {
    let path = credentials_file_path();
    if let Some(parent) = path.parent() {
        tokio::fs::create_dir_all(parent).await.map_err(|error| {
            AvaError::ConfigError(format!(
                "Failed to create Claude credential directory {}: {error}",
                parent.display()
            ))
        })?;
    }

    let content = serde_json::to_string_pretty(credentials).map_err(|error| {
        AvaError::ConfigError(format!("Failed to serialize Claude credentials: {error}"))
    })?;
    tokio::fs::write(&path, content).await.map_err(|error| {
        AvaError::ConfigError(format!(
            "Failed to write Claude credentials at {}: {error}",
            path.display()
        ))
    })?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;

        tokio::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600))
            .await
            .map_err(|error| {
                AvaError::ConfigError(format!(
                    "Failed to secure Claude credentials at {}: {error}",
                    path.display()
                ))
            })?;
    }

    Ok(())
}

fn parse_credentials_blob(raw: &str) -> Result<ClaudeCredentialsFile> {
    if let Ok(credentials) = serde_json::from_str(raw) {
        return Ok(credentials);
    }

    let decoded = hex::decode(raw.trim()).map_err(|error| {
        AvaError::ConfigError(format!("Failed to decode Claude credentials blob: {error}"))
    })?;
    serde_json::from_slice(&decoded).map_err(|error| {
        AvaError::ConfigError(format!("Failed to parse Claude credentials JSON: {error}"))
    })
}

fn credentials_file_path() -> PathBuf {
    if let Ok(config_dir) = env::var("CLAUDE_CONFIG_DIR") {
        return Path::new(&config_dir).join(".credentials.json");
    }

    let home = env::var("HOME").unwrap_or_else(|_| ".".to_string());
    Path::new(&home).join(".claude").join(".credentials.json")
}

pub fn is_auth_expiry_error(message: &str) -> bool {
    let lower = message.to_ascii_lowercase();
    let mentions_auth_credential = lower.contains("oauth token")
        || lower.contains("access token")
        || lower.contains("refresh token")
        || lower.contains("claude login")
        || lower.contains("authentication");
    let mentions_expiry = lower.contains("expired") || lower.contains("expires");
    let mentions_login_reauth =
        lower.contains("login required") || lower.contains("run claude login");
    let mentions_unauthorized_token = lower.contains("unauthorized")
        && (lower.contains("token") || lower.contains("authentication"));

    (mentions_auth_credential && mentions_expiry)
        || mentions_login_reauth
        || mentions_unauthorized_token
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_auth_status_aliases() {
        let status: ClaudeAuthStatus = serde_json::from_str(
            r#"{"loggedIn":true,"subscriptionType":"max","email":"dev@example.com"}"#,
        )
        .unwrap();
        assert!(status.logged_in);
        assert_eq!(status.subscription_type.as_deref(), Some("max"));
    }

    #[test]
    fn detects_auth_expiry_errors() {
        assert!(is_auth_expiry_error(
            "OAuth token expired. Run claude login."
        ));
        assert!(is_auth_expiry_error("Unauthorized: auth token invalid"));
        assert!(!is_auth_expiry_error("maximum turns reached"));
        assert!(!is_auth_expiry_error("invalid token count in request"));
    }

    #[test]
    fn parses_plain_json_credentials_blob() {
        let credentials = parse_credentials_blob(
            r#"{"claudeAiOauth":{"accessToken":"a","refreshToken":"b","expiresAt":1},"mcpOAuth":{"server":{"token":"keep-me"}}}"#,
        )
        .unwrap();
        assert_eq!(credentials.claude_ai_oauth.access_token, "a");
        assert_eq!(credentials.claude_ai_oauth.refresh_token, "b");
        assert!(credentials.other.contains_key("mcpOAuth"));
    }

    #[test]
    fn serializes_credentials_with_claude_code_key_names() {
        let credentials = ClaudeCredentialsFile {
            claude_ai_oauth: ClaudeOAuthCredentials {
                access_token: "a".to_string(),
                refresh_token: "b".to_string(),
                expires_at: 1,
                scopes: None,
                subscription_type: None,
                rate_limit_tier: None,
            },
            other: serde_json::Map::from_iter([(
                "mcpOAuth".to_string(),
                serde_json::json!({"server": {"token": "keep-me"}}),
            )]),
        };

        let json = serde_json::to_string(&credentials).unwrap();
        assert!(json.contains("claudeAiOauth"));
        assert!(json.contains("accessToken"));
        assert!(json.contains("refreshToken"));
        assert!(json.contains("mcpOAuth"));
    }
}
