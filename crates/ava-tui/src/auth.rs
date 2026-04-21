//! `ava auth` CLI subcommand — manage provider authentication.

use ava_auth::config::AuthFlow;
use ava_config::{
    execute_credential_command, provider_name, redact_key, CredentialCommand, CredentialStore,
};
use color_eyre::Result;
use std::io::{self, IsTerminal, Write};

use crate::config::cli::AuthCommand;

pub async fn run_auth(cmd: AuthCommand) -> Result<()> {
    match cmd {
        AuthCommand::Login { provider } => auth_login(&provider).await,
        AuthCommand::Logout { provider } => auth_logout(&provider).await,
        AuthCommand::List => auth_list().await,
        AuthCommand::Test { provider } => auth_test(&provider).await,
    }
}

async fn auth_login(provider_id: &str) -> Result<()> {
    let info = ava_auth::provider_info(provider_id).ok_or_else(|| {
        color_eyre::eyre::eyre!(
            "Unknown provider: {provider_id}\nAvailable: {}",
            ava_auth::all_providers()
                .iter()
                .map(|p| p.id)
                .collect::<Vec<_>>()
                .join(", ")
        )
    })?;

    println!("Signing in to {}...", info.name);

    let flow = select_auth_flow(info)?;

    match flow {
        AuthFlow::Pkce => {
            let result = ava_auth::authenticate_with_flow(provider_id, AuthFlow::Pkce).await;
            match result {
                Ok(ava_auth::AuthResult::OAuth(tokens)) => {
                    store_oauth_tokens(provider_id, tokens).await?;
                    println!("{}: Connected via OAuth", provider_name(provider_id));
                }
                Ok(_) => unreachable!("PKCE flow should return OAuth result"),
                Err(err) => {
                    eprintln!("Authentication failed: {err}");
                    std::process::exit(1);
                }
            }
        }
        AuthFlow::OpenAiHeadless => {
            let cfg = ava_auth::config::oauth_config(provider_id)
                .ok_or_else(|| color_eyre::eyre::eyre!("No OAuth config for {provider_id}"))?;

            let device = ava_auth::openai_headless::request_code(cfg.client_id).await?;
            println!();
            println!("  Enter this code: {}", device.user_code);
            println!("  Visit: {}", device.verification_uri);
            println!();

            let _ = ava_auth::browser::open_browser(&device.verification_uri);

            println!("Waiting for authorization...");

            match ava_auth::openai_headless::poll_code(
                cfg.client_id,
                &device.device_code,
                &device.user_code,
                device.interval,
            )
            .await?
            {
                Some(tokens) => {
                    store_oauth_tokens(provider_id, tokens).await?;
                    println!(
                        "{}: Connected via headless ChatGPT login",
                        provider_name(provider_id)
                    );
                }
                None => {
                    eprintln!("Headless login expired. Please try again.");
                    std::process::exit(1);
                }
            }
        }
        AuthFlow::DeviceCode => {
            let cfg = ava_auth::config::oauth_config(provider_id)
                .ok_or_else(|| color_eyre::eyre::eyre!("No OAuth config for {provider_id}"))?;

            let device = ava_auth::device_code::request_device_code(cfg).await?;
            println!();
            println!("  Enter this code: {}", device.user_code);
            println!("  Visit: {}", device.verification_uri);
            println!();

            // Try to open browser
            let _ = ava_auth::browser::open_browser(&device.verification_uri);

            println!("Waiting for authorization...");

            match ava_auth::device_code::poll_device_code(
                cfg,
                &device.device_code,
                device.interval,
                device.expires_in,
            )
            .await?
            {
                Some(tokens) => {
                    store_oauth_tokens(provider_id, tokens).await?;
                    println!("{}: Connected via device code", provider_name(provider_id));
                }
                None => {
                    eprintln!("Device code expired. Please try again.");
                    std::process::exit(1);
                }
            }
        }
        AuthFlow::ApiKey => {
            // Prompt for API key
            print!("Enter API key for {}: ", info.name);
            io::stdout().flush()?;

            let mut api_key = String::new();
            io::stdin().read_line(&mut api_key)?;
            let api_key = api_key.trim().to_string();

            if api_key.is_empty() {
                eprintln!("API key cannot be empty");
                std::process::exit(1);
            }

            let mut store = CredentialStore::load_default().await.unwrap_or_default();
            execute_credential_command(
                CredentialCommand::Set {
                    provider: provider_id.to_string(),
                    api_key,
                    base_url: None,
                },
                &mut store,
            )
            .await?;

            println!("{}: API key saved", provider_name(provider_id));
        }
    }

    Ok(())
}

async fn store_oauth_tokens(
    provider_id: &str,
    tokens: ava_auth::tokens::OAuthTokens,
) -> Result<()> {
    let mut store = CredentialStore::load_default().await.unwrap_or_default();
    store.set_oauth_tokens(provider_id, &tokens);
    store.save_default().await?;
    Ok(())
}

fn select_auth_flow(info: &ava_auth::ProviderInfo) -> Result<AuthFlow> {
    select_auth_flow_with_input(info, io::stdin().is_terminal(), || {
        let mut input = String::new();
        io::stdin().read_line(&mut input)?;
        Ok(input)
    })
}

