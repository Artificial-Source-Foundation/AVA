use std::collections::{HashMap, HashSet};
use std::future::Future;
use std::path::{Path, PathBuf};
use std::pin::Pin;

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use tokio::fs;
use tracing::warn;

/// Per-provider credential entry.
#[derive(Clone, Default, Serialize, Deserialize)]
pub struct ProviderCredential {
    /// API key or token.
    #[serde(default)]
    pub api_key: String,
    /// Optional base URL override (e.g., custom OpenAI-compatible endpoint).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub base_url: Option<String>,
    /// Optional organization ID (OpenAI).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub org_id: Option<String>,
    /// OAuth access token (for PKCE/device code providers).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_token: Option<String>,
    /// OAuth refresh token for automatic renewal.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_refresh_token: Option<String>,
    /// OAuth token expiry as Unix timestamp (seconds).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_expires_at: Option<u64>,
    /// OAuth account identifier (e.g., ChatGPT account ID).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_account_id: Option<String>,
    /// Enable LiteLLM proxy compatibility mode.
    /// When true, a dummy tool is injected into requests with empty tool lists
    /// to prevent LiteLLM routing issues with certain models.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub litellm_compatible: Option<bool>,
    /// Override loop-prone detection for this provider.
    /// When `true`, all models from this provider get aggressive stuck detection.
    /// When `false`, relaxed detection is used regardless of model registry flags.
    /// When `None` (default), falls back to model registry + heuristics.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub loop_prone: Option<bool>,
}

impl std::fmt::Debug for ProviderCredential {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ProviderCredential")
            .field(
                "api_key",
                &if self.api_key.is_empty() {
                    "<empty>"
                } else {
                    "<redacted>"
                },
            )
            .field("base_url", &self.base_url)
            .field("org_id", &self.org_id)
            .field(
                "oauth_token",
                &self.oauth_token.as_ref().map(|t| {
                    if t.is_empty() {
                        "<empty>"
                    } else {
                        "<redacted>"
                    }
                }),
            )
            .field(
                "oauth_refresh_token",
                &self.oauth_refresh_token.as_ref().map(|t| {
                    if t.is_empty() {
                        "<empty>"
                    } else {
                        "<redacted>"
                    }
                }),
            )
            .field("oauth_expires_at", &self.oauth_expires_at)
            .field("oauth_account_id", &self.oauth_account_id)
            .finish()
    }
}

impl ProviderCredential {
    /// Whether this credential has OAuth tokens configured.
    pub fn is_oauth_configured(&self) -> bool {
        self.oauth_token.is_some()
    }

    /// Whether the OAuth token has expired.
    pub fn is_oauth_expired(&self) -> bool {
        match self.oauth_expires_at {
            Some(expires_at) => {
                let now = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_secs();
                // Refresh 30 seconds before actual expiry
                now + 30 >= expires_at
            }
            None => false, // No expiry set — assume valid
        }
    }

    /// Returns the effective API key: OAuth token (if valid) > api_key.
    pub fn effective_api_key(&self) -> Option<&str> {
        if let Some(ref token) = self.oauth_token {
            if !self.is_oauth_expired() {
                return Some(token.as_str());
            }
        }
        if self.api_key.trim().is_empty() {
            None
        } else {
            Some(&self.api_key)
        }
    }
}

/// Credential store loaded from ~/.ava/credentials.json.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CredentialStore {
    /// Provider name -> credential mapping.
    pub providers: HashMap<String, ProviderCredential>,
}

#[derive(Debug, Clone)]
pub struct PendingProviderRefresh {
    pub existing: ProviderCredential,
    pub refresh_token: String,
    pub config: &'static ava_auth::config::OAuthConfig,
}

#[derive(Debug, Clone)]
pub enum ProviderCredentialState {
    Ready(Option<ProviderCredential>),
    RefreshNeeded(PendingProviderRefresh),
}

impl CredentialStore {
    pub async fn load(path: &Path) -> Result<Self> {
        if !path.exists() {
            return Ok(Self::default());
        }

        let content = fs::read_to_string(path)
            .await
            .map_err(|error| AvaError::IoError(error.to_string()))?;

        let store: Self = serde_json::from_str(&content)
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        for (provider, credential) in &store.providers {
            // Skip placeholder check for OAuth-configured providers (api_key is empty by design)
            if credential.is_oauth_configured() {
                continue;
            }
            if is_placeholder_key(&credential.api_key) {
                return Err(AvaError::ConfigError(format!(
                    "Provider {provider} has placeholder API key in credentials file"
                )));
            }
        }

        Ok(store)
    }

