//! `ava auth` CLI subcommand — manage provider authentication.

use ava_auth::config::AuthFlow;
use ava_config::{
    execute_credential_command, provider_name, redact_key, CredentialCommand, CredentialStore,
    ProviderCredential,
};
use color_eyre::Result;
use std::io::{self, Write};

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

    match info.primary_flow() {
        AuthFlow::Pkce => {
            let result = ava_auth::authenticate(provider_id).await;
            match result {
                Ok(ava_auth::AuthResult::OAuth(tokens)) => {
                    // Store OAuth tokens
                    let mut store = CredentialStore::load_default().await.unwrap_or_default();
                    let account_id = tokens
                        .id_token
                        .as_deref()
                        .and_then(ava_auth::tokens::extract_account_id);

                    store.set(
                        provider_id,
                        ProviderCredential {
                            api_key: String::new(),
                            base_url: None,
                            org_id: None,
                            oauth_token: Some(tokens.access_token),
                            oauth_refresh_token: tokens.refresh_token,
                            oauth_expires_at: tokens.expires_at,
                            oauth_account_id: account_id,
                            litellm_compatible: None,
                        },
                    );
                    store.save_default().await?;
                    println!("{}: Connected via OAuth", provider_name(provider_id));
                }
                Ok(_) => unreachable!("PKCE flow should return OAuth result"),
                Err(err) => {
                    eprintln!("Authentication failed: {err}");
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
                    let mut store = CredentialStore::load_default().await.unwrap_or_default();
                    store.set(
                        provider_id,
                        ProviderCredential {
                            api_key: String::new(),
                            base_url: None,
                            org_id: None,
                            oauth_token: Some(tokens.access_token),
                            oauth_refresh_token: tokens.refresh_token,
                            oauth_expires_at: tokens.expires_at,
                            oauth_account_id: None,
                            litellm_compatible: None,
                        },
                    );
                    store.save_default().await?;
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
