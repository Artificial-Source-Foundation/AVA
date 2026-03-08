pub mod common;

use std::sync::Arc;

use ava_config::CredentialStore;
use ava_types::{AvaError, Result};

use crate::pool::ConnectionPool;
use crate::provider::LLMProvider;

pub mod anthropic;
pub mod gemini;
pub mod mock;
pub mod ollama;
pub mod openai;
pub mod openrouter;

pub use anthropic::AnthropicProvider;
pub use gemini::GeminiProvider;
pub use mock::MockProvider;
pub use ollama::OllamaProvider;
pub use openai::OpenAIProvider;
pub use openrouter::OpenRouterProvider;

/// Return the default base URL for a known provider name.
pub fn base_url_for_provider(provider_name: &str) -> Option<&'static str> {
    match provider_name {
        "anthropic" => Some("https://api.anthropic.com"),
        "openai" => Some("https://api.openai.com"),
        "openrouter" => Some("https://openrouter.ai/api"),
        "gemini" => Some("https://generativelanguage.googleapis.com"),
        "ollama" => Some("http://localhost:11434"),
        _ => None,
    }
}

/// Create a provider by name from credentials, using the shared connection pool.
///
/// For CLI agent providers (e.g., `cli:claude-code`), use a `ProviderFactory`
/// registered on the `ModelRouter` instead — this function only handles API providers.
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

    let credential = credentials.get(provider_name);

    match provider_name {
        "anthropic" => {
            let api_key = credential
                .as_ref()
                .map(|entry| entry.api_key.as_str())
                .filter(|value| !value.trim().is_empty())
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "anthropic".to_string(),
                })?;
            Ok(Box::new(AnthropicProvider::new(pool, api_key, model)))
        }
        "openai" => {
            let entry = credential
                .filter(|entry| !entry.api_key.trim().is_empty())
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "openai".to_string(),
                })?;
            if let Some(base_url) = entry.base_url {
                Ok(Box::new(OpenAIProvider::with_base_url(
                    pool,
                    entry.api_key,
                    model,
                    base_url,
                )))
            } else {
                Ok(Box::new(OpenAIProvider::new(pool, entry.api_key, model)))
            }
        }
        "openrouter" => {
            let entry = credential
                .filter(|entry| !entry.api_key.trim().is_empty())
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "openrouter".to_string(),
                })?;
            if let Some(base_url) = entry.base_url {
                Ok(Box::new(OpenRouterProvider::with_base_url(
                    pool,
                    entry.api_key,
                    model,
                    base_url,
                )))
            } else {
                Ok(Box::new(OpenRouterProvider::new(pool, entry.api_key, model)))
            }
        }
        "gemini" => {
            let api_key = credential
                .as_ref()
                .map(|entry| entry.api_key.as_str())
                .filter(|value| !value.trim().is_empty())
                .ok_or_else(|| AvaError::MissingApiKey {
                    provider: "gemini".to_string(),
                })?;
            Ok(Box::new(GeminiProvider::new(pool, api_key, model)))
        }
        "ollama" => {
            let base_url = credential
                .and_then(|entry| entry.base_url)
                .or_else(|| std::env::var("OLLAMA_BASE_URL").ok())
                .unwrap_or_else(|| "http://localhost:11434".to_string());
            Ok(Box::new(OllamaProvider::new(pool, base_url, model)))
        }
        _ => Err(AvaError::ProviderError {
            provider: provider_name.to_string(),
            message: "unknown provider. Available: anthropic, openai, openrouter, gemini, ollama"
                .to_string(),
        }),
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
        assert!(err.to_string().contains("must be registered via ModelRouter"));
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
        assert!(base_url_for_provider("unknown").is_none());
    }
}
