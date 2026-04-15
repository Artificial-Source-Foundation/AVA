pub mod common;
pub mod gateway;

use std::sync::Arc;

use ava_config::CredentialStore;
use ava_plugin::PluginManager;
use ava_types::{AvaError, Result};

use crate::pool::ConnectionPool;
use crate::provider::LLMProvider;

pub mod anthropic;
pub mod copilot;
pub mod gemini;
pub mod inception;
pub mod mock;
pub mod ollama;
pub mod openai;
pub mod openrouter;

pub use anthropic::AnthropicProvider;
pub use gemini::GeminiProvider;
pub use ollama::OllamaProvider;
pub use openai::OpenAIProvider;

use self::copilot::CopilotProvider;
use self::inception::InceptionProvider;
use self::openrouter::OpenRouterProvider;

// Keep select providers constructible internally without promoting their
// concrete types as top-level core exports from this module.

fn normalize_provider_alias(provider_name: &str) -> String {
    match provider_name.to_ascii_lowercase().as_str() {
        "chatgpt" => "openai".to_string(),
        "google" => "gemini".to_string(),
        "alibaba-cn" => "alibaba".to_string(),
        "zhipuai-coding-plan" | "zai-coding-plan" => "zai".to_string(),
        "kimi-for-coding" => "kimi".to_string(),
        "minimax-coding-plan" | "minimax-cn-coding-plan" => "minimax".to_string(),
        other => other.to_string(),
    }
}

fn default_anthropic_compat_base_url(provider_name: &str, normalized: &str) -> &'static str {
    match provider_name.to_ascii_lowercase().as_str() {
        "alibaba-cn" => "https://coding.dashscope.aliyuncs.com/apps/anthropic/v1",
        _ => match normalized {
            "alibaba" => "https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1",
            "kimi" => "https://api.kimi.com/coding/v1",
            "minimax" => "https://api.minimax.io/anthropic/v1",
            _ => unreachable!(),
        },
    }
}

pub fn is_known_provider(provider_name: &str) -> bool {
    matches!(
        normalize_provider_alias(provider_name).as_str(),
        "anthropic"
            | "openai"
            | "openrouter"
            | "inception"
            | "gemini"
            | "copilot"
            | "ollama"
            | "alibaba"
            | "zai"
            | "kimi"
            | "minimax"
    )
}

/// Return the default base URL for a known provider name.
pub fn base_url_for_provider(provider_name: &str) -> Option<&'static str> {
    if provider_name.eq_ignore_ascii_case("alibaba-cn") {
        return Some("https://coding.dashscope.aliyuncs.com/apps/anthropic/v1");
    }

    match normalize_provider_alias(provider_name).as_str() {
        "anthropic" => Some("https://api.anthropic.com"),
        "openai" => Some("https://api.openai.com"),
        "openrouter" => Some("https://openrouter.ai/api"),
        "gemini" => Some("https://generativelanguage.googleapis.com"),
        "copilot" => Some("https://api.individual.githubcopilot.com"),
        "inception" => Some("https://api.inceptionlabs.ai"),
        "ollama" => Some("http://localhost:11434"),
        "alibaba" => Some("https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1"),
        "zai" => Some("https://api.z.ai/api/coding/paas/v4"),
        "kimi" => Some("https://api.kimi.com/coding/v1"),
        "minimax" => Some("https://api.minimax.io/anthropic/v1"),
        _ => None,
    }
}

fn openai_oauth_account_id(entry: &ava_config::ProviderCredential) -> Option<String> {
    entry.oauth_account_id.clone().or_else(|| {
        entry
            .oauth_token
            .as_deref()
            .and_then(ava_auth::tokens::extract_account_id)
    })
}

