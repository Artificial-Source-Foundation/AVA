use ava_auth::config::AuthFlow;
use ava_config::{known_providers, provider_name, redact_key, standard_env_var, CredentialStore};
use ratatui::prelude::*;
use ratatui::widgets::Paragraph;

use crate::app::AppState;

/// Provider status for display.
#[derive(Debug, Clone)]
pub struct ProviderStatus {
    pub id: String,
    pub display_name: String,
    pub configured: bool,
    pub is_local: bool,
    pub auth_flow: AuthFlow,
    pub redacted_key: Option<String>,
    pub env_var_hint: Option<&'static str>,
}

/// Which screen the provider connect modal is showing.
#[derive(Debug, Clone)]
pub enum ConnectScreen {
    /// Provider list with status.
    List,
    /// API key input for a specific provider.
    Configure(String),
    /// Browser OAuth in progress (PKCE flow).
    OAuthBrowser {
        provider_id: String,
        auth_url: String,
        elapsed_secs: u64,
    },
    /// Device code display (e.g., GitHub Copilot).
    DeviceCode {
        provider_id: String,
        user_code: String,
        verification_uri: String,
        elapsed_secs: u64,
    },
}

/// Which field is active in the configure screen.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectField {
    ApiKey,
    BaseUrl,
}

/// State for the provider connect modal.
#[derive(Debug, Clone)]
pub struct ProviderConnectState {
    pub screen: ConnectScreen,
    pub selected: usize,
    pub providers: Vec<ProviderStatus>,
    pub key_input: String,
    pub base_url_input: String,
    pub active_field: ConnectField,
    pub message: Option<String>,
}

impl ProviderConnectState {
    /// Build provider list from credential store.
    pub fn from_credentials(credentials: &CredentialStore) -> Self {
        let providers = build_provider_list(credentials);
        Self {
            screen: ConnectScreen::List,
            selected: 0,
            providers,
            key_input: String::new(),
            base_url_input: String::new(),
            active_field: ConnectField::ApiKey,
            message: None,
        }
    }

    /// Build for a specific provider (from `/connect <provider>`).
    pub fn for_provider(credentials: &CredentialStore, provider: &str) -> Self {
        let providers = build_provider_list(credentials);
        let base_url = credentials
            .get(provider)
            .and_then(|c| c.base_url.clone())
            .unwrap_or_default();
        Self {
            screen: ConnectScreen::Configure(provider.to_string()),
            selected: 0,
            providers,
            key_input: String::new(),
            base_url_input: base_url,
            active_field: ConnectField::ApiKey,
            message: None,
        }
    }

    /// Masked display of the key input.
    pub fn masked_key(&self) -> String {
        if self.key_input.is_empty() {
            String::new()
        } else {
            "\u{25CF}".repeat(self.key_input.len())
        }
    }
}

