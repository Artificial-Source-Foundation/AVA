//! Tauri commands for reading and writing configuration.

use ava_config::ProviderCredential;
use serde::Deserialize;
use tauri::State;
use tracing::debug;

use crate::bridge::DesktopBridge;

/// Get the full config as JSON.
#[tauri::command]
pub async fn get_config(bridge: State<'_, DesktopBridge>) -> Result<serde_json::Value, String> {
    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg).map_err(|e| e.to_string())
}

/// Update LLM settings (provider, model, temperature, max_tokens) in config.yaml.
///
/// Only provided (non-null) fields are updated; omitted fields are left unchanged.
#[tauri::command]
pub async fn update_llm_config(
    provider: Option<String>,
    model: Option<String>,
    temperature: Option<f32>,
    max_tokens: Option<usize>,
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    bridge
        .stack
        .config
        .update(|config| {
            if let Some(ref p) = provider {
                config.llm.provider = p.clone();
            }
            if let Some(ref m) = model {
                config.llm.model = m.clone();
            }
            if let Some(t) = temperature {
                config.llm.temperature = t;
            }
            if let Some(mt) = max_tokens {
                config.llm.max_tokens = mt;
            }
        })
        .await
        .map_err(|e| e.to_string())?;

    bridge
        .stack
        .config
        .save()
        .await
        .map_err(|e| e.to_string())?;

    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg.llm).map_err(|e| e.to_string())
}

/// Update feature flags in config.yaml.
///
/// Only provided (non-null) fields are updated; omitted fields are left unchanged.
#[tauri::command]
pub async fn update_feature_flags(
    enable_git: Option<bool>,
    enable_lsp: Option<bool>,
    enable_mcp: Option<bool>,
    audit_logging: Option<bool>,
    session_logging: Option<bool>,
    auto_review: Option<bool>,
    enable_codebase_index: Option<bool>,
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    bridge
        .stack
        .config
        .update(|config| {
            if let Some(v) = enable_git {
                config.features.enable_git = v;
            }
            if let Some(v) = enable_lsp {
                config.features.enable_lsp = v;
            }
            if let Some(v) = enable_mcp {
                config.features.enable_mcp = v;
            }
            if let Some(v) = audit_logging {
                config.features.audit_logging = v;
            }
            if let Some(v) = session_logging {
                config.features.session_logging = v;
            }
            if let Some(v) = auto_review {
                config.features.auto_review = v;
            }
            if let Some(v) = enable_codebase_index {
                config.features.enable_codebase_index = v;
            }
        })
        .await
        .map_err(|e| e.to_string())?;

    bridge
        .stack
        .config
        .save()
        .await
        .map_err(|e| e.to_string())?;

    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg.features).map_err(|e| e.to_string())
}

/// Read the current feature flags from config.yaml.
#[tauri::command]
pub async fn get_feature_flags(
    bridge: State<'_, DesktopBridge>,
) -> Result<serde_json::Value, String> {
    let cfg = bridge.stack.config.get().await;
    serde_json::to_value(&cfg.features).map_err(|e| e.to_string())
}