    pub async fn load_default() -> Result<Self> {
        Self::load(&Self::default_path()?).await
    }

    pub async fn save(&self, path: &Path) -> Result<()> {
        let content = serde_json::to_string_pretty(self)
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        crate::write_file_atomic(path, &content).await
    }

    pub async fn save_default(&self) -> Result<()> {
        self.save(&Self::default_path()?).await
    }

    /// Get credential for provider. Env vars override file credentials.
    ///
    /// Lookup order:
    /// 1) `AVA_<PROVIDER>_API_KEY`
    /// 2) Standard provider env var (e.g. OPENAI_API_KEY)
    /// 3) File credential
    pub fn get(&self, provider: &str) -> Option<ProviderCredential> {
        if let Some(credential) = env_override_credential(provider, self.providers.get(provider)) {
            return Some(credential);
        }

        self.providers.get(provider).cloned()
    }

    pub fn provider_credential_state(&self, provider: &str) -> ProviderCredentialState {
        if let Some(credential) = env_override_credential(provider, self.providers.get(provider)) {
            return ProviderCredentialState::Ready(Some(credential));
        }

        let Some(existing) = self.providers.get(provider).cloned() else {
            return ProviderCredentialState::Ready(None);
        };

        if !existing.is_oauth_configured() || !existing.is_oauth_expired() {
            return ProviderCredentialState::Ready(Some(existing));
        }

        let Some(refresh_token) = existing.oauth_refresh_token.clone() else {
            return ProviderCredentialState::Ready(Some(existing));
        };

        let Some(config) = ava_auth::config::oauth_config(provider) else {
            return ProviderCredentialState::Ready(Some(existing));
        };

        ProviderCredentialState::RefreshNeeded(PendingProviderRefresh {
            existing,
            refresh_token,
            config,
        })
    }

    pub fn apply_refreshed_provider_tokens(
        &mut self,
        provider: &str,
        expected: &ProviderCredential,
        refreshed_tokens: ava_auth::tokens::OAuthTokens,
    ) -> Option<ProviderCredential> {
        if let Some(credential) = env_override_credential(provider, self.providers.get(provider)) {
            return Some(credential);
        }

        let current = self.providers.get(provider).cloned()?;
        if current.oauth_token != expected.oauth_token
            || current.oauth_refresh_token != expected.oauth_refresh_token
            || current.oauth_expires_at != expected.oauth_expires_at
        {
            return Some(current);
        }

        let mut refreshed = current;
        refreshed.oauth_token = Some(refreshed_tokens.access_token.clone());
        refreshed.oauth_refresh_token = refreshed_tokens
            .refresh_token
            .or(refreshed.oauth_refresh_token.clone());
        refreshed.oauth_expires_at = refreshed_tokens.expires_at;
        refreshed.oauth_account_id = refreshed_tokens
            .id_token
            .as_deref()
            .and_then(ava_auth::tokens::extract_account_id)
            .or_else(|| ava_auth::tokens::extract_account_id(&refreshed_tokens.access_token))
            .or(refreshed.oauth_account_id.clone());

        self.providers
            .insert(provider.to_string(), refreshed.clone());
        Some(refreshed)
    }

    pub async fn resolve_provider_credentials(
        &mut self,
        provider: &str,
        persist_path: Option<&Path>,
    ) -> Result<Option<ProviderCredential>> {
        self.resolve_provider_credentials_with_refresher(
            provider,
            persist_path,
            |config, refresh_token| Box::pin(refresh_oauth_tokens(config, refresh_token)),
        )
        .await
    }