/// Render the provider connect modal.
pub fn render_provider_connect(frame: &mut Frame<'_>, area: Rect, state: &AppState) {
    let pc = match state.provider_connect {
        Some(ref s) => s,
        None => return,
    };

    match &pc.screen {
        ConnectScreen::List => {
            let mut lines = vec![
                Line::from(Span::styled(
                    "Provider Status",
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
            ];

            for (idx, provider) in pc.providers.iter().enumerate() {
                let is_selected = idx == pc.selected;
                let (icon, icon_color) = if provider.is_local {
                    ("\u{25CF}", state.theme.primary)  // ● for local
                } else if provider.configured {
                    ("\u{2713}", state.theme.accent)   // ✓ for configured
                } else {
                    ("\u{2717}", state.theme.error)    // ✗ for unconfigured
                };

                let prefix = if is_selected { "> " } else { "  " };
                let name_style = if is_selected {
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD)
                } else {
                    Style::default().fg(state.theme.text)
                };

                let key_display = if let Some(ref key) = provider.redacted_key {
                    format!("  {key}")
                } else if provider.is_local {
                    "  localhost:11434".to_string()
                } else {
                    "  not configured".to_string()
                };

                let key_color = if provider.configured || provider.is_local {
                    state.theme.text_muted
                } else {
                    state.theme.text_dimmed
                };

                // Auth flow badge
                let badge = match provider.auth_flow {
                    AuthFlow::Pkce => "Browser",
                    AuthFlow::DeviceCode => "Device code",
                    AuthFlow::ApiKey => "API key",
                };
                let badge_color = match provider.auth_flow {
                    AuthFlow::Pkce | AuthFlow::DeviceCode => state.theme.accent,
                    AuthFlow::ApiKey => state.theme.text_dimmed,
                };

                lines.push(Line::from(vec![
                    Span::styled(prefix, Style::default().fg(state.theme.primary)),
                    Span::styled(format!("{icon} "), Style::default().fg(icon_color)),
                    Span::styled(
                        format!("{:<16}", provider.display_name),
                        name_style,
                    ),
                    Span::styled(key_display, Style::default().fg(key_color)),
                    Span::styled(
                        format!("  [{badge}]"),
                        Style::default().fg(badge_color),
                    ),
                ]));
            }

            lines.push(Line::from(""));
            lines.push(Line::from(vec![
                Span::styled("[Enter] ", Style::default().fg(state.theme.text_muted)),
                Span::styled("Configure  ", Style::default().fg(state.theme.text_dimmed)),
                Span::styled("[d] ", Style::default().fg(state.theme.text_muted)),
                Span::styled("Disconnect  ", Style::default().fg(state.theme.text_dimmed)),
                Span::styled("[t] ", Style::default().fg(state.theme.text_muted)),
                Span::styled("Test  ", Style::default().fg(state.theme.text_dimmed)),
                Span::styled("[Esc] ", Style::default().fg(state.theme.text_muted)),
                Span::styled("Close", Style::default().fg(state.theme.text_dimmed)),
            ]));

            frame.render_widget(Paragraph::new(lines), area);
        }
        ConnectScreen::Configure(provider_id) => {
            let display = provider_name(provider_id);
            let env_hint = standard_env_var(provider_id);

            let mut lines = vec![
                Line::from(Span::styled(
                    format!("Configure {display}"),
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
            ];

            // API key field
            let key_label_style = if pc.active_field == ConnectField::ApiKey {
                Style::default().fg(state.theme.primary)
            } else {
                Style::default().fg(state.theme.text_muted)
            };
            let masked = pc.masked_key();
            let cursor = if pc.active_field == ConnectField::ApiKey {
                "_"
            } else {
                ""
            };
            lines.push(Line::from(vec![
                Span::styled("API Key: ", key_label_style),
                Span::styled(
                    format!("{masked}{cursor}"),
                    Style::default().fg(state.theme.text),
                ),
            ]));

            // Env var hint
            if let Some(env_var) = env_hint {
                lines.push(Line::from(""));
                lines.push(Line::from(Span::styled(
                    format!("Or set {env_var} in your environment."),
                    Style::default().fg(state.theme.text_dimmed),
                )));
            }

            lines.push(Line::from(""));

            // Base URL field
            let url_label_style = if pc.active_field == ConnectField::BaseUrl {
                Style::default().fg(state.theme.primary)
            } else {
                Style::default().fg(state.theme.text_muted)
            };
            let url_cursor = if pc.active_field == ConnectField::BaseUrl {
                "_"
            } else {
                ""
            };
            lines.push(Line::from(vec![
                Span::styled("Base URL (optional): ", url_label_style),
                Span::styled(
                    format!("{}{url_cursor}", pc.base_url_input),
                    Style::default().fg(state.theme.text),
                ),
            ]));

            // Error message
            if let Some(ref msg) = pc.message {
                lines.push(Line::from(""));
                lines.push(Line::from(Span::styled(
                    msg.as_str(),
                    Style::default().fg(state.theme.error),
                )));
            }

            lines.push(Line::from(""));
            lines.push(Line::from(vec![
                Span::styled("[Enter] ", Style::default().fg(state.theme.text_muted)),
                Span::styled("Save  ", Style::default().fg(state.theme.text_dimmed)),
                Span::styled("[Tab] ", Style::default().fg(state.theme.text_muted)),
                Span::styled("Next field  ", Style::default().fg(state.theme.text_dimmed)),
                Span::styled("[Esc] ", Style::default().fg(state.theme.text_muted)),
                Span::styled("Cancel", Style::default().fg(state.theme.text_dimmed)),
            ]));

            frame.render_widget(Paragraph::new(lines), area);
        }
        ConnectScreen::OAuthBrowser {
            provider_id,
            auth_url,
            elapsed_secs,
        } => {
            let display = provider_name(provider_id);
            let spinner = spinner_char(*elapsed_secs);

            let lines = vec![
                Line::from(Span::styled(
                    format!("Sign in to {display}"),
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    "Opening browser for authentication...",
                    Style::default().fg(state.theme.text),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    "If the browser didn't open, visit:",
                    Style::default().fg(state.theme.text_muted),
                )),
                Line::from(Span::styled(
                    truncate_url(auth_url, area.width as usize - 4),
                    Style::default().fg(state.theme.accent),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    format!("{spinner} Waiting for authorization... {elapsed_secs}s"),
                    Style::default().fg(state.theme.text_muted),
                )),
                Line::from(""),
                Line::from(vec![
                    Span::styled("[Esc] ", Style::default().fg(state.theme.text_muted)),
                    Span::styled("Cancel", Style::default().fg(state.theme.text_dimmed)),
                ]),
            ];

            frame.render_widget(Paragraph::new(lines), area);
        }
        ConnectScreen::DeviceCode {
            provider_id,
            user_code,
            verification_uri,
            elapsed_secs,
        } => {
            let display = provider_name(provider_id);
            let spinner = spinner_char(*elapsed_secs);

            let lines = vec![
                Line::from(Span::styled(
                    format!("Sign in to {display}"),
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    "Enter this code:",
                    Style::default().fg(state.theme.text),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    format!("    {user_code}"),
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
                Line::from(vec![
                    Span::styled("Visit: ", Style::default().fg(state.theme.text_muted)),
                    Span::styled(
                        verification_uri.as_str(),
                        Style::default().fg(state.theme.accent),
                    ),
                ]),
                Line::from(""),
                Line::from(Span::styled(
                    format!("{spinner} Waiting for authorization... {elapsed_secs}s"),
                    Style::default().fg(state.theme.text_muted),
                )),
                Line::from(""),
                Line::from(vec![
                    Span::styled("[Enter] ", Style::default().fg(state.theme.text_muted)),
                    Span::styled("Open browser  ", Style::default().fg(state.theme.text_dimmed)),
                    Span::styled("[Esc] ", Style::default().fg(state.theme.text_muted)),
                    Span::styled("Cancel", Style::default().fg(state.theme.text_dimmed)),
                ]),
            ];

            frame.render_widget(Paragraph::new(lines), area);
        }
    }
}

fn build_provider_list(credentials: &CredentialStore) -> Vec<ProviderStatus> {
    known_providers()
        .iter()
        .map(|&id| {
            let is_local = id == "ollama";
            let auth_flow = ava_auth::provider_info(id)
                .map(|p| p.auth_flow)
                .unwrap_or(AuthFlow::ApiKey);
            let credential = credentials.get(id);
            let configured = credential.as_ref().is_some_and(|c| {
                !c.api_key.trim().is_empty()
                    || c.is_oauth_configured()
                    || (is_local && c.base_url.is_some())
            });
            let redacted = credential.as_ref().map(|c| {
                if c.is_oauth_configured() {
                    let token = c.oauth_token.as_deref().unwrap_or("");
                    format!("OAuth ({})", redact_key(token))
                } else if !c.api_key.trim().is_empty() {
                    redact_key(&c.api_key)
                } else {
                    String::new()
                }
            }).filter(|s| !s.is_empty());
            ProviderStatus {
                id: id.to_string(),
                display_name: provider_name(id),
                configured,
                is_local,
                auth_flow,
                redacted_key: redacted,
                env_var_hint: standard_env_var(id),
            }
        })
        .collect()
}

fn spinner_char(elapsed: u64) -> char {
    const CHARS: &[char] = &['\u{280B}', '\u{2819}', '\u{2838}', '\u{2834}', '\u{2826}', '\u{2807}'];
    CHARS[(elapsed as usize) % CHARS.len()]
}

fn truncate_url(url: &str, max_len: usize) -> String {
    if url.len() <= max_len {
        url.to_string()
    } else if max_len > 3 {
        format!("{}...", &url[..max_len - 3])
    } else {
        "...".to_string()
    }
}