// ============================================================================
// Credential Sync — Desktop ↔ secure credential store
// ============================================================================

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CredentialEntry {
    pub provider: String,
    pub api_key: String,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StoredAuth {
    #[serde(rename = "type")]
    pub auth_type: String,
    pub access_token: String,
    #[serde(default)]
    pub refresh_token: Option<String>,
    #[serde(default)]
    pub expires_at: Option<u64>,
    #[serde(default)]
    pub account_id: Option<String>,
}

async fn refresh_router_credentials(bridge: &DesktopBridge) {
    let credentials = bridge.stack.config.credentials().await;
    bridge.stack.router.update_credentials(credentials).await;
}

fn clear_oauth_metadata(credential: &mut ProviderCredential) {
    credential.oauth_token = None;
    credential.oauth_refresh_token = None;
    credential.oauth_expires_at = None;
    credential.oauth_account_id = None;
}

async fn store_provider_auth_inner(
    provider: String,
    auth: StoredAuth,
    bridge: &DesktopBridge,
) -> Result<(), String> {
    let provider = provider.trim().to_string();
    if provider.is_empty() {
        return Err("Provider name must not be empty".to_string());
    }

    apply_stored_auth(&mut ProviderCredential::default(), &auth)?;

    bridge
        .stack
        .config
        .update_credentials(|store| {
            let mut credential = store.providers.get(&provider).cloned().unwrap_or_default();
            let _ = apply_stored_auth(&mut credential, &auth);
            store.set(&provider, credential);
        })
        .await
        .map_err(|e| e.to_string())?;

    bridge
        .stack
        .config
        .save_credentials()
        .await
        .map_err(|e| e.to_string())?;

    refresh_router_credentials(bridge).await;
    debug!(provider, auth_type = %auth.auth_type, "Stored provider auth in secure store");
    Ok(())
}

async fn delete_provider_auth_inner(
    provider: String,
    bridge: &DesktopBridge,
) -> Result<(), String> {
    let provider = provider.trim().to_string();
    if provider.is_empty() {
        return Err("Provider name must not be empty".to_string());
    }

    bridge
        .stack
        .config
        .update_credentials(|store| {
            if let Some(credential) = store.providers.get_mut(&provider) {
                clear_stored_auth(credential);
            }
        })
        .await
        .map_err(|e| e.to_string())?;

    bridge
        .stack
        .config
        .save_credentials()
        .await
        .map_err(|e| e.to_string())?;

    refresh_router_credentials(bridge).await;
    debug!(provider, "Deleted provider auth from secure store");
    Ok(())
}

fn normalize_oauth_expires_at(expires_at: Option<u64>) -> Option<u64> {
    expires_at.map(|value| {
        if value > 10_000_000_000 {
            value / 1_000
        } else {
            value
        }
    })
}

fn is_ignored_api_key(value: &str) -> bool {
    let trimmed = value.trim();
    trimmed.is_empty() || trimmed == "(oauth)"
}

fn apply_stored_auth(credential: &mut ProviderCredential, auth: &StoredAuth) -> Result<(), String> {
    let access_token = auth.access_token.trim();
    if access_token.is_empty() {
        return Err("Credential value must not be empty".to_string());
    }

    match auth.auth_type.as_str() {
        "oauth" => {
            credential.api_key.clear();
            credential.oauth_token = Some(access_token.to_string());
            credential.oauth_refresh_token = auth
                .refresh_token
                .as_deref()
                .map(str::trim)
                .filter(|value| !value.is_empty())
                .map(str::to_string);
            credential.oauth_expires_at = normalize_oauth_expires_at(auth.expires_at);
            credential.oauth_account_id = auth
                .account_id
                .as_deref()
                .map(str::trim)
                .filter(|value| !value.is_empty())
                .map(str::to_string);
        }
        "api-key" => {
            credential.api_key = access_token.to_string();
            clear_oauth_metadata(credential);
        }
        other => {
            return Err(format!(
                "Unsupported auth type '{other}' — expected 'oauth' or 'api-key'"
            ));
        }
    }

    Ok(())
}

fn clear_stored_auth(credential: &mut ProviderCredential) {
    credential.api_key.clear();
    clear_oauth_metadata(credential);
}

async fn sync_credentials_inner(
    credentials: Vec<CredentialEntry>,
    bridge: &DesktopBridge,
) -> Result<(), String> {
    let count = credentials.len();
    bridge
        .stack
        .config
        .update_credentials(|store| {
            for entry in &credentials {
                let key = entry.api_key.trim();
                if is_ignored_api_key(key) {
                    continue;
                }
                // Preserve non-auth fields (base_url, org_id, etc.) but clear stale
                // OAuth metadata so explicit API-key auth fully takes over.
                let mut cred = store
                    .providers
                    .get(&entry.provider)
                    .cloned()
                    .unwrap_or_default();
                cred.api_key = key.to_string();
                clear_oauth_metadata(&mut cred);
                store.set(&entry.provider, cred);
            }
        })
        .await
        .map_err(|e| e.to_string())?;

    bridge
        .stack
        .config
        .save_credentials()
        .await
        .map_err(|e| e.to_string())?;

    refresh_router_credentials(bridge).await;

    debug!(count, "Synced Desktop credentials to secure store");
    Ok(())
}

/// Sync provider API keys from the Desktop frontend into the secure credential store.
///
/// Additive: only sets keys that are provided; does not remove existing entries.
/// Skips empty API keys silently.
#[tauri::command]
pub async fn sync_credentials(
    credentials: Vec<CredentialEntry>,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    sync_credentials_inner(credentials, &bridge).await
}

/// Store provider auth in the secure credential store and refresh the live router cache.
#[tauri::command]
pub async fn store_provider_auth(
    provider: String,
    auth: StoredAuth,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    store_provider_auth_inner(provider, auth, &bridge).await
}

/// Remove provider auth fields from the secure credential store.
#[tauri::command]
pub async fn delete_provider_auth(
    provider: String,
    bridge: State<'_, DesktopBridge>,
) -> Result<(), String> {
    delete_provider_auth_inner(provider, &bridge).await
}

/// Load provider credentials from the secure credential store for the Desktop frontend.
///
/// Returns a list of `{ provider, apiKey }` entries for all providers that have a
/// non-empty API key. OAuth tokens and other sensitive fields are NOT returned —
/// only static API keys that the Desktop settings panel manages.
#[tauri::command]
pub async fn load_credentials(
    bridge: State<'_, DesktopBridge>,
) -> Result<Vec<serde_json::Value>, String> {
    let store = bridge.stack.config.credentials().await;
    let mut entries = Vec::new();
    for provider_name in store.providers() {
        if let Some(cred) = store.providers.get(provider_name) {
            let key = cred.api_key.trim();
            if !key.is_empty() {
                entries.push(serde_json::json!({
                    "provider": provider_name,
                    "apiKey": key,
                }));
            }
        }
    }
    Ok(entries)
}

#[cfg(test)]
mod tests {
    use tempfile::tempdir;

    use crate::bridge::DesktopBridge;

    use super::{
        apply_stored_auth, clear_stored_auth, delete_provider_auth_inner,
        normalize_oauth_expires_at, store_provider_auth_inner, sync_credentials_inner,
        CredentialEntry, StoredAuth,
    };

    #[test]
    fn normalizes_js_millisecond_expiry_to_unix_seconds() {
        assert_eq!(
            normalize_oauth_expires_at(Some(1_762_806_000_000)),
            Some(1_762_806_000)
        );
        assert_eq!(
            normalize_oauth_expires_at(Some(1_762_806_000)),
            Some(1_762_806_000)
        );
        assert_eq!(normalize_oauth_expires_at(None), None);
    }

    #[test]
    fn oauth_auth_clears_stale_api_key_and_sets_oauth_metadata() {
        let mut credential = ava_config::ProviderCredential {
            api_key: "fallback-key".to_string(),
            ..Default::default()
        };

        apply_stored_auth(
            &mut credential,
            &StoredAuth {
                auth_type: "oauth".to_string(),
                access_token: "oauth-access".to_string(),
                refresh_token: Some("oauth-refresh".to_string()),
                expires_at: Some(1_762_806_000_000),
                account_id: Some("acct-123".to_string()),
            },
        )
        .unwrap();

        assert!(credential.api_key.is_empty());
        assert_eq!(credential.oauth_token.as_deref(), Some("oauth-access"));
        assert_eq!(
            credential.oauth_refresh_token.as_deref(),
            Some("oauth-refresh")
        );
        assert_eq!(credential.oauth_expires_at, Some(1_762_806_000));
        assert_eq!(credential.oauth_account_id.as_deref(), Some("acct-123"));
    }

    #[test]
    fn clearing_auth_removes_api_and_oauth_tokens() {
        let mut credential = ava_config::ProviderCredential {
            api_key: "api-key".to_string(),
            oauth_token: Some("oauth-access".to_string()),
            oauth_refresh_token: Some("oauth-refresh".to_string()),
            oauth_expires_at: Some(1_762_806_000),
            oauth_account_id: Some("acct-123".to_string()),
            ..Default::default()
        };

        clear_stored_auth(&mut credential);

        assert!(credential.api_key.is_empty());
        assert!(credential.oauth_token.is_none());
        assert!(credential.oauth_refresh_token.is_none());
        assert!(credential.oauth_expires_at.is_none());
        assert!(credential.oauth_account_id.is_none());
    }

    #[test]
    fn ignores_oauth_placeholder_api_keys() {
        assert!(super::is_ignored_api_key(""));
        assert!(super::is_ignored_api_key("   "));
        assert!(super::is_ignored_api_key("(oauth)"));
        assert!(!super::is_ignored_api_key("sk-live"));
    }

    #[tokio::test]
    async fn store_provider_auth_command_persists_oauth_metadata() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");

        store_provider_auth_inner(
            "openai".to_string(),
            StoredAuth {
                auth_type: "oauth".to_string(),
                access_token: "oauth-access".to_string(),
                refresh_token: Some("oauth-refresh".to_string()),
                expires_at: Some(1_762_806_000_000),
                account_id: Some("acct-123".to_string()),
            },
            &bridge,
        )
        .await
        .expect("oauth auth should store");

        let credentials = bridge.stack.config.credentials().await;
        let credential = credentials
            .providers
            .get("openai")
            .expect("openai credentials should exist");

        assert!(credential.api_key.is_empty());
        assert_eq!(credential.oauth_token.as_deref(), Some("oauth-access"));
        assert_eq!(
            credential.oauth_refresh_token.as_deref(),
            Some("oauth-refresh")
        );
        assert_eq!(credential.oauth_expires_at, Some(1_762_806_000));
        assert_eq!(credential.oauth_account_id.as_deref(), Some("acct-123"));
    }

    #[tokio::test]
    async fn delete_provider_auth_command_clears_existing_credentials() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");

        store_provider_auth_inner(
            "openai".to_string(),
            StoredAuth {
                auth_type: "oauth".to_string(),
                access_token: "oauth-access".to_string(),
                refresh_token: Some("oauth-refresh".to_string()),
                expires_at: Some(1_762_806_000_000),
                account_id: Some("acct-123".to_string()),
            },
            &bridge,
        )
        .await
        .expect("oauth auth should store");

        delete_provider_auth_inner("openai".to_string(), &bridge)
            .await
            .expect("provider auth should delete");

        let credentials = bridge.stack.config.credentials().await;
        let credential = credentials
            .providers
            .get("openai")
            .expect("openai credentials should remain addressable");

        assert!(credential.api_key.is_empty());
        assert!(credential.oauth_token.is_none());
        assert!(credential.oauth_refresh_token.is_none());
        assert!(credential.oauth_expires_at.is_none());
        assert!(credential.oauth_account_id.is_none());
    }

    #[tokio::test]
    async fn sync_credentials_command_clears_stale_oauth_metadata_when_api_key_takes_over() {
        let dir = tempdir().expect("temp dir should be created");
        let bridge = DesktopBridge::init(dir.path().to_path_buf())
            .await
            .expect("bridge should initialize");

        store_provider_auth_inner(
            "openai".to_string(),
            StoredAuth {
                auth_type: "oauth".to_string(),
                access_token: "oauth-access".to_string(),
                refresh_token: Some("oauth-refresh".to_string()),
                expires_at: Some(1_762_806_000_000),
                account_id: Some("acct-123".to_string()),
            },
            &bridge,
        )
        .await
        .expect("oauth auth should store");

        sync_credentials_inner(
            vec![CredentialEntry {
                provider: "openai".to_string(),
                api_key: "sk-live".to_string(),
            }],
            &bridge,
        )
        .await
        .expect("api key sync should store");

        let credentials = bridge.stack.config.credentials().await;
        let credential = credentials
            .providers
            .get("openai")
            .expect("openai credentials should exist");

        assert_eq!(credential.api_key, "sk-live");
        assert!(credential.oauth_token.is_none());
        assert!(credential.oauth_refresh_token.is_none());
        assert!(credential.oauth_expires_at.is_none());
        assert!(credential.oauth_account_id.is_none());
    }
}
