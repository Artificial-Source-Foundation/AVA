//! OAuth provider configurations.
//!
//! Hardcoded OAuth configs for providers that support browser login or device code flows.

use crate::pkce::PkceParams;

/// Authentication flow type.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AuthFlow {
    /// PKCE browser-based OAuth (e.g., OpenAI).
    Pkce,
    /// Device code flow (e.g., GitHub Copilot).
    DeviceCode,
    /// Simple API key (most providers).
    ApiKey,
}

/// OAuth configuration for a provider.
#[derive(Debug, Clone)]
pub struct OAuthConfig {
    pub client_id: &'static str,
    pub authorization_url: &'static str,
    pub token_url: &'static str,
    pub scopes: &'static [&'static str],
    pub redirect_port: u16,
    pub redirect_path: &'static str,
    pub extra_params: &'static [(&'static str, &'static str)],
    pub flow: AuthFlow,
}

/// OpenAI PKCE OAuth configuration.
static OPENAI_CONFIG: OAuthConfig = OAuthConfig {
    client_id: "app_EMoamEEZ73f0CkXaXp7hrann",
    authorization_url: "https://auth.openai.com/oauth/authorize",
    token_url: "https://auth.openai.com/oauth/token",
    scopes: &["openid", "profile", "email", "offline_access"],
    redirect_port: 1455,
    redirect_path: "/auth/callback",
    extra_params: &[
        ("id_token_add_organizations", "true"),
        ("codex_cli_simplified_flow", "true"),
        ("originator", "ava_cli"),
    ],
    flow: AuthFlow::Pkce,
};

/// GitHub Copilot device code configuration.
static COPILOT_CONFIG: OAuthConfig = OAuthConfig {
    client_id: "Iv1.b507a08c87ecfe98",
    authorization_url: "https://github.com/login/device/code",
    token_url: "https://github.com/login/oauth/access_token",
    scopes: &["read:user"],
    redirect_port: 0,
    redirect_path: "",
    extra_params: &[],
    flow: AuthFlow::DeviceCode,
};

/// Get the OAuth config for a provider, if it uses OAuth.
pub fn oauth_config(provider: &str) -> Option<&'static OAuthConfig> {
    match provider {
        "openai" => Some(&OPENAI_CONFIG),
        "copilot" => Some(&COPILOT_CONFIG),
        _ => None,
    }
}

/// Build the full authorization URL for a PKCE flow.
///
/// Uses `url::Url` for safe URL construction instead of string concatenation.
pub fn build_auth_url(config: &OAuthConfig, pkce: &PkceParams) -> String {
    let redirect_uri = format!(
        "http://localhost:{}{}",
        config.redirect_port, config.redirect_path
    );
    let scope = config.scopes.join(" ");

    let mut url = url::Url::parse(config.authorization_url)
        .expect("Static authorization_url must be a valid URL");

    {
        let mut query = url.query_pairs_mut();
        query.append_pair("response_type", "code");
        query.append_pair("client_id", config.client_id);
        query.append_pair("redirect_uri", &redirect_uri);
        query.append_pair("scope", &scope);
        query.append_pair("state", &pkce.state);
        query.append_pair("code_challenge", &pkce.challenge);
        query.append_pair("code_challenge_method", "S256");

        for &(key, value) in config.extra_params {
            query.append_pair(key, value);
        }
    }

    url.to_string()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pkce::generate_pkce;

    #[test]
    fn openai_config_is_pkce() {
        let cfg = oauth_config("openai").unwrap();
        assert_eq!(cfg.flow, AuthFlow::Pkce);
        assert_eq!(cfg.redirect_port, 1455);
        assert!(cfg.authorization_url.contains("auth.openai.com"));
    }

    #[test]
    fn copilot_config_is_device_code() {
        let cfg = oauth_config("copilot").unwrap();
        assert_eq!(cfg.flow, AuthFlow::DeviceCode);
        assert!(cfg.authorization_url.contains("github.com"));
    }

    #[test]
    fn unknown_provider_returns_none() {
        assert!(oauth_config("anthropic").is_none());
        assert!(oauth_config("openrouter").is_none());
    }

    #[test]
    fn build_auth_url_contains_required_params() {
        let cfg = oauth_config("openai").unwrap();
        let pkce = generate_pkce();
        let url = build_auth_url(cfg, &pkce);

        assert!(url.starts_with("https://auth.openai.com/oauth/authorize?"));
        assert!(url.contains("response_type=code"));
        assert!(url.contains("client_id=app_EMoamEEZ73f0CkXaXp7hrann"));
        assert!(url.contains("code_challenge_method=S256"));
        assert!(url.contains(&format!("state={}", pkce.state)));
        assert!(url.contains(&format!("code_challenge={}", pkce.challenge)));
        assert!(url.contains("originator=ava_cli"));
    }
}
