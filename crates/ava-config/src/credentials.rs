use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use tokio::fs;

#[cfg(unix)]
use std::os::unix::fs::PermissionsExt;

/// Per-provider credential entry.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProviderCredential {
    /// API key or token.
    pub api_key: String,
    /// Optional base URL override (e.g., custom OpenAI-compatible endpoint).
    pub base_url: Option<String>,
    /// Optional organization ID (OpenAI).
    pub org_id: Option<String>,
}

/// Credential store loaded from ~/.ava/credentials.json.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CredentialStore {
    /// Provider name -> credential mapping.
    pub providers: HashMap<String, ProviderCredential>,
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
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .await
                .map_err(|error| AvaError::IoError(error.to_string()))?;
        }

        let content = serde_json::to_string_pretty(self)
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        fs::write(path, content)
            .await
            .map_err(|error| AvaError::IoError(error.to_string()))?;

        #[cfg(unix)]
        {
            let perms = std::fs::Permissions::from_mode(0o600);
            std::fs::set_permissions(path, perms)
                .map_err(|error| AvaError::IoError(error.to_string()))?;
        }

        Ok(())
    }

    pub async fn save_default(&self) -> Result<()> {
        self.save(&Self::default_path()?).await
    }

    /// Get credential for provider. Env vars override file credentials.
    ///
    /// Lookup order:
    /// 1) AVA_<PROVIDER>_API_KEY
    /// 2) Standard provider env var (e.g. OPENAI_API_KEY)
    /// 3) File credential
    pub fn get(&self, provider: &str) -> Option<ProviderCredential> {
        let provider_key = provider.to_ascii_uppercase().replace('-', "_");
        let ava_env = format!("AVA_{provider_key}_API_KEY");

        if let Ok(api_key) = std::env::var(&ava_env) {
            let api_key = api_key.trim().to_string();
            if !api_key.is_empty() && !is_placeholder_key(&api_key) {
                let mut credential = self.providers.get(provider).cloned().unwrap_or_else(|| {
                    ProviderCredential {
                        api_key: String::new(),
                        base_url: None,
                        org_id: None,
                    }
                });
                credential.api_key = api_key;
                return Some(credential);
            }
        }

        if let Some(standard_env) = standard_env_var(provider) {
            if let Ok(api_key) = std::env::var(standard_env) {
                let api_key = api_key.trim().to_string();
                if !api_key.is_empty() && !is_placeholder_key(&api_key) {
                    let mut credential = self.providers.get(provider).cloned().unwrap_or_else(|| {
                        ProviderCredential {
                            api_key: String::new(),
                            base_url: None,
                            org_id: None,
                        }
                    });
                    credential.api_key = api_key;
                    return Some(credential);
                }
            }
        }

        self.providers.get(provider).cloned()
    }

    pub fn set(&mut self, provider: &str, credential: ProviderCredential) {
        self.providers.insert(provider.to_string(), credential);
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
                let has_base_url = credential.base_url.is_some();
                if has_key || (provider == "ollama" && has_base_url) {
                    configured.insert(provider);
                }
            }
        }

        for &provider in known_providers() {
            if let Some(credential) = self.get(provider) {
                let has_key = !credential.api_key.trim().is_empty();
                let has_base_url = credential.base_url.is_some();
                if has_key || (provider == "ollama" && has_base_url) {
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

fn known_providers() -> &'static [&'static str] {
    &["anthropic", "openai", "openrouter", "gemini", "ollama"]
}

fn standard_env_var(provider: &str) -> Option<&'static str> {
    match provider {
        "anthropic" => Some("ANTHROPIC_API_KEY"),
        "openai" => Some("OPENAI_API_KEY"),
        "openrouter" => Some("OPENROUTER_API_KEY"),
        "gemini" => Some("GEMINI_API_KEY"),
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
    use std::sync::Mutex;
    use tempfile::TempDir;

    static ENV_LOCK: Mutex<()> = Mutex::new(());

    fn sample_credential(key: &str) -> ProviderCredential {
        ProviderCredential {
            api_key: key.to_string(),
            base_url: None,
            org_id: None,
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
    fn env_var_fallback() {
        let _guard = ENV_LOCK.lock().unwrap();
        let mut store = CredentialStore::default();
        store.set("envcredtest", sample_credential("file-key"));

        let key = "AVA_ENVCREDTEST_API_KEY";
        std::env::set_var(key, "env-key");

        let credential = store.get("envcredtest").unwrap();
        assert_eq!(credential.api_key, "env-key");

        std::env::remove_var(key);
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
}
