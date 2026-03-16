use ava_auth::config::AuthFlow;
use ava_auth::ProviderGroup;
use ava_config::{provider_name, redact_key, standard_env_var, CredentialStore};
use ratatui::prelude::*;
use ratatui::widgets::Paragraph;
use std::time::Instant;

use crate::app::AppState;
use crate::widgets::safe_render::{clamp_line, truncate_str};
use crate::widgets::select_list::{
    render_select_list, ItemStatus, KeybindHint, SelectItem, SelectListConfig, SelectListState,
};

/// Provider status for display.
#[derive(Debug, Clone)]
pub struct ProviderStatus {
    pub id: String,
    pub display_name: String,
    pub description: String,
    pub configured: bool,
    pub is_local: bool,
    pub auth_flows: Vec<AuthFlow>,
    pub redacted_key: Option<String>,
    pub env_var_hint: Option<&'static str>,
    pub group: ProviderGroup,
}

/// Which screen the provider connect modal is showing.
#[derive(Debug, Clone)]
pub enum ConnectScreen {
    /// Provider list with search.
    List,
    /// Auth method choice (API key vs Browser Login).
    AuthMethodChoice {
        provider_id: String,
        selected: usize,
    },
    /// API key input for a specific provider.
    Configure(String),
    /// Browser OAuth in progress (PKCE flow).
    OAuthBrowser {
        provider_id: String,
        auth_url: String,
        started: Instant,
    },
    /// Device code display (e.g., GitHub Copilot).
    DeviceCode {
        provider_id: String,
        user_code: String,
        verification_uri: String,
        started: Instant,
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
    pub list: SelectListState<String>,
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
        let items = build_select_items(&providers);
        Self {
            screen: ConnectScreen::List,
            list: SelectListState::new(items),
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
        let items = build_select_items(&providers);
        let base_url = credentials
            .get(provider)
            .and_then(|c| c.base_url.clone())
            .unwrap_or_default();
        Self {
            screen: ConnectScreen::Configure(provider.to_string()),
            list: SelectListState::new(items),
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
            "*".repeat(self.key_input.len())
        }
    }

    /// Get the selected provider from the filtered list.
    pub fn selected_provider(&self) -> Option<&ProviderStatus> {
        let provider_id = self.list.selected_value()?;
        self.providers.iter().find(|p| &p.id == provider_id)
    }
}

fn build_select_items(providers: &[ProviderStatus]) -> Vec<SelectItem<String>> {
    providers
        .iter()
        .map(|p| {
            let section = match p.group {
                ProviderGroup::Popular => "* Popular",
                ProviderGroup::Other => "- Other",
            };
            let status = if p.configured {
                let text = if let Some(ref key) = p.redacted_key {
                    key.clone()
                } else {
                    "connected".to_string()
                };
                Some(ItemStatus::Connected(text))
            } else {
                None
            };
            SelectItem {
                title: p.display_name.clone(),
                detail: p.description.clone(),
                section: Some(section.to_string()),
                status,
                value: p.id.clone(),
                enabled: true,
            }
        })
        .collect()
}

/// Render the provider connect modal.
pub fn render_provider_connect(frame: &mut Frame<'_>, area: Rect, state: &mut AppState) {
    let Some(ref mut pc) = state.provider_connect else {
        return;
    };

    match &pc.screen {
        ConnectScreen::List => {
            let config = SelectListConfig {
                title: "Connect a provider".to_string(),
                search_placeholder: "Search providers...".to_string(),
                keybinds: vec![
                    KeybindHint {
                        key: "enter".to_string(),
                        label: "connect".to_string(),
                    },
                    KeybindHint {
                        key: "d".to_string(),
                        label: "disconnect".to_string(),
                    },
                    KeybindHint {
                        key: "t".to_string(),
                        label: "test".to_string(),
                    },
                ],
            };
            render_select_list(frame, area, &mut pc.list, &config, &state.theme);
        }
        ConnectScreen::AuthMethodChoice {
            provider_id,
            selected,
        } => {
            let aw = area.width as usize;
            let info = ava_auth::provider_info(provider_id);
            let display = info
                .map(|i| i.name.to_string())
                .unwrap_or_else(|| provider_name(provider_id));
            let flows: Vec<AuthFlow> = info
                .map(|i| i.auth_flows.to_vec())
                .unwrap_or_else(|| vec![AuthFlow::ApiKey]);

            let mut lines = vec![
                clamp_line(
                    Line::from(vec![
                        Span::styled(
                            format!("Connect {display}"),
                            Style::default()
                                .fg(state.theme.primary)
                                .add_modifier(Modifier::BOLD),
                        ),
                        Span::raw("  ".to_string()),
                        Span::styled(
                            "esc".to_string(),
                            Style::default().fg(state.theme.text_dimmed),
                        ),
                    ]),
                    aw,
                ),
                Line::from(""),
                clamp_line(
                    Line::from(Span::styled(
                        "How do you want to connect?".to_string(),
                        Style::default().fg(state.theme.text),
                    )),
                    aw,
                ),
                Line::from(""),
            ];

            for (idx, flow) in flows.iter().enumerate() {
                let is_sel = idx == *selected;
                let (label, hint) = match flow {
                    AuthFlow::ApiKey => ("API Key", "Paste your API key"),
                    AuthFlow::Pkce => ("Browser Login", "Sign in with your browser"),
                    AuthFlow::DeviceCode => ("Device Code", "Enter code on provider website"),
                };

                let name_style = if is_sel {
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD)
                } else {
                    Style::default().fg(state.theme.text)
                };

                lines.push(clamp_line(
                    Line::from(vec![
                        Span::styled(
                            if is_sel {
                                "> ".to_string()
                            } else {
                                "  ".to_string()
                            },
                            Style::default().fg(state.theme.primary),
                        ),
                        Span::styled(label.to_string(), name_style),
                        Span::styled(
                            format!("  {hint}"),
                            Style::default().fg(state.theme.text_dimmed),
                        ),
                    ]),
                    aw,
                ));
            }

            frame.render_widget(Paragraph::new(lines), area);
        }
        ConnectScreen::Configure(provider_id) => {
            let aw = area.width as usize;
            let display = provider_name(provider_id);
            let env_hint = standard_env_var(provider_id);

            let mut lines = vec![
                clamp_line(
                    Line::from(vec![
                        Span::styled(
                            format!("Configure {display}"),
                            Style::default()
                                .fg(state.theme.primary)
                                .add_modifier(Modifier::BOLD),
                        ),
                        Span::raw("  ".to_string()),
                        Span::styled(
                            "esc".to_string(),
                            Style::default().fg(state.theme.text_dimmed),
                        ),
                    ]),
                    aw,
                ),
                Line::from(""),
            ];

            // API key field
            let key_active = pc.active_field == ConnectField::ApiKey;
            let key_indicator = if key_active { "> " } else { "  " };
            let key_label_style = if key_active {
                Style::default()
                    .fg(state.theme.primary)
                    .add_modifier(Modifier::BOLD)
            } else {
                Style::default().fg(state.theme.text_muted)
            };
            let masked = pc.masked_key();
            let cursor = if key_active { "|" } else { "" };

            lines.push(Line::from(Span::styled(
                truncate_str(&format!("{key_indicator}API Key"), aw),
                key_label_style,
            )));
            lines.push(clamp_line(
                Line::from(vec![
                    Span::raw("  ".to_string()),
                    Span::styled(
                        if masked.is_empty() && key_active {
                            cursor.to_string()
                        } else {
                            format!("{masked}{cursor}")
                        },
                        Style::default().fg(state.theme.text),
                    ),
                ]),
                aw,
            ));

            // Env var hint
            if let Some(env_var) = env_hint {
                lines.push(Line::from(Span::styled(
                    truncate_str(&format!("  or set {env_var}"), aw),
                    Style::default().fg(state.theme.text_dimmed),
                )));
            }

            lines.push(Line::from(""));

            // Base URL field
            let url_active = pc.active_field == ConnectField::BaseUrl;
            let url_indicator = if url_active { "> " } else { "  " };
            let url_label_style = if url_active {
                Style::default()
                    .fg(state.theme.primary)
                    .add_modifier(Modifier::BOLD)
            } else {
                Style::default().fg(state.theme.text_muted)
            };
            let url_cursor = if url_active { "|" } else { "" };

            lines.push(Line::from(Span::styled(
                truncate_str(&format!("{url_indicator}Base URL (optional)"), aw),
                url_label_style,
            )));
            lines.push(clamp_line(
                Line::from(vec![
                    Span::raw("  ".to_string()),
                    Span::styled(
                        if pc.base_url_input.is_empty() && url_active {
                            url_cursor.to_string()
                        } else {
                            format!("{}{url_cursor}", pc.base_url_input)
                        },
                        Style::default().fg(state.theme.text),
                    ),
                ]),
                aw,
            ));

            // Error message
            if let Some(ref msg) = pc.message {
                lines.push(Line::from(""));
                lines.push(Line::from(Span::styled(
                    truncate_str(msg.as_str(), aw),
                    Style::default().fg(state.theme.error),
                )));
            }

            lines.push(Line::from(""));
            lines.push(clamp_line(
                Line::from(vec![
                    Span::styled(
                        "enter ".to_string(),
                        Style::default().fg(state.theme.text_muted),
                    ),
                    Span::styled(
                        "save  ".to_string(),
                        Style::default().fg(state.theme.text_dimmed),
                    ),
                    Span::styled(
                        "tab ".to_string(),
                        Style::default().fg(state.theme.text_muted),
                    ),
                    Span::styled(
                        "next field  ".to_string(),
                        Style::default().fg(state.theme.text_dimmed),
                    ),
                    Span::styled(
                        "esc ".to_string(),
                        Style::default().fg(state.theme.text_muted),
                    ),
                    Span::styled(
                        "back".to_string(),
                        Style::default().fg(state.theme.text_dimmed),
                    ),
                ]),
                aw,
            ));

            frame.render_widget(Paragraph::new(lines), area);
        }
        ConnectScreen::OAuthBrowser {
            provider_id,
            auth_url,
            started,
        } => {
            let aw = area.width as usize;
            let display = provider_name(provider_id);
            let elapsed_secs = started.elapsed().as_secs();
            let spinner = spinner_char(elapsed_secs);

            let lines = vec![
                Line::from(Span::styled(
                    truncate_str(&format!("Sign in to {display}"), aw),
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    truncate_str("Opening browser for authentication...", aw),
                    Style::default().fg(state.theme.text),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    truncate_str("If the browser didn't open, visit:", aw),
                    Style::default().fg(state.theme.text_muted),
                )),
                Line::from(Span::styled(
                    truncate_url(auth_url, aw.saturating_sub(4)),
                    Style::default().fg(state.theme.accent),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    truncate_str(
                        &format!("{spinner} Waiting for authorization... {elapsed_secs}s"),
                        aw,
                    ),
                    Style::default().fg(state.theme.text_muted),
                )),
                Line::from(""),
                clamp_line(
                    Line::from(vec![
                        Span::styled(
                            "[Esc] ".to_string(),
                            Style::default().fg(state.theme.text_muted),
                        ),
                        Span::styled(
                            "Cancel".to_string(),
                            Style::default().fg(state.theme.text_dimmed),
                        ),
                    ]),
                    aw,
                ),
            ];

            frame.render_widget(Paragraph::new(lines), area);
        }
        ConnectScreen::DeviceCode {
            provider_id,
            user_code,
            verification_uri,
            started,
        } => {
            let aw = area.width as usize;
            let display = provider_name(provider_id);
            let elapsed_secs = started.elapsed().as_secs();
            let spinner = spinner_char(elapsed_secs);

            let lines = vec![
                Line::from(Span::styled(
                    truncate_str(&format!("Sign in to {display}"), aw),
                    Style::default()
                        .fg(state.theme.primary)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    truncate_str("Enter this code:", aw),
                    Style::default().fg(state.theme.text),
                )),
                Line::from(""),
                Line::from(Span::styled(
                    truncate_str(&format!("    {user_code}"), aw),
                    Style::default()
                        .fg(state.theme.accent)
                        .add_modifier(Modifier::BOLD),
                )),
                Line::from(""),
                clamp_line(
                    Line::from(vec![
                        Span::styled(
                            "Visit: ".to_string(),
                            Style::default().fg(state.theme.text_muted),
                        ),
                        Span::styled(
                            truncate_str(verification_uri.as_str(), aw.saturating_sub(7)),
                            Style::default().fg(state.theme.accent),
                        ),
                    ]),
                    aw,
                ),
                Line::from(""),
                Line::from(Span::styled(
                    truncate_str(
                        &format!("{spinner} Waiting for authorization... {elapsed_secs}s"),
                        aw,
                    ),
                    Style::default().fg(state.theme.text_muted),
                )),
                Line::from(""),
                clamp_line(
                    Line::from(vec![
                        Span::styled(
                            "[Enter] ".to_string(),
                            Style::default().fg(state.theme.text_muted),
                        ),
                        Span::styled(
                            "Open browser  ".to_string(),
                            Style::default().fg(state.theme.text_dimmed),
                        ),
                        Span::styled(
                            "[Esc] ".to_string(),
                            Style::default().fg(state.theme.text_muted),
                        ),
                        Span::styled(
                            "Cancel".to_string(),
                            Style::default().fg(state.theme.text_dimmed),
                        ),
                    ]),
                    aw,
                ),
            ];

            frame.render_widget(Paragraph::new(lines), area);
        }
    }
}

