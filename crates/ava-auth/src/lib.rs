//! OAuth and provider authentication for AVA.
//!
//! Shared crate used by both the CLI/TUI and Desktop app.
//! Supports PKCE browser login, device code flow, and API key management.

pub mod browser;
pub mod callback;
pub mod config;
pub mod device_code;
pub mod pkce;
pub mod tokens;

use config::{oauth_config, AuthFlow};
use device_code::DeviceCodeResponse;
use tokens::OAuthTokens;

/// Result of starting an authentication flow.
#[derive(Debug)]
pub enum AuthResult {
    /// OAuth tokens obtained (PKCE flow completed).
    OAuth(OAuthTokens),
    /// Device code flow started — caller must display user_code and poll.
    DeviceCodePending(DeviceCodeResponse),
    /// Provider uses API key — caller should prompt the user.
    NeedsApiKey { env_var: Option<String> },
}

/// Authentication errors.
#[derive(Debug, thiserror::Error)]
pub enum AuthError {
    #[error("Unknown provider: {0}")]
    UnknownProvider(String),
    #[error("No OAuth config for provider: {0}")]
    NoOAuthConfig(String),
    #[error("CSRF state mismatch — possible attack")]
    StateMismatch,
    #[error("OAuth callback timed out")]
    CallbackTimeout,
    #[error("Token exchange failed: {0}")]
    TokenExchange(String),
    #[error("Device code expired")]
    DeviceCodeExpired,
    #[error("Token refresh failed: {0}")]
    RefreshFailed(String),
    #[error("Network error: {0}")]
    Network(String),
    #[error("Browser open failed: {0}")]
    BrowserOpen(String),
    #[error("{0}")]
    Other(String),
}

/// Provider metadata for auth flow selection.
#[derive(Debug, Clone)]
pub struct ProviderInfo {
    pub id: &'static str,
    pub name: &'static str,
    pub auth_flow: AuthFlow,
    pub env_var: Option<&'static str>,
    pub default_base_url: Option<&'static str>,
}

/// All supported providers with their auth metadata.
pub fn all_providers() -> &'static [ProviderInfo] {
    &[
        // OAuth providers (browser login)
        ProviderInfo {
            id: "openai",
            name: "OpenAI",
            auth_flow: AuthFlow::Pkce,
            env_var: Some("OPENAI_API_KEY"),
            default_base_url: Some("https://api.openai.com/v1"),
        },
        ProviderInfo {
            id: "copilot",
            name: "GitHub Copilot",
            auth_flow: AuthFlow::DeviceCode,
            env_var: None,
            default_base_url: None,
        },
        // API key providers
        ProviderInfo {
            id: "anthropic",
            name: "Anthropic",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("ANTHROPIC_API_KEY"),
            default_base_url: Some("https://api.anthropic.com"),
        },
        ProviderInfo {
            id: "openrouter",
            name: "OpenRouter",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("OPENROUTER_API_KEY"),
            default_base_url: Some("https://openrouter.ai/api/v1"),
        },
        ProviderInfo {
            id: "gemini",
            name: "Google Gemini",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("GEMINI_API_KEY"),
            default_base_url: None,
        },
        ProviderInfo {
            id: "mistral",
            name: "Mistral",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("MISTRAL_API_KEY"),
            default_base_url: Some("https://api.mistral.ai/v1"),
        },
        ProviderInfo {
            id: "groq",
            name: "Groq",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("GROQ_API_KEY"),
            default_base_url: Some("https://api.groq.com/openai/v1"),
        },
        ProviderInfo {
            id: "xai",
            name: "xAI (Grok)",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("XAI_API_KEY"),
            default_base_url: Some("https://api.x.ai/v1"),
        },
        ProviderInfo {
            id: "deepinfra",
            name: "DeepInfra",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("DEEPINFRA_API_KEY"),
            default_base_url: Some("https://api.deepinfra.com/v1/openai"),
        },
        ProviderInfo {
            id: "together",
            name: "Together AI",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("TOGETHER_API_KEY"),
            default_base_url: Some("https://api.together.xyz/v1"),
        },
        ProviderInfo {
            id: "cerebras",
            name: "Cerebras",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("CEREBRAS_API_KEY"),
            default_base_url: Some("https://api.cerebras.ai/v1"),
        },
        ProviderInfo {
            id: "perplexity",
            name: "Perplexity",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("PERPLEXITY_API_KEY"),
            default_base_url: Some("https://api.perplexity.ai"),
        },
        ProviderInfo {
            id: "cohere",
            name: "Cohere",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("COHERE_API_KEY"),
            default_base_url: Some("https://api.cohere.ai/v1"),
        },
        ProviderInfo {
            id: "azure",
            name: "Azure OpenAI",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("AZURE_OPENAI_API_KEY"),
            default_base_url: None,
        },
        ProviderInfo {
            id: "bedrock",
            name: "AWS Bedrock",
            auth_flow: AuthFlow::ApiKey,
            env_var: Some("AWS_BEARER_TOKEN_BEDROCK"),
            default_base_url: None,
        },
        // Local providers (no auth required)
        ProviderInfo {
            id: "ollama",
            name: "Ollama (local)",
            auth_flow: AuthFlow::ApiKey,
            env_var: None,
            default_base_url: Some("http://localhost:11434"),
        },
    ]
}

