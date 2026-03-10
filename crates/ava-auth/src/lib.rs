//! OAuth and provider authentication for AVA.
//!
//! Shared crate used by both the CLI/TUI and Desktop app.
//! Supports PKCE browser login, device code flow, and API key management.

pub mod browser;
pub mod callback;
pub mod config;
pub mod copilot;
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

/// Provider group for display.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProviderGroup {
    Popular,
    Other,
}

/// Provider metadata for auth flow selection.
#[derive(Debug, Clone)]
pub struct ProviderInfo {
    pub id: &'static str,
    pub name: &'static str,
    /// Description shown next to name (e.g., "ChatGPT Plus/Pro or API key").
    pub description: &'static str,
    /// Supported auth flows. First is the default.
    pub auth_flows: &'static [AuthFlow],
    pub env_var: Option<&'static str>,
    pub default_base_url: Option<&'static str>,
    pub group: ProviderGroup,
}

impl ProviderInfo {
    /// The credential store key (always the provider id).
    pub fn cred_key(&self) -> &'static str {
        self.id
    }

    /// Primary auth flow.
    pub fn primary_flow(&self) -> AuthFlow {
        self.auth_flows.first().copied().unwrap_or(AuthFlow::ApiKey)
    }

    /// Whether this provider supports multiple auth methods.
    pub fn has_multiple_flows(&self) -> bool {
        self.auth_flows.len() > 1
    }
}

/// All supported providers with their auth metadata.
pub fn all_providers() -> &'static [ProviderInfo] {
    &[
        // Popular providers
        ProviderInfo {
            id: "openai",
            name: "OpenAI",
            description: "ChatGPT Plus/Pro or API key",
            auth_flows: &[AuthFlow::ApiKey, AuthFlow::Pkce],
            env_var: Some("OPENAI_API_KEY"),
            default_base_url: Some("https://api.openai.com/v1"),
            group: ProviderGroup::Popular,
        },
        ProviderInfo {
            id: "copilot",
            name: "GitHub Copilot",
            description: "Free with Copilot subscription",
            auth_flows: &[AuthFlow::DeviceCode],
            env_var: None,
            default_base_url: Some("https://api.individual.githubcopilot.com"),
            group: ProviderGroup::Popular,
        },
        ProviderInfo {
            id: "anthropic",
            name: "Anthropic",
            description: "Claude API key",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("ANTHROPIC_API_KEY"),
            default_base_url: Some("https://api.anthropic.com"),
            group: ProviderGroup::Popular,
        },
        ProviderInfo {
            id: "openrouter",
            name: "OpenRouter",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("OPENROUTER_API_KEY"),
            default_base_url: Some("https://openrouter.ai/api/v1"),
            group: ProviderGroup::Popular,
        },
        ProviderInfo {
            id: "gemini",
            name: "Google Gemini",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("GEMINI_API_KEY"),
            default_base_url: None,
            group: ProviderGroup::Popular,
        },
        // Other providers
        ProviderInfo {
            id: "mistral",
            name: "Mistral",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("MISTRAL_API_KEY"),
            default_base_url: Some("https://api.mistral.ai/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "groq",
            name: "Groq",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("GROQ_API_KEY"),
            default_base_url: Some("https://api.groq.com/openai/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "xai",
            name: "xAI (Grok)",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("XAI_API_KEY"),
            default_base_url: Some("https://api.x.ai/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "deepinfra",
            name: "DeepInfra",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("DEEPINFRA_API_KEY"),
            default_base_url: Some("https://api.deepinfra.com/v1/openai"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "together",
            name: "Together AI",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("TOGETHER_API_KEY"),
            default_base_url: Some("https://api.together.xyz/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "cerebras",
            name: "Cerebras",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("CEREBRAS_API_KEY"),
            default_base_url: Some("https://api.cerebras.ai/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "perplexity",
            name: "Perplexity",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("PERPLEXITY_API_KEY"),
            default_base_url: Some("https://api.perplexity.ai"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "cohere",
            name: "Cohere",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("COHERE_API_KEY"),
            default_base_url: Some("https://api.cohere.ai/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "azure",
            name: "Azure OpenAI",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("AZURE_OPENAI_API_KEY"),
            default_base_url: None,
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "bedrock",
            name: "AWS Bedrock",
            description: "",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("AWS_BEARER_TOKEN_BEDROCK"),
            default_base_url: None,
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "ollama",
            name: "Ollama",
            description: "local",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: None,
            default_base_url: Some("http://localhost:11434"),
            group: ProviderGroup::Other,
        },
        // Coding plan providers
        ProviderInfo {
            id: "alibaba",
            name: "Alibaba Model Studio",
            description: "Alibaba Cloud Coding Plan (International)",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("DASHSCOPE_API_KEY"),
            default_base_url: Some("https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "alibaba-cn",
            name: "Alibaba (China)",
            description: "Alibaba Cloud Coding Plan (China mainland)",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("DASHSCOPE_API_KEY"),
            default_base_url: Some("https://coding.dashscope.aliyuncs.com/apps/anthropic/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "zai-coding-plan",
            name: "Z.AI Coding Plan",
            description: "ZhipuAI coding subscription (z.ai)",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("ZHIPU_API_KEY"),
            default_base_url: Some("https://api.z.ai/api/coding/paas/v4"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "zhipuai-coding-plan",
            name: "ZhipuAI Coding Plan",
            description: "ZhipuAI coding subscription (bigmodel.cn)",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("ZHIPU_API_KEY"),
            default_base_url: Some("https://open.bigmodel.cn/api/coding/paas/v4"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "kimi-for-coding",
            name: "Kimi For Coding",
            description: "Moonshot Kimi coding subscription",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("KIMI_API_KEY"),
            default_base_url: Some("https://api.kimi.com/coding/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "minimax-coding-plan",
            name: "MiniMax Coding Plan",
            description: "MiniMax coding subscription (minimax.io)",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("MINIMAX_API_KEY"),
            default_base_url: Some("https://api.minimax.io/anthropic/v1"),
            group: ProviderGroup::Other,
        },
        ProviderInfo {
            id: "minimax-cn-coding-plan",
            name: "MiniMax CN Coding Plan",
            description: "MiniMax coding subscription (minimaxi.com)",
            auth_flows: &[AuthFlow::ApiKey],
            env_var: Some("MINIMAX_API_KEY"),
            default_base_url: Some("https://api.minimaxi.com/anthropic/v1"),
            group: ProviderGroup::Other,
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

    match info.primary_flow() {
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
        assert_eq!(all_providers().len(), 23);
    }

    #[test]
    fn provider_info_lookup() {
        let info = provider_info("openai").unwrap();
        assert_eq!(info.name, "OpenAI");
        assert!(info.has_multiple_flows());
        assert_eq!(info.primary_flow(), AuthFlow::ApiKey);
        assert!(info.auth_flows.contains(&AuthFlow::Pkce));

        let info = provider_info("copilot").unwrap();
        assert_eq!(info.primary_flow(), AuthFlow::DeviceCode);

        let info = provider_info("anthropic").unwrap();
        assert_eq!(info.primary_flow(), AuthFlow::ApiKey);
        assert!(!info.has_multiple_flows());

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

    #[test]
    fn popular_providers_come_first() {
        let providers = all_providers();
        let first_other = providers.iter().position(|p| p.group == ProviderGroup::Other).unwrap();
        for p in &providers[..first_other] {
            assert_eq!(p.group, ProviderGroup::Popular);
        }
    }
}
