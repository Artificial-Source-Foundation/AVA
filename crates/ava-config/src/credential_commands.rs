use std::future::Future;
use std::path::Path;

use ava_types::{AvaError, Result};

use crate::credentials::{CredentialStore, ProviderCredential};

/// CLI-callable credential operations.
pub enum CredentialCommand {
    /// Set a provider's API key.
    Set {
        provider: String,
        api_key: String,
        base_url: Option<String>,
    },
    /// Remove a provider's credentials.
    Remove { provider: String },
    /// List all configured providers (redacted keys).
    List,
    /// Test a provider's credentials.
    Test { provider: String },
}

/// Execute a credential command against a mutable store.
pub async fn execute_credential_command(
    cmd: CredentialCommand,
    store: &mut CredentialStore,
) -> Result<String> {
    execute_internal(cmd, store, None, |provider, credentials| async move {
        default_tester(&provider, &credentials).await
    })
    .await
}

/// Execute credential command with an injected tester implementation.
pub async fn execute_credential_command_with_tester<F, Fut>(
    cmd: CredentialCommand,
    store: &mut CredentialStore,
    tester: F,
) -> Result<String>
where
    F: Fn(String, CredentialStore) -> Fut,
    Fut: Future<Output = Result<String>>,
{
    execute_internal(cmd, store, None, tester).await
}

#[cfg(test)]
async fn execute_with_path(
    cmd: CredentialCommand,
    store: &mut CredentialStore,
    save_path: Option<&Path>,
) -> Result<String> {
    execute_internal(cmd, store, save_path, |provider, credentials| async move {
        default_tester(&provider, &credentials).await
    })
    .await
}

async fn execute_internal<F, Fut>(
    cmd: CredentialCommand,
    store: &mut CredentialStore,
    save_path: Option<&Path>,
    tester: F,
) -> Result<String>
where
    F: Fn(String, CredentialStore) -> Fut,
    Fut: Future<Output = Result<String>>,
{
    match cmd {
        CredentialCommand::Set {
            provider,
            api_key,
            base_url,
        } => {
            store.set(
                &provider,
                ProviderCredential {
                    api_key,
                    base_url,
                    org_id: None,
                    oauth_token: None,
                    oauth_refresh_token: None,
                    oauth_expires_at: None,
                    oauth_account_id: None,
                    litellm_compatible: None,
                },
            );

            if let Some(path) = save_path {
                store.save(path).await?;
            } else {
                store.save_default().await?;
            }

            Ok(format!("{} API key saved", provider_name(&provider)))
        }
        CredentialCommand::Remove { provider } => {
            if store.remove(&provider) {
                if let Some(path) = save_path {
                    store.save(path).await?;
                } else {
                    store.save_default().await?;
                }
                Ok(format!("{} credentials removed", provider_name(&provider)))
            } else {
                Ok(format!(
                    "{} credentials were not configured",
                    provider_name(&provider)
                ))
            }
        }
        CredentialCommand::List => {
            let providers = store.providers();
            if providers.is_empty() {
                return Ok("No providers configured".to_string());
            }

            let lines = providers
                .iter()
                .filter_map(|provider| {
                    store.get(provider).map(|credential| {
                        let key_display = if credential.is_oauth_configured() {
                            let token = credential.oauth_token.as_deref().unwrap_or("");
                            format!("OAuth ({})", redact_key(token))
                        } else {
                            redact_key(&credential.api_key)
                        };
                        let mut line = format!("{}: {}", provider_name(provider), key_display,);
                        if let Some(ref base_url) = credential.base_url {
                            line.push_str(&format!(" (base_url: {base_url})"));
                        }
                        line
                    })
                })
                .collect::<Vec<_>>();

            Ok(lines.join("\n"))
        }
        CredentialCommand::Test { provider } => tester(provider, store.clone()).await,
    }
}

async fn default_tester(provider: &str, store: &CredentialStore) -> Result<String> {
    let credential = store.get(provider).ok_or_else(|| {
        AvaError::ConfigError(format!(
            "{} credentials are not configured",
            provider_name(provider)
        ))
    })?;

    if provider == "ollama" {
        let endpoint = credential
            .base_url
            .unwrap_or_else(|| "http://localhost:11434".to_string());
        return Ok(format!("ollama: OK ({endpoint})"));
    }

    if credential.api_key.trim().is_empty() {
        return Err(AvaError::ConfigError(format!(
            "{} API key is empty",
            provider_name(provider)
        )));
    }

    Ok(format!("{provider}: OK (credentials configured)"))
}