fn build_provider_list(credentials: &CredentialStore) -> Vec<ProviderStatus> {
    ava_auth::all_providers()
        .iter()
        .map(|info| {
            let is_local = info.id == "ollama";
            let credential = credentials.get(info.id);
            let configured = credential.as_ref().is_some_and(|c| {
                !c.api_key.trim().is_empty()
                    || c.is_oauth_configured()
                    || (is_local && c.base_url.is_some())
            });
            let redacted = credential
                .as_ref()
                .map(|c| {
                    if c.is_oauth_configured() {
                        let token = c.oauth_token.as_deref().unwrap_or("");
                        format!("OAuth ({})", redact_key(token))
                    } else if !c.api_key.trim().is_empty() {
                        redact_key(&c.api_key)
                    } else {
                        String::new()
                    }
                })
                .filter(|s| !s.is_empty());
            ProviderStatus {
                id: info.id.to_string(),
                display_name: info.name.to_string(),
                description: info.description.to_string(),
                configured,
                is_local,
                auth_flows: info.auth_flows.to_vec(),
                redacted_key: redacted,
                env_var_hint: info.env_var,
                group: info.group,
            }
        })
        .collect()
}

fn spinner_char(elapsed: u64) -> char {
    const CHARS: &[char] = &['|', '/', '-', '\\'];
    CHARS[(elapsed as usize) % CHARS.len()]
}

fn truncate_url(url: &str, max_len: usize) -> String {
    crate::text_utils::truncate_display(url, max_len)
}