fn select_auth_flow_with_input<F>(
    info: &ava_auth::ProviderInfo,
    stdin_is_terminal: bool,
    read_input: F,
) -> Result<AuthFlow>
where
    F: FnOnce() -> io::Result<String>,
{
    if info.auth_flows.len() <= 1 {
        return Ok(info.primary_flow());
    }

    if !stdin_is_terminal {
        return Ok(info.primary_flow());
    }

    println!("Choose an auth method:");
    for (idx, flow) in info.auth_flows.iter().enumerate() {
        println!("  {}. {}", idx + 1, auth_flow_label(*flow));
    }
    print!("Select [1-{}] (default 1): ", info.auth_flows.len());
    io::stdout().flush()?;

    let input = read_input()?;
    resolve_auth_flow_selection(info, stdin_is_terminal, Some(input.trim()))
}

fn resolve_auth_flow_selection(
    info: &ava_auth::ProviderInfo,
    stdin_is_terminal: bool,
    selection: Option<&str>,
) -> Result<AuthFlow> {
    if info.auth_flows.len() <= 1 {
        return Ok(info.primary_flow());
    }

    if !stdin_is_terminal {
        return Ok(info.primary_flow());
    }

    let trimmed = selection.unwrap_or("").trim();
    if trimmed.is_empty() {
        return Ok(info.primary_flow());
    }

    let selection = trimmed.parse::<usize>().map_err(|_| {
        color_eyre::eyre::eyre!(
            "Invalid auth method selection: {trimmed}. Enter a number between 1 and {}.",
            info.auth_flows.len()
        )
    })?;
    info.auth_flows
        .get(selection.saturating_sub(1))
        .copied()
        .ok_or_else(|| {
            color_eyre::eyre::eyre!(
                "Invalid auth method selection: {selection}. Enter a number between 1 and {}.",
                info.auth_flows.len()
            )
        })
}

fn auth_flow_label(flow: AuthFlow) -> &'static str {
    match flow {
        AuthFlow::Pkce => "ChatGPT Pro/Plus (browser)",
        AuthFlow::OpenAiHeadless => "ChatGPT Pro/Plus (headless)",
        AuthFlow::ApiKey => "Manually enter API key",
        AuthFlow::DeviceCode => "Device code",
    }
}

#[cfg(test)]
mod tests {
    use super::{resolve_auth_flow_selection, select_auth_flow_with_input};
    use ava_auth::config::AuthFlow;

    #[test]
    fn non_interactive_openai_login_defaults_to_primary_flow() {
        let info = ava_auth::provider_info("openai").expect("openai provider metadata");
        let selected = select_auth_flow_with_input(info, false, || {
            panic!("non-interactive flow selection should not read stdin")
        })
        .expect("flow selection");
        assert_eq!(selected, AuthFlow::Pkce);
    }

    #[test]
    fn interactive_openai_login_accepts_headless_choice() {
        let info = ava_auth::provider_info("openai").expect("openai provider metadata");
        let selected =
            resolve_auth_flow_selection(info, true, Some("2")).expect("headless flow selection");
        assert_eq!(selected, AuthFlow::OpenAiHeadless);
    }
}

async fn auth_logout(provider_id: &str) -> Result<()> {
    let mut store = CredentialStore::load_default().await.unwrap_or_default();
    let msg = execute_credential_command(
        CredentialCommand::Remove {
            provider: provider_id.to_string(),
        },
        &mut store,
    )
    .await?;
    println!("{msg}");
    Ok(())
}

async fn auth_list() -> Result<()> {
    let store = CredentialStore::load_default().await.unwrap_or_default();

    println!("{:<18} {:<12} STATUS", "PROVIDER", "AUTH");
    println!("{}", "-".repeat(60));

    for info in ava_auth::all_providers() {
        let credential = store.get(info.id);
        let (status, auth_type) = match &credential {
            Some(c) if c.is_oauth_configured() => {
                let token = c.oauth_token.as_deref().unwrap_or("");
                (format!("OAuth ({})", redact_key(token)), "oauth")
            }
            Some(c) if !c.api_key.trim().is_empty() => (redact_key(&c.api_key), "api_key"),
            Some(c) if info.id == "ollama" && c.base_url.is_some() => {
                (c.base_url.clone().unwrap_or_default(), "local")
            }
            _ => ("not configured".to_string(), "-"),
        };

        let configured = credential
            .as_ref()
            .is_some_and(|c| !c.api_key.trim().is_empty() || c.is_oauth_configured());
        let icon = if configured || (info.id == "ollama") {
            "\u{2713}"
        } else {
            "\u{2717}"
        };

        println!("{icon} {:<16} {:<12} {}", info.name, auth_type, status,);
    }

    Ok(())
}

async fn auth_test(provider_id: &str) -> Result<()> {
    let mut store = CredentialStore::load_default().await.unwrap_or_default();
    let msg = execute_credential_command(
        CredentialCommand::Test {
            provider: provider_id.to_string(),
        },
        &mut store,
    )
    .await?;
    println!("{msg}");
    Ok(())
}