/// Look up provider info by ID.
pub fn provider_info(id: &str) -> Option<&'static ProviderInfo> {
    all_providers().iter().find(|p| p.id == id)
}

/// Start the appropriate auth flow for a provider.
pub async fn authenticate(provider_id: &str) -> Result<AuthResult, AuthError> {
    let info = provider_info(provider_id)
        .ok_or_else(|| AuthError::UnknownProvider(provider_id.to_string()))?;

    match info.auth_flow {
        AuthFlow::Pkce => {
            let cfg = oauth_config(provider_id)
                .ok_or_else(|| AuthError::NoOAuthConfig(provider_id.to_string()))?;
            let pkce_params = pkce::generate_pkce();
            let auth_url = config::build_auth_url(cfg, &pkce_params);

            // Start callback server before opening browser (avoid race)
            let callback_fut =
                callback::listen_for_callback(cfg.redirect_port, cfg.redirect_path, 120);

            browser::open_browser(&auth_url)?;

            let cb = callback_fut.await?;

            // Validate CSRF state
            if cb.state != pkce_params.state {
                return Err(AuthError::StateMismatch);
            }

            let oauth_tokens =
                tokens::exchange_code_for_tokens(cfg, &cb.code, &pkce_params).await?;
            Ok(AuthResult::OAuth(oauth_tokens))
        }
        AuthFlow::DeviceCode => {
            let cfg = oauth_config(provider_id)
                .ok_or_else(|| AuthError::NoOAuthConfig(provider_id.to_string()))?;
            let device = device_code::request_device_code(cfg).await?;
            Ok(AuthResult::DeviceCodePending(device))
        }
        AuthFlow::ApiKey => Ok(AuthResult::NeedsApiKey {
            env_var: info.env_var.map(String::from),
        }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_providers_has_expected_count() {
        assert_eq!(all_providers().len(), 16);
    }

    #[test]
    fn provider_info_lookup() {
        let info = provider_info("openai").unwrap();
        assert_eq!(info.name, "OpenAI");
        assert_eq!(info.auth_flow, AuthFlow::Pkce);

        let info = provider_info("copilot").unwrap();
        assert_eq!(info.auth_flow, AuthFlow::DeviceCode);

        let info = provider_info("anthropic").unwrap();
        assert_eq!(info.auth_flow, AuthFlow::ApiKey);

        assert!(provider_info("nonexistent").is_none());
    }

    #[test]
    fn all_provider_ids_are_unique() {
        let ids: Vec<&str> = all_providers().iter().map(|p| p.id).collect();
        let mut deduped = ids.clone();
        deduped.sort();
        deduped.dedup();
        assert_eq!(ids.len(), deduped.len());
    }
}
