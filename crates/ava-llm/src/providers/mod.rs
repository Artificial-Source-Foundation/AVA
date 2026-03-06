mod common;

use ava_config::CredentialStore;
use ava_types::{AvaError, Result};

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

/// Create a provider by name from credentials.
///
/// For CLI agent providers (e.g., `cli:claude-code`), use a `ProviderFactory`
/// registered on the `ModelRouter` instead — this function only handles API providers.
pub fn create_provider(
    provider_name: &str,
    model: &str,
    credentials: &CredentialStore,
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
                .ok_or_else(|| {
                    AvaError::ConfigError(
                        "No Anthropic API key. Set AVA_ANTHROPIC_API_KEY or add to ~/.ava/credentials.json"
                            .to_string(),
                    )
                })?;
            Ok(Box::new(AnthropicProvider::new(api_key, model)))
        }
        "openai" => {
            let entry = credential
                .filter(|entry| !entry.api_key.trim().is_empty())
                .ok_or_else(|| {
                    AvaError::ConfigError(
                        "No OpenAI API key. Set AVA_OPENAI_API_KEY or add to ~/.ava/credentials.json"
                            .to_string(),
                    )
                })?;
            if let Some(base_url) = entry.base_url {
                Ok(Box::new(OpenAIProvider::with_base_url(
                    entry.api_key,
                    model,
                    base_url,
                )))
            } else {
                Ok(Box::new(OpenAIProvider::new(entry.api_key, model)))
            }
        }
        "openrouter" => {
            let entry = credential
                .filter(|entry| !entry.api_key.trim().is_empty())
                .ok_or_else(|| {
                    AvaError::ConfigError(
                        "No OpenRouter API key. Set AVA_OPENROUTER_API_KEY or add to ~/.ava/credentials.json"
                            .to_string(),
                    )
                })?;
            if let Some(base_url) = entry.base_url {
                Ok(Box::new(OpenRouterProvider::with_base_url(
                    entry.api_key,
                    model,
                    base_url,
                )))
            } else {
                Ok(Box::new(OpenRouterProvider::new(entry.api_key, model)))
            }
        }
        "gemini" => {
            let api_key = credential
                .as_ref()
                .map(|entry| entry.api_key.as_str())
                .filter(|value| !value.trim().is_empty())
                .ok_or_else(|| {
                    AvaError::ConfigError(
                        "No Gemini API key. Set AVA_GEMINI_API_KEY or add to ~/.ava/credentials.json"
                            .to_string(),
                    )
                })?;
            Ok(Box::new(GeminiProvider::new(api_key, model)))
        }
        "ollama" => {
            let base_url = credential
                .and_then(|entry| entry.base_url)
                .or_else(|| std::env::var("OLLAMA_BASE_URL").ok())
                .unwrap_or_else(|| "http://localhost:11434".to_string());
            Ok(Box::new(OllamaProvider::new(base_url, model)))
        }
        _ => Err(AvaError::ConfigError(format!(
            "Unknown provider: {provider_name}"
        ))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unknown_provider_returns_error() {
        let credentials = CredentialStore::default();
        let result = create_provider("unknown-provider", "model", &credentials);
        let err = result.err().expect("should fail");
        assert!(err.to_string().contains("Unknown provider"));
    }

    #[test]
    fn cli_provider_without_factory_returns_error() {
        let credentials = CredentialStore::default();
        let result = create_provider("cli:claude-code", "sonnet", &credentials);
        let err = result.err().expect("should fail");
        assert!(err.to_string().contains("must be registered via ModelRouter"));
    }
}