    async fn resolve_provider_credentials_with_refresher<F>(
        &mut self,
        provider: &str,
        persist_path: Option<&Path>,
        refresher: F,
    ) -> Result<Option<ProviderCredential>>
    where
        F: for<'a> Fn(
            &'a ava_auth::config::OAuthConfig,
            &'a str,
        ) -> Pin<
            Box<
                dyn Future<
                        Output = std::result::Result<
                            ava_auth::tokens::OAuthTokens,
                            ava_auth::AuthError,
                        >,
                    > + Send
                    + 'a,
            >,
        >,
    {
        let refresh = match self.provider_credential_state(provider) {
            ProviderCredentialState::Ready(credential) => return Ok(credential),
            ProviderCredentialState::RefreshNeeded(refresh) => refresh,
        };

        let refreshed_tokens = match refresher(refresh.config, &refresh.refresh_token).await {
            Ok(tokens) => tokens,
            Err(error) if !refresh.existing.api_key.trim().is_empty() => {
                warn!(provider, %error, "OAuth refresh failed; falling back to static API key");
                return Ok(Some(refresh.existing));
            }
            Err(error) => {
                return Err(AvaError::ConfigError(format!(
                    "Failed to refresh OAuth credential for {provider}: {error}"
                )))
            }
        };

        let refreshed =
            self.apply_refreshed_provider_tokens(provider, &refresh.existing, refreshed_tokens);

        if let Some(path) = persist_path {
            self.save(path).await?;
        }

        Ok(refreshed)
    }

    pub fn set(&mut self, provider: &str, credential: ProviderCredential) {
        self.providers.insert(provider.to_string(), credential);
    }

    /// Set OAuth tokens for a provider, preserving existing API key/base_url.
    pub fn set_oauth(
        &mut self,
        provider: &str,
        access_token: &str,
        refresh_token: Option<&str>,
        expires_at: Option<u64>,
    ) {
        let mut cred =
            self.providers
                .get(provider)
                .cloned()
                .unwrap_or_else(|| ProviderCredential {
                    api_key: String::new(),
                    base_url: None,
                    org_id: None,
                    oauth_token: None,
                    oauth_refresh_token: None,
                    oauth_expires_at: None,
                    oauth_account_id: None,
                    litellm_compatible: None,
                    loop_prone: None,
                });
        cred.oauth_token = Some(access_token.to_string());
        cred.oauth_refresh_token = refresh_token.map(String::from);
        cred.oauth_expires_at = expires_at;
        self.providers.insert(provider.to_string(), cred);
    }

    pub fn remove(&mut self, provider: &str) -> bool {
        self.providers.remove(provider).is_some()
    }

    pub fn providers(&self) -> Vec<&str> {
        let mut providers = self
            .providers
            .keys()
            .map(String::as_str)
            .collect::<Vec<&str>>();
        providers.sort_unstable();
        providers
    }

    pub fn configured_providers(&self) -> Vec<&str> {
        let mut configured = HashSet::new();

        for provider in self.providers() {
            if let Some(credential) = self.get(provider) {
                let has_key = !credential.api_key.trim().is_empty();
                let has_oauth = credential.is_oauth_configured();
                let has_base_url = credential.base_url.is_some();
                if has_key || has_oauth || (provider == "ollama" && has_base_url) {
                    configured.insert(provider);
                }
            }
        }

        for &provider in known_providers() {
            if let Some(credential) = self.get(provider) {
                let has_key = !credential.api_key.trim().is_empty();
                let has_oauth = credential.is_oauth_configured();
                let has_base_url = credential.base_url.is_some();
                if has_key || has_oauth || (provider == "ollama" && has_base_url) {
                    configured.insert(provider);
                }
            }
        }

        let mut providers = configured.into_iter().collect::<Vec<&str>>();
        providers.sort_unstable();
        providers
    }

    pub fn default_path() -> Result<PathBuf> {
        let home = dirs::home_dir().ok_or_else(|| {
            AvaError::ConfigError("Could not resolve home directory for credentials".to_string())
        })?;
        Ok(home.join(".ava").join("credentials.json"))
    }
}

fn env_override_credential(
    provider: &str,
    base: Option<&ProviderCredential>,
) -> Option<ProviderCredential> {
    let provider_key = provider.to_ascii_uppercase().replace('-', "_");
    let ava_env = format!("AVA_{provider_key}_API_KEY");

    if let Ok(api_key) = std::env::var(&ava_env) {
        let api_key = api_key.trim().to_string();
        if !api_key.is_empty() && !is_placeholder_key(&api_key) {
            let mut credential = base.cloned().unwrap_or_else(empty_credential);
            credential.api_key = api_key;
            return Some(credential);
        }
    }

    if let Some(standard_env) = standard_env_var(provider) {
        if let Ok(api_key) = std::env::var(standard_env) {
            let api_key = api_key.trim().to_string();
            if !api_key.is_empty() && !is_placeholder_key(&api_key) {
                let mut credential = base.cloned().unwrap_or_else(empty_credential);
                credential.api_key = api_key;
                return Some(credential);
            }
        }
    }

    None
}

