pub mod common;

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
pub use copilot::CopilotProvider;
pub use gemini::GeminiProvider;
pub use inception::InceptionProvider;
pub use mock::MockProvider;
pub use ollama::OllamaProvider;
pub use openai::OpenAIProvider;
pub use openrouter::OpenRouterProvider;

/// Return the default base URL for a known provider name.
pub fn base_url_for_provider(provider_name: &str) -> Option<&'static str> {
    match provider_name.to_ascii_lowercase().as_str() {
        "anthropic" => Some("https://api.anthropic.com"),
        "openai" => Some("https://api.openai.com"),
        "chatgpt" => Some("https://chatgpt.com/backend-api/codex"),
        "openrouter" => Some("https://openrouter.ai/api"),
        "gemini" => Some("https://generativelanguage.googleapis.com"),
        "copilot" => Some("https://api.individual.githubcopilot.com"),
        "inception" => Some("https://api.inceptionlabs.ai"),
        "ollama" => Some("http://localhost:11434"),
        "alibaba" => Some("https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1"),
        "alibaba-cn" => Some("https://coding.dashscope.aliyuncs.com/apps/anthropic/v1"),
        "zai-coding-plan" => Some("https://api.z.ai/api/coding/paas/v4"),
        "zhipuai-coding-plan" => Some("https://open.bigmodel.cn/api/coding/paas/v4"),
        "kimi-for-coding" => Some("https://api.kimi.com/coding/v1"),
        "minimax-coding-plan" => Some("https://api.minimax.io/anthropic/v1"),
        "minimax-cn-coding-plan" => Some("https://api.minimaxi.com/anthropic/v1"),
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
/// ```rust,ignore
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

    // Normalize provider name to lowercase for matching. This handles cases where
    // the UI or config sends "ChatGPT" instead of "chatgpt", "OpenAI" instead of
    // "openai", etc. Credential lookup tries original name first, then normalized.
    let normalized = provider_name.to_ascii_lowercase();
    let credential = credentials.get(provider_name).or_else(|| {
        if normalized != provider_name {
            credentials.get(&normalized)
        } else {
            None
        }
    });

    match normalized.as_str() {
        "anthropic" => {
            let entry = credential
                .ok_or_else(|| AvaError::MissingApiKey {
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
            let entry = credential
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "openai".to_string(),
                })?;

            // ChatGPT OAuth uses chatgpt.com/backend-api/codex with JWT access_token.
            // When the user has an OAuth token but NO API key, route to the
            // ChatGPT Responses API instead of api.openai.com.
            let has_api_key = !entry.api_key.trim().is_empty();
            let has_oauth = entry.is_oauth_configured() && !entry.is_oauth_expired();

            if !has_api_key && has_oauth {
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
                    .unwrap_or_else(|| {
                        "https://chatgpt.com/backend-api/codex".to_string()
                    });
                let account_id = openai_oauth_account_id(&entry);
                Ok(Box::new(
                    OpenAIProvider::with_base_url(pool, oauth_token, model, base_url)
                        .with_responses_api(true)
                        .with_subscription(true)
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
                let litellm = entry.litellm_compatible.unwrap_or_else(|| {
                    openai::looks_like_litellm_proxy(&base_url)
                });
                Ok(Box::new(
                    OpenAIProvider::with_base_url(pool, api_key, model, base_url)
                        .with_litellm_compatible(litellm),
                ))
            }
        }
        "openrouter" => {
            let entry = credential
                .ok_or_else(|| AvaError::MissingApiKey {
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
            let entry = credential
                .ok_or_else(|| AvaError::MissingApiKey {
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
        "gemini" => {
            let entry = credential
                .ok_or_else(|| AvaError::MissingApiKey {
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
            let oauth_token = entry
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
        // ChatGPT — explicit provider alias for the Responses API.
        // Users can also configure OAuth under the "openai" provider name
        // (auto-detected from OAuth-only credentials).
        "chatgpt" => {
            let entry = credential
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "chatgpt".to_string(),
                })?;
            let oauth_token = entry
                .oauth_token
                .as_deref()
                .or_else(|| {
                    let key = entry.api_key.trim();
                    if key.is_empty() { None } else { Some(key) }
                })
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "chatgpt (not connected — configure OAuth or set token)".to_string(),
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
                    .with_chatgpt_account_id(account_id),
            ))
        }
        // OpenAI-compatible coding plan providers
        "zai-coding-plan" | "zhipuai-coding-plan" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: provider_name.to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: provider_name.to_string(),
                })?
                .to_string();
            let default_url = match normalized.as_str() {
                "zai-coding-plan" => "https://api.z.ai/api/coding/paas/v4",
                "zhipuai-coding-plan" => "https://open.bigmodel.cn/api/coding/paas/v4",
                _ => unreachable!(),
            };
            let base_url = entry.base_url.as_deref().unwrap_or(default_url);
            let thinking_format = match normalized.as_str() {
                "zai-coding-plan" | "zhipuai-coding-plan" => openai::ThinkingFormat::Zhipu,
                _ => unreachable!(),
            };
            Ok(Box::new(
                OpenAIProvider::with_base_url(pool, api_key, model, base_url)
                    .with_thinking_format(thinking_format),
            ))
        }
        // Anthropic-compatible coding plan providers
        "alibaba" | "alibaba-cn" | "kimi-for-coding" | "minimax-coding-plan" | "minimax-cn-coding-plan" => {
            let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
                provider: provider_name.to_string(),
            })?;
            let api_key = entry
                .effective_api_key()
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: provider_name.to_string(),
                })?;
            let default_url = match normalized.as_str() {
                "alibaba" => "https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1",
                "alibaba-cn" => "https://coding.dashscope.aliyuncs.com/apps/anthropic/v1",
                "kimi-for-coding" => "https://api.kimi.com/coding/v1",
                "minimax-coding-plan" => "https://api.minimax.io/anthropic/v1",
                "minimax-cn-coding-plan" => "https://api.minimaxi.com/anthropic/v1",
                _ => unreachable!(),
            };
            let base_url = entry
                .base_url
                .as_deref()
                .unwrap_or(default_url);
            Ok(Box::new(AnthropicProvider::with_base_url(
                pool, api_key, model, base_url,
            )))
        }
        _ => Err(AvaError::ProviderError {
            provider: provider_name.to_string(),
            message: "unknown provider. Available: anthropic, openai, chatgpt, openrouter, inception, copilot, gemini, ollama, \
                      alibaba, alibaba-cn, zai-coding-plan, zhipuai-coding-plan, kimi-for-coding, \
                      minimax-coding-plan, minimax-cn-coding-plan"
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
    let normalized = provider_name.to_ascii_lowercase();
    match normalized.as_str() {
        "anthropic"
        | "alibaba"
        | "alibaba-cn"
        | "kimi-for-coding"
        | "minimax-coding-plan"
        | "minimax-cn-coding-plan" => {
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
                    let default_url = match normalized.as_str() {
                        "alibaba" => "https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1",
                        "alibaba-cn" => "https://coding.dashscope.aliyuncs.com/apps/anthropic/v1",
                        "kimi-for-coding" => "https://api.kimi.com/coding/v1",
                        "minimax-coding-plan" => "https://api.minimax.io/anthropic/v1",
                        "minimax-cn-coding-plan" => "https://api.minimaxi.com/anthropic/v1",
                        _ => unreachable!(),
                    };
                    let base_url = entry.base_url.as_deref().unwrap_or(default_url);
                    let new_pool = pool;
                    return Ok(Box::new(
                        AnthropicProvider::with_base_url(new_pool, api_key, model, base_url)
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
            Some("https://chatgpt.com/backend-api/codex")
        );
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
            ("openrouter", "anthropic/claude-sonnet-4"),
            ("inception", "mercury-2"),
            ("gemini", "gemini-2.5-pro"),
            ("ollama", "llama3.3"),
            ("alibaba", "qwen3.5-plus"),
            ("alibaba-cn", "qwen3.5-plus"),
            ("zai-coding-plan", "glm-4.7"),
            ("zhipuai-coding-plan", "glm-4.7"),
            ("kimi-for-coding", "k2p5"),
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
            "openrouter",
            "inception",
            "gemini",
            "copilot",
            "ollama",
            "alibaba",
            "alibaba-cn",
            "zai-coding-plan",
            "zhipuai-coding-plan",
            "kimi-for-coding",
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
        let creds = mock_creds_for(&["zai-coding-plan"]);
        let provider =
            create_provider("zai-coding-plan", "glm-4.7", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "glm-4.7");
        assert!(provider.supports_tools());
    }

    #[test]
    fn kimi_creates_anthropic_compatible_provider() {
        let creds = mock_creds_for(&["kimi-for-coding"]);
        let provider = create_provider("kimi-for-coding", "k2p5", &creds, default_pool()).unwrap();
        assert_eq!(provider.model_name(), "k2p5");
        assert!(provider.supports_tools());
    }

    #[test]
    fn minimax_creates_anthropic_compatible_provider() {
        let creds = mock_creds_for(&["minimax-coding-plan"]);
        let provider =
            create_provider("minimax-coding-plan", "MiniMax-M2", &creds, default_pool()).unwrap();
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
            },
        );
        let provider = create_provider("openai", "codex-mini", &store, default_pool())
            .expect("should create OpenAI provider with OAuth");
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