/// Create a provider by name from credentials, using the shared connection pool.
///
/// For CLI agent providers (e.g., `cli:claude-code`), use a `ProviderFactory`
/// registered on the `ModelRouter` instead — this function only handles API providers.
///
/// # Plugin auth hook point
///
/// Before falling through to the credential store, callers should check whether any
/// plugin provides auth for this provider via the plugin manager:
///
/// ```text
/// // TODO: Wire this into AgentStack or the provider creation call site.
/// // The PluginManager is async and lives on the AgentStack, so the hook
/// // needs to run before this synchronous function is called.
/// //
/// //   let plugin_methods = plugin_manager.get_auth_methods(provider_name).await;
/// //   if !plugin_methods.is_empty() {
/// //       // Let the user pick a method, then:
/// //       let creds = plugin_manager.authorize(provider_name, method_index, user_input).await;
/// //       // Inject creds into the CredentialStore before calling create_provider().
/// //   }
/// //
/// // For token refresh, before each LLM call check expiry:
/// //   if credential.is_oauth_expired() {
/// //       if let Some(refreshed) = plugin_manager.refresh_auth(provider_name, refresh_token).await {
/// //           credential_store.update_from_plugin_creds(refreshed);
/// //       }
/// //   }
/// ```
pub fn create_provider(
    provider_name: &str,
    model: &str,
    credentials: &CredentialStore,
    pool: Arc<ConnectionPool>,
) -> Result<Box<dyn LLMProvider>> {
    if provider_name.starts_with("cli:") {
        return Err(AvaError::ConfigError(format!(
            "CLI provider '{provider_name}' must be registered via ModelRouter::register_factory(). \
             Add ava-cli-providers to your binary crate and register it at startup."
        )));
    }

    // Normalize provider names and legacy aliases (chatgpt->openai,
    // alibaba-cn->alibaba, etc.). Credential lookup tries original name first,
    // then canonical normalized key.
    let normalized = normalize_provider_alias(provider_name);
    let credential = credentials.get(provider_name).or_else(|| {
        if normalized != provider_name {
            credentials.get(&normalized)
        } else {
            None
        }
    });

    match normalized.as_str() {
        "anthropic" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: "anthropic".to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "anthropic".to_string(),
                })?;
            Ok(Box::new(AnthropicProvider::new(pool, api_key, model)))
        }
        "openai" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: "openai".to_string(),
            })?;

            // ChatGPT OAuth uses chatgpt.com/backend-api/codex with JWT access_token.
            // When OAuth is configured, prefer it over any stale desktop API-key
            // value so reconnects immediately route through the ChatGPT backend.
            let has_oauth = entry.is_oauth_configured() && !entry.is_oauth_expired();

            if has_oauth {
                let oauth_token = entry
                    .oauth_token
                    .as_deref()
                    .ok_or_else(|| AvaError::MissingApiKey {
                        provider: "openai (OAuth token missing)".to_string(),
                    })?
                    .to_string();
                let base_url = entry
                    .base_url
                    .clone()
                    .unwrap_or_else(|| "https://chatgpt.com/backend-api/codex".to_string());
                let account_id = openai_oauth_account_id(&entry);
                Ok(Box::new(
                    OpenAIProvider::with_base_url(pool, oauth_token, model, base_url)
                        .with_responses_api(true)
                        .with_subscription(true)
                        .with_provider_label("openai")
                        .with_chatgpt_account_id(account_id),
                ))
            } else {
                // Standard API key — use Chat Completions API
                let api_key = entry
                    .effective_api_key()
                    .ok_or_else(|| {
                        // Provide a more specific error when OAuth is configured but expired
                        if entry.is_oauth_configured() && entry.is_oauth_expired() {
                            AvaError::ConfigError(
                                "OpenAI OAuth token has expired. Reconnect with /connect openai \
                                 or set an API key in ~/.ava/credentials.json"
                                    .to_string(),
                            )
                        } else {
                            AvaError::MissingApiKey {
                                provider: "openai".to_string(),
                            }
                        }
                    })?
                    .to_string();
                let base_url = entry
                    .base_url
                    .clone()
                    .unwrap_or_else(|| "https://api.openai.com".to_string());
                let litellm = entry
                    .litellm_compatible
                    .unwrap_or_else(|| openai::looks_like_litellm_proxy(&base_url));
                Ok(Box::new(
                    OpenAIProvider::with_base_url(pool, api_key, model, base_url)
                        .with_provider_label("openai")
                        .with_litellm_compatible(litellm),
                ))
            }
        }
        "openrouter" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: "openrouter".to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "openrouter".to_string(),
                })?
                .to_string();
            if let Some(base_url) = entry.base_url {
                Ok(Box::new(OpenRouterProvider::with_base_url(
                    pool, api_key, model, base_url,
                )))
            } else {
                Ok(Box::new(OpenRouterProvider::new(pool, api_key, model)))
            }
        }
        "inception" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: "inception".to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "inception".to_string(),
                })?
                .to_string();
            if let Some(base_url) = entry.base_url {
                Ok(Box::new(InceptionProvider::with_base_url(
                    pool, api_key, model, base_url,
                )))
            } else {
                Ok(Box::new(InceptionProvider::new(pool, api_key, model)))
            }
        }
        "gemini" | "google" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: "gemini".to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "gemini".to_string(),
                })?;
            Ok(Box::new(GeminiProvider::new(pool, api_key, model)))
        }
        "copilot" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: "copilot".to_string(),
            })?;
            let oauth_token =
                entry
                    .oauth_token
                    .as_deref()
                    .ok_or_else(|| AvaError::MissingApiKey {
                        provider: "copilot (not connected — use /connect copilot)".to_string(),
                    })?;
            Ok(Box::new(CopilotProvider::new(
                pool,
                oauth_token.to_string(),
                model,
            )))
        }
        "ollama" => {
            let base_url = credential
                .and_then(|entry| entry.base_url)
                .or_else(|| std::env::var("OLLAMA_BASE_URL").ok())
                .unwrap_or_else(|| "http://localhost:11434".to_string());
            Ok(Box::new(OllamaProvider::new(pool, base_url, model)))
        }
        // OpenAI-compatible coding plan providers
        "zai" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: provider_name.to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: provider_name.to_string(),
                })?
                .to_string();
            let default_url = "https://api.z.ai/api/coding/paas/v4";
            let base_url = entry.base_url.as_deref().unwrap_or(default_url);
            let thinking_format = openai::ThinkingFormat::Zhipu;
            Ok(Box::new(
                OpenAIProvider::with_base_url(pool, api_key, model, base_url)
                    .with_thinking_format(thinking_format)
                    .with_provider_label(normalized.clone()),
            ))
        }
        // Anthropic-compatible coding plan providers
        "alibaba" | "kimi" | "minimax" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: provider_name.to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: provider_name.to_string(),
                })?;
            let default_url = default_anthropic_compat_base_url(provider_name, normalized.as_str());
            let base_url = entry.base_url.as_deref().unwrap_or(default_url);
            Ok(Box::new(
                AnthropicProvider::with_base_url(pool, api_key, model, base_url)
                    .with_provider_label(normalized.clone()),
            ))
        }
        _ => Err(AvaError::ProviderError {
            provider: provider_name.to_string(),
            message:
                "unknown provider. Available core providers: anthropic, openai, gemini, ollama, \
                      openrouter, copilot, inception, alibaba, zai, kimi, minimax"
                    .to_string(),
        }),
    }
}