fn empty_credential() -> ProviderCredential {
    ProviderCredential {
        api_key: String::new(),
        base_url: None,
        org_id: None,
        oauth_token: None,
        oauth_refresh_token: None,
        oauth_expires_at: None,
        oauth_account_id: None,
        litellm_compatible: None,
        loop_prone: None,
    }
}

async fn refresh_oauth_tokens(
    config: &ava_auth::config::OAuthConfig,
    refresh_token: &str,
) -> std::result::Result<ava_auth::tokens::OAuthTokens, ava_auth::AuthError> {
    ava_auth::tokens::refresh_token(config, refresh_token).await
}

pub fn known_providers() -> &'static [&'static str] {
    &[
        "anthropic",
        "openai",
        "openrouter",
        "copilot",
        "gemini",
        "inception",
        "alibaba",
        "zai",
        "kimi",
        "minimax",
        "ollama",
    ]
}

pub fn standard_env_var(provider: &str) -> Option<&'static str> {
    match provider {
        "anthropic" => Some("ANTHROPIC_API_KEY"),
        "openai" => Some("OPENAI_API_KEY"),
        "openrouter" => Some("OPENROUTER_API_KEY"),
        "gemini" => Some("GEMINI_API_KEY"),
        "inception" => Some("INCEPTION_API_KEY"),
        "alibaba" => Some("DASHSCOPE_API_KEY"),
        "zai" => Some("ZHIPU_API_KEY"),
        "kimi" => Some("KIMI_API_KEY"),
        "minimax" => Some("MINIMAX_API_KEY"),
        "ollama" => Some("OLLAMA_API_KEY"),
        _ => None,
    }
}

fn is_placeholder_key(value: &str) -> bool {
    let normalized = value.trim().to_ascii_lowercase();
    normalized.is_empty() || normalized == "sk-xxx" || normalized.contains("your-key-here")
}

#[cfg(test)]
mod tests {
    use super::*;
    use base64::Engine;
    use std::sync::Mutex;
    use tempfile::TempDir;

    static ENV_LOCK: Mutex<()> = Mutex::new(());

    fn lock_env() -> std::sync::MutexGuard<'static, ()> {
        ENV_LOCK
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn sample_credential(key: &str) -> ProviderCredential {
        ProviderCredential {
            api_key: key.to_string(),
            base_url: None,
            org_id: None,
            oauth_token: None,
            oauth_refresh_token: None,
            oauth_expires_at: None,
            oauth_account_id: None,
            litellm_compatible: None,
            loop_prone: None,
        }
    }

    #[tokio::test]
    async fn load_from_file() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("credentials.json");

        let mut store = CredentialStore::default();
        store.set("openai", sample_credential("sk-test-openai"));
        store.save(&path).await.unwrap();