pub fn provider_name(provider: &str) -> String {
    match provider {
        "openai" => "OpenAI".to_string(),
        "openrouter" => "OpenRouter".to_string(),
        "ollama" => "Ollama".to_string(),
        "anthropic" => "Anthropic".to_string(),
        "gemini" => "Gemini".to_string(),
        "copilot" => "GitHub Copilot".to_string(),
        "mistral" => "Mistral".to_string(),
        "groq" => "Groq".to_string(),
        "xai" => "xAI (Grok)".to_string(),
        "deepinfra" => "DeepInfra".to_string(),
        "together" => "Together AI".to_string(),
        "cerebras" => "Cerebras".to_string(),
        "perplexity" => "Perplexity".to_string(),
        "cohere" => "Cohere".to_string(),
        "azure" => "Azure OpenAI".to_string(),
        "bedrock" => "AWS Bedrock".to_string(),
        _ => {
            let mut chars = provider.chars();
            if let Some(first) = chars.next() {
                format!("{}{}", first.to_ascii_uppercase(), chars.as_str())
            } else {
                provider.to_string()
            }
        }
    }
}

pub fn redact_key(key: &str) -> String {
    let chars = key.chars().collect::<Vec<_>>();
    if chars.len() <= 8 {
        "****".to_string()
    } else {
        let prefix = chars[..4].iter().collect::<String>();
        let suffix = chars[chars.len() - 4..].iter().collect::<String>();
        format!("{prefix}...{suffix}")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[tokio::test]
    async fn set_and_list_redacts_key() {
        let mut store = CredentialStore::default();
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("credentials.json");

        execute_with_path(
            CredentialCommand::Set {
                provider: "openai".to_string(),
                api_key: "sk-1234567890abcd".to_string(),
                base_url: None,
            },
            &mut store,
            Some(&path),
        )
        .await
        .unwrap();

        let listed = execute_with_path(CredentialCommand::List, &mut store, Some(&path))
            .await
            .unwrap();
        assert!(listed.contains("OpenAI: sk-1...abcd"));
    }

    #[tokio::test]
    async fn remove_then_list_empty() {
        let mut store = CredentialStore::default();
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("credentials.json");

        store.set(
            "openrouter",
            ProviderCredential {
                api_key: "key-12345678".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
                litellm_compatible: None,
            },
        );

        execute_with_path(
            CredentialCommand::Remove {
                provider: "openrouter".to_string(),
            },
            &mut store,
            Some(&path),
        )
        .await
        .unwrap();

        let listed = execute_with_path(CredentialCommand::List, &mut store, Some(&path))
            .await
            .unwrap();
        assert_eq!(listed, "No providers configured");
    }

    #[test]
    fn redact_key_handles_short_and_long_values() {
        assert_eq!(redact_key("abc"), "****");
        assert_eq!(redact_key("12345678"), "****");
        assert_eq!(redact_key("123456789"), "1234...6789");
        assert_eq!(redact_key("sk-abcdefghijkl"), "sk-a...ijkl");
    }

    #[tokio::test]
    async fn test_command_reports_missing_provider() {
        let mut store = CredentialStore::default();
        let err = execute_credential_command(
            CredentialCommand::Test {
                provider: "anthropic".to_string(),
            },
            &mut store,
        )
        .await
        .unwrap_err();

        assert!(err
            .to_string()
            .contains("Anthropic credentials are not configured"));
    }

    #[tokio::test]
    async fn test_command_with_mock_tester() {
        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ProviderCredential {
                api_key: "sk-live-test".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
                litellm_compatible: None,
            },
        );

        let result = execute_credential_command_with_tester(
            CredentialCommand::Test {
                provider: "openai".to_string(),
            },
            &mut store,
            |provider, _store| async move { Ok(format!("{provider}: OK (mock tester)")) },
        )
        .await
        .unwrap();

        assert_eq!(result, "openai: OK (mock tester)");
    }
}