/// Like [`create_provider`] but also attaches `plugin_manager` to providers
/// that support the `request.headers` hook, so plugins can inject custom
/// HTTP headers into every outgoing LLM API request.
///
/// Currently only `AnthropicProvider` (and its compatible aliases) supports
/// the hook — other providers receive the manager but silently ignore it until
/// they opt in.
pub fn create_provider_with_plugins(
    provider_name: &str,
    model: &str,
    credentials: &CredentialStore,
    pool: Arc<ConnectionPool>,
    plugin_manager: Arc<tokio::sync::Mutex<PluginManager>>,
) -> Result<Box<dyn LLMProvider>> {
    let provider = create_provider(provider_name, model, credentials, pool.clone())?;
    // Downcast to AnthropicProvider to attach the plugin manager.
    // For providers that don't yet support the hook, the manager is not attached —
    // it can be added for each provider following the same pattern.
    let normalized = normalize_provider_alias(provider_name);
    match normalized.as_str() {
        "anthropic" | "alibaba" | "kimi" | "minimax" => {
            // Re-create the Anthropic provider with the plugin manager attached.
            // We must re-call the constructor because Box<dyn LLMProvider> doesn't
            // give us back the concrete type.
            let cred = credentials.get(provider_name).or_else(|| {
                if normalized != provider_name {
                    credentials.get(&normalized)
                } else {
                    None
                }
            });
            if let Some(entry) = cred {
                let api_key = entry.effective_api_key().unwrap_or_default().to_string();
                let is_third_party = normalized != "anthropic";
                if is_third_party {
                    let default_url =
                        default_anthropic_compat_base_url(provider_name, normalized.as_str());
                    let base_url = entry.base_url.as_deref().unwrap_or(default_url);
                    let new_pool = pool;
                    return Ok(Box::new(
                        AnthropicProvider::with_base_url(new_pool, api_key, model, base_url)
                            .with_provider_label(normalized.clone())
                            .with_plugin_manager(plugin_manager),
                    ));
                }
                let new_pool = pool;
                return Ok(Box::new(
                    AnthropicProvider::new(new_pool, api_key, model)
                        .with_plugin_manager(plugin_manager),
                ));
            }
            // Credential not found — return the already-created provider without plugin manager.
            Ok(provider)
        }
        _ => Ok(provider),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn default_pool() -> Arc<ConnectionPool> {
        Arc::new(ConnectionPool::new())
    }

    #[test]
    fn unknown_provider_returns_error() {
        let credentials = CredentialStore::default();
        let result = create_provider("unknown-provider", "model", &credentials, default_pool());
        let err = result.err().expect("should fail");
        assert!(err.to_string().contains("unknown provider"));
    }

    #[test]
    fn cli_provider_without_factory_returns_error() {
        let credentials = CredentialStore::default();
        let result = create_provider("cli:claude-code", "sonnet", &credentials, default_pool());
        let err = result.err().expect("should fail");
        assert!(err
            .to_string()
            .contains("must be registered via ModelRouter"));
    }

    #[test]
    fn base_url_for_known_providers() {
        assert_eq!(
            base_url_for_provider("anthropic"),
            Some("https://api.anthropic.com")
        );
        assert_eq!(
            base_url_for_provider("openai"),
            Some("https://api.openai.com")
        );
        assert_eq!(
            base_url_for_provider("openrouter"),
            Some("https://openrouter.ai/api")
        );
        assert_eq!(
            base_url_for_provider("copilot"),
            Some("https://api.individual.githubcopilot.com")
        );
        assert!(base_url_for_provider("unknown").is_none());
    }

    #[test]
    fn base_url_for_provider_normalizes_case() {
        assert_eq!(
            base_url_for_provider("OpenAI"),
            Some("https://api.openai.com")
        );
        assert_eq!(
            base_url_for_provider("ChatGPT"),
            Some("https://api.openai.com")
        );
    }

    #[test]
    fn alibaba_cn_uses_china_region_base_url() {
        assert_eq!(
            base_url_for_provider("alibaba-cn"),
            Some("https://coding.dashscope.aliyuncs.com/apps/anthropic/v1")
        );
    }

    #[test]
    fn removed_long_tail_providers_are_not_known() {
        for provider in [
            "azure",
            "bedrock",
            "xai",
            "mistral",
            "groq",
            "deepseek",
            "fireworks",
        ] {
            assert!(
                !is_known_provider(provider),
                "{provider} should not be known"
            );
        }
    }

    #[test]
    fn copilot_requires_oauth_token() {
        let mut store = CredentialStore::default();
        // Set credential without OAuth token — should fail
        store.set(
            "copilot",
            ava_config::ProviderCredential {
                api_key: String::new(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );
        let result = create_provider("copilot", "gpt-4o", &store, default_pool());
        let err = result.err().expect("should fail without oauth");
        assert!(err.to_string().contains("not connected"));
    }

    #[test]
    fn copilot_creates_with_oauth_token() {
        let mut store = CredentialStore::default();
        store.set(
            "copilot",
            ava_config::ProviderCredential {
                api_key: String::new(),
                base_url: None,
                org_id: None,
                oauth_token: Some("gho_test_token".to_string()),
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );
        let provider = create_provider("copilot", "gpt-4o", &store, default_pool())
            .expect("copilot provider should be created");
        assert_eq!(provider.model_name(), "gpt-4o");
    }

    fn mock_creds_for(providers: &[&str]) -> CredentialStore {
        let mut store = CredentialStore::default();
        for &p in providers {
            store.set(
                p,
                ava_config::ProviderCredential {
                    api_key: "test-key-12345".to_string(),
                    base_url: None,
                    org_id: None,
                    oauth_token: None,
                    oauth_refresh_token: None,
                    oauth_expires_at: None,
                    oauth_account_id: None,
                    litellm_compatible: None,
                    loop_prone: None,
                },
            );
        }
        store
    }

    #[test]
    fn all_api_providers_create_successfully() {
        let providers_and_models = [
            ("anthropic", "claude-sonnet-4"),
            ("openai", "gpt-4.1"),
            ("chatgpt", "gpt-4.1"),
            ("openrouter", "anthropic/claude-sonnet-4"),
            ("inception", "mercury-2"),
            ("gemini", "gemini-2.5-pro"),
            ("google", "gemini-2.5-pro"),
            ("ollama", "llama3.3"),
            ("alibaba", "qwen3.5-plus"),
            ("alibaba-cn", "qwen3.5-plus"),
            ("zai", "glm-4.7"),
            ("zhipuai-coding-plan", "glm-4.7"),
            ("kimi", "k2p5"),
            ("kimi-for-coding", "k2p5"),
            ("minimax", "MiniMax-M2"),
            ("minimax-coding-plan", "MiniMax-M2"),
            ("minimax-cn-coding-plan", "MiniMax-M2"),
        ];

        let provider_names: Vec<&str> = providers_and_models.iter().map(|(p, _)| *p).collect();
        let creds = mock_creds_for(&provider_names);
        let pool = default_pool();

        for (provider, model) in providers_and_models {
            let result = create_provider(provider, model, &creds, pool.clone());
            assert!(
                result.is_ok(),
                "Failed to create provider {provider}: {:?}",
                result.err()
            );
        }
    }

    #[test]
    fn all_routable_providers_have_base_url() {
        let expected = [
            "anthropic",
            "openai",
            "chatgpt",
            "openrouter",
            "inception",
            "gemini",
            "google",
            "copilot",
            "ollama",
            "alibaba",
            "alibaba-cn",
            "zai",
            "zhipuai-coding-plan",
            "kimi",
            "kimi-for-coding",
            "minimax",
            "minimax-coding-plan",
            "minimax-cn-coding-plan",
        ];
        for provider in expected {
            assert!(
                base_url_for_provider(provider).is_some(),
                "Missing base_url_for_provider entry: {provider}"
            );
        }
    }

    #[test]
    fn alibaba_creates_anthropic_compatible_provider() {
        let creds = mock_creds_for(&["alibaba"]);
        let provider = create_provider("alibaba", "qwen3.5-plus", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "qwen3.5-plus");
        assert!(provider.supports_tools());
    }

    #[test]
    fn zai_creates_openai_provider_with_correct_model() {
        let creds = mock_creds_for(&["zai"]);
        let provider = create_provider("zai", "glm-4.7", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "glm-4.7");
        assert!(provider.supports_tools());
    }

    #[test]
    fn kimi_creates_anthropic_compatible_provider() {
        let creds = mock_creds_for(&["kimi"]);
        let provider = create_provider("kimi", "k2p5", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "k2p5");
        assert!(provider.supports_tools());
    }

    #[test]
    fn minimax_creates_anthropic_compatible_provider() {
        let creds = mock_creds_for(&["minimax"]);
        let provider = create_provider("minimax", "MiniMax-M2", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "MiniMax-M2");
        assert!(provider.supports_tools());
    }

    #[test]
    fn openai_oauth_uses_chatgpt_backend_url() {
        // When OpenAI credential has only an OAuth token (no API key),
        // the provider should route to the ChatGPT backend API.
        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ava_config::ProviderCredential {
                api_key: String::new(),
                base_url: None,
                org_id: None,
                oauth_token: Some("oauth-access-token".to_string()),
                oauth_refresh_token: Some("refresh-token".to_string()),
                oauth_expires_at: Some(u64::MAX), // far future
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );
        let provider = create_provider("openai", "codex-mini", &store, default_pool())
            .expect("should create OpenAI provider with OAuth");
        assert_eq!(provider.model_name(), "codex-mini");
    }

    #[test]
    fn openai_oauth_is_preferred_over_stale_api_key() {
        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ava_config::ProviderCredential {
                api_key: "sk-stale-desktop-key".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: Some("oauth-access-token".to_string()),
                oauth_refresh_token: Some("refresh-token".to_string()),
                oauth_expires_at: Some(u64::MAX),
                oauth_account_id: Some("acct-live".to_string()),
                litellm_compatible: None,
                loop_prone: None,
            },
        );

        let provider = create_provider("openai", "codex-mini", &store, default_pool())
            .expect("should prefer OAuth over stale API key");

        assert_eq!(provider.model_name(), "codex-mini");
    }

    #[test]
    fn openai_oauth_account_id_falls_back_to_oauth_token_claim() {
        let token = concat!(
            "e30.",
            "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoiYWNjdC1mcm9tLWFjY2Vzcy10b2tlbiJ9fQ.",
            "c2ln"
        )
        .to_string();

        let entry = ava_config::ProviderCredential {
            api_key: String::new(),
            base_url: None,
            org_id: None,
            oauth_token: Some(token),
            oauth_refresh_token: Some("refresh-token".to_string()),
            oauth_expires_at: Some(u64::MAX),
            oauth_account_id: None,
            litellm_compatible: None,
            loop_prone: None,
        };

        assert_eq!(
            openai_oauth_account_id(&entry).as_deref(),
            Some("acct-from-access-token")
        );
    }

    #[test]
    fn openai_api_key_uses_standard_url() {
        // When OpenAI credential has a regular API key (no OAuth),
        // it should use api.openai.com.
        let creds = mock_creds_for(&["openai"]);
        let provider = create_provider("openai", "gpt-4.1", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "gpt-4.1");
    }

    #[test]
    fn inception_creates_provider_with_correct_model() {
        let creds = mock_creds_for(&["inception"]);
        let provider = create_provider("inception", "mercury-2", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "mercury-2");
        assert!(provider.supports_tools());
        assert!(!provider.supports_thinking());
    }

    #[test]
    fn inception_resolves_mercury_coder_alias() {
        let creds = mock_creds_for(&["inception"]);
        let provider =
            create_provider("inception", "mercury-coder", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "mercury-coder-small");
    }

    #[test]
    fn removed_long_tail_provider_errors_are_clear() {
        for provider in [
            "fireworks",
            "xai",
            "mistral",
            "groq",
            "deepseek",
            "azure",
            "bedrock",
        ] {
            let creds = mock_creds_for(&[provider]);
            let err = create_provider(provider, "model", &creds, default_pool())
                .err()
                .expect("long-tail provider should not be routable");
            assert!(err.to_string().contains("unknown provider"));
        }
    }

    #[test]
    fn minimax_cn_creates_anthropic_compatible_provider() {
        let creds = mock_creds_for(&["minimax-cn-coding-plan"]);
        let provider = create_provider(
            "minimax-cn-coding-plan",
            "MiniMax-M2",
            &creds,
            default_pool(),
        )
        .unwrap();
        assert_eq!(provider.model_name(), "MiniMax-M2");
        assert!(provider.supports_tools());
    }
}