        let loaded = CredentialStore::load(&path).await.unwrap();
        assert_eq!(loaded.providers().as_slice(), &["openai"]);
        assert_eq!(loaded.get("openai").unwrap().api_key, "sk-test-openai");
    }

    #[tokio::test]
    async fn save_reload_roundtrip() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("credentials.json");

        let mut store = CredentialStore::default();
        store.set(
            "openrouter",
            ProviderCredential {
                api_key: "or-key-1234".to_string(),
                base_url: Some("https://openrouter.ai/api".to_string()),
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );
        store.save(&path).await.unwrap();

        let reloaded = CredentialStore::load(&path).await.unwrap();
        let credential = reloaded.get("openrouter").unwrap();
        assert_eq!(credential.api_key, "or-key-1234");
        assert_eq!(
            credential.base_url.as_deref(),
            Some("https://openrouter.ai/api")
        );
    }

    #[test]
    fn standard_env_var_maps_canonical_provider_ids() {
        let _guard = lock_env();
        assert_eq!(standard_env_var("alibaba"), Some("DASHSCOPE_API_KEY"));
        assert_eq!(standard_env_var("zai"), Some("ZHIPU_API_KEY"));
        assert_eq!(standard_env_var("minimax"), Some("MINIMAX_API_KEY"));
        assert_eq!(standard_env_var("inception"), Some("INCEPTION_API_KEY"));
    }

    #[test]
    fn provider_crud() {
        let mut store = CredentialStore::default();
        assert!(store.providers().is_empty());

        store.set("anthropic", sample_credential("anthropic-key"));
        assert_eq!(store.get("anthropic").unwrap().api_key, "anthropic-key");
        assert_eq!(store.providers(), vec!["anthropic"]);

        assert!(store.remove("anthropic"));
        assert!(store.get("anthropic").is_none());
        assert!(!store.remove("anthropic"));
    }

    #[test]
    fn default_path_resolves_to_home_ava_credentials() {
        let path = CredentialStore::default_path().unwrap();
        let path_str = path.to_string_lossy();
        assert!(path_str.ends_with(".ava/credentials.json"));
    }

    #[tokio::test]
    async fn missing_file_returns_empty_store() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("missing.json");

        let store = CredentialStore::load(&path).await.unwrap();
        assert!(store.providers().is_empty());
    }

    #[tokio::test]
    async fn credentials_refresh_expired_oauth_tokens_before_returning_provider() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("credentials.json");

        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ProviderCredential {
                api_key: String::new(),
                base_url: None,
                org_id: None,
                oauth_token: Some("expired-token".to_string()),
                oauth_refresh_token: Some("refresh-token".to_string()),
                oauth_expires_at: Some(1),
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );

        let resolved = store
            .resolve_provider_credentials_with_refresher(
                "openai",
                Some(&path),
                |_config, refresh_token| {
                    let refresh_token = refresh_token.to_string();
                    Box::pin(async move {
                        assert_eq!(refresh_token, "refresh-token");
                        Ok(ava_auth::tokens::OAuthTokens {
                            access_token: "fresh-token".to_string(),
                            refresh_token: Some("fresh-refresh".to_string()),
                            expires_at: Some(999_999_999),
                            id_token: Some({
                                let header =
                                    base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(b"{}");
                                let payload = base64::engine::general_purpose::URL_SAFE_NO_PAD
                                    .encode(br#"{"chatgpt_account_id":"acct-123"}"#);
                                let sig =
                                    base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(b"sig");
                                format!("{header}.{payload}.{sig}")
                            }),
                        })
                    })
                },
            )
            .await
            .unwrap()
            .unwrap();

        assert_eq!(resolved.oauth_token.as_deref(), Some("fresh-token"));
        assert_eq!(
            resolved.oauth_refresh_token.as_deref(),
            Some("fresh-refresh")
        );
        assert_eq!(resolved.oauth_account_id.as_deref(), Some("acct-123"));

        let persisted = CredentialStore::load(&path).await.unwrap();
        let persisted_openai = persisted.get("openai").unwrap();
        assert_eq!(persisted_openai.oauth_token.as_deref(), Some("fresh-token"));
    }

    #[tokio::test]
    async fn credentials_refresh_extracts_account_id_from_access_token() {
        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ProviderCredential {
                api_key: String::new(),
                base_url: None,
                org_id: None,
                oauth_token: Some("expired-token".to_string()),
                oauth_refresh_token: Some("refresh-token".to_string()),
                oauth_expires_at: Some(1),
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );

        let resolved = store
            .resolve_provider_credentials_with_refresher(
                "openai",
                None,
                |_config, _refresh_token| {
                    Box::pin(async move {
                        Ok(ava_auth::tokens::OAuthTokens {
                            access_token: {
                                let header =
                                    base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(b"{}");
                                let payload = base64::engine::general_purpose::URL_SAFE_NO_PAD
                                    .encode(br#"{"https://api.openai.com/auth":{"chatgpt_account_id":"acct-access"}}"#);
                                let sig =
                                    base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(b"sig");
                                format!("{header}.{payload}.{sig}")
                            },
                            refresh_token: Some("fresh-refresh".to_string()),
                            expires_at: Some(999_999_999),
                            id_token: None,
                        })
                    })
                },
            )
            .await
            .unwrap()
            .unwrap();

        assert_eq!(resolved.oauth_account_id.as_deref(), Some("acct-access"));
    }

    #[tokio::test]
    async fn credentials_preserve_static_api_key_when_refresh_fails() {
        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ProviderCredential {
                api_key: "static-key".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: Some("expired-token".to_string()),
                oauth_refresh_token: Some("refresh-token".to_string()),
                oauth_expires_at: Some(1),
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );

        let resolved = store
            .resolve_provider_credentials_with_refresher(
                "openai",
                None,
                |_config, _refresh_token| {
                    Box::pin(
                        async move { Err(ava_auth::AuthError::RefreshFailed("boom".to_string())) },
                    )
                },
            )
            .await
            .unwrap()
            .unwrap();

        assert_eq!(resolved.effective_api_key(), Some("static-key"));
        assert_eq!(resolved.oauth_token.as_deref(), Some("expired-token"));
    }
}
