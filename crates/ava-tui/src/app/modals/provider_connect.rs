use super::*;
use crate::widgets::provider_connect::{ConnectField, ConnectScreen};
use ava_auth::config::AuthFlow;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Instant;

fn auth_attempt_is_current(tracker: &Arc<AtomicU64>, attempt: u64) -> bool {
    tracker.load(Ordering::SeqCst) == attempt
}

impl App {
    pub(crate) fn handle_provider_connect_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let Some(ref mut state) = self.state.provider_connect else {
            self.state.active_modal = None;
            return false;
        };

        match &state.screen.clone() {
            ConnectScreen::List => {
                // Handle custom keys (d=disconnect, t=test) before delegating
                if let KeyCode::Char('d') = key.code {
                    if state.list.query.is_empty() {
                        if let Some(provider) = state.selected_provider() {
                            let provider_id = provider.id.clone();
                            let tx = app_tx.clone();
                            tokio::spawn(async move {
                                let mut store = ava_config::CredentialStore::load_default()
                                    .await
                                    .unwrap_or_default();
                                let result = ava_config::execute_credential_command(
                                    ava_config::CredentialCommand::Remove {
                                        provider: provider_id,
                                    },
                                    &mut store,
                                )
                                .await;
                                let next_state = ProviderConnectState::from_credentials(&store);
                                let _ = tx.send(AppEvent::ProviderConnectFinished(match result {
                                    Ok(status) => crate::event::ProviderConnectResult::Refreshed {
                                        state: next_state,
                                        status,
                                    },
                                    Err(err) => crate::event::ProviderConnectResult::InlineError(
                                        format!("Failed: {err}"),
                                    ),
                                }));
                            });
                            return false;
                        }
                    }
                }
                if let KeyCode::Char('t') = key.code {
                    if state.list.query.is_empty() {
                        if let Some(provider) = state.selected_provider() {
                            let provider_id = provider.id.clone();
                            let tx = app_tx.clone();
                            tokio::spawn(async move {
                                let mut store = ava_config::CredentialStore::load_default()
                                    .await
                                    .unwrap_or_default();
                                let result = ava_config::execute_credential_command(
                                    ava_config::CredentialCommand::Test {
                                        provider: provider_id,
                                    },
                                    &mut store,
                                )
                                .await
                                .map_err(|err| err.to_string());
                                let _ = tx.send(AppEvent::ProviderConnectFinished(
                                    crate::event::ProviderConnectResult::Tested(result),
                                ));
                            });
                            return false;
                        }
                    }
                }

                let vh = list_viewport_height(modal_viewport_height());
                let action = handle_select_list_key(&mut state.list, key, vh);
                match action {
                    SelectListAction::Cancelled => {
                        self.state.provider_connect = None;
                        self.state.active_modal = None;
                    }
                    SelectListAction::Selected => {
                        if let Some(provider) = state.selected_provider() {
                            let provider_id = provider.id.clone();
                            let auth_flows = provider.auth_flows.clone();

                            if auth_flows.len() > 1 {
                                state.screen = ConnectScreen::AuthMethodChoice {
                                    provider_id,
                                    selected: 0,
                                };
                                state.message = None;
                            } else {
                                let flow = auth_flows.first().copied().unwrap_or(AuthFlow::ApiKey);
                                Self::start_auth_flow(state, &provider_id, flow, &app_tx);
                            }
                        }
                    }
                    _ => {}
                }
            }
            ConnectScreen::AuthMethodChoice {
                provider_id,
                selected,
            } => {
                let flows: Vec<AuthFlow> = ava_auth::provider_info(provider_id)
                    .map(|i| i.auth_flows.to_vec())
                    .unwrap_or_else(|| vec![AuthFlow::ApiKey]);
                let sel = *selected;

                match key.code {
                    KeyCode::Esc => {
                        let Some(state) = self.state.provider_connect.as_mut() else {
                            return false;
                        };
                        state.cancel_auth_attempt();
                        state.screen = ConnectScreen::List;
                        state.message = None;
                    }
                    KeyCode::Down => {
                        let Some(state) = self.state.provider_connect.as_mut() else {
                            return false;
                        };
                        if let ConnectScreen::AuthMethodChoice { selected, .. } = &mut state.screen
                        {
                            *selected = (*selected + 1) % flows.len();
                        }
                    }
                    KeyCode::Up => {
                        let Some(state) = self.state.provider_connect.as_mut() else {
                            return false;
                        };
                        if let ConnectScreen::AuthMethodChoice { selected, .. } = &mut state.screen
                        {
                            *selected = selected.saturating_sub(1);
                        }
                    }
                    KeyCode::Enter => {
                        if let Some(&flow) = flows.get(sel) {
                            let pid = provider_id.clone();
                            let Some(state) = self.state.provider_connect.as_mut() else {
                                return false;
                            };
                            Self::start_auth_flow(state, &pid, flow, &app_tx);
                        }
                    }
                    _ => {}
                }
            }
            ConnectScreen::Configure(provider_id) => match key.code {
                KeyCode::Esc => {
                    let Some(state) = self.state.provider_connect.as_mut() else {
                        return false;
                    };
                    state.screen = ConnectScreen::List;
                    state.key_input.clear();
                    state.base_url_input.clear();
                    state.message = None;
                }
                KeyCode::Tab | KeyCode::BackTab => {
                    let Some(state) = self.state.provider_connect.as_mut() else {
                        return false;
                    };
                    state.active_field = match state.active_field {
                        ConnectField::ApiKey => ConnectField::BaseUrl,
                        ConnectField::BaseUrl => ConnectField::ApiKey,
                    };
                }
                KeyCode::Enter => {
                    let Some(state) = self.state.provider_connect.as_mut() else {
                        return false;
                    };
                    let api_key = state.key_input.clone();
                    let base_url = if state.base_url_input.trim().is_empty() {
                        None
                    } else {
                        Some(state.base_url_input.trim().to_string())
                    };
                    let provider = provider_id.clone();

                    if api_key.trim().is_empty() && provider != "ollama" {
                        state.message = Some("API key is required".to_string());
                        return false;
                    }

                    let tx = app_tx.clone();
                    tokio::spawn(async move {
                        let mut store = ava_config::CredentialStore::load_default()
                            .await
                            .unwrap_or_default();
                        let result = ava_config::execute_credential_command(
                            ava_config::CredentialCommand::Set {
                                provider: provider.clone(),
                                api_key,
                                base_url,
                            },
                            &mut store,
                        )
                        .await
                        .map_err(|err| err.to_string());
                        let _ = tx.send(AppEvent::ProviderConnectFinished(
                            crate::event::ProviderConnectResult::Saved(result),
                        ));
                    });
                }
                KeyCode::Char(ch) => {
                    let Some(state) = self.state.provider_connect.as_mut() else {
                        return false;
                    };
                    match state.active_field {
                        ConnectField::ApiKey => state.key_input.push(ch),
                        ConnectField::BaseUrl => state.base_url_input.push(ch),
                    }
                }
                KeyCode::Backspace => {
                    let Some(state) = self.state.provider_connect.as_mut() else {
                        return false;
                    };
                    match state.active_field {
                        ConnectField::ApiKey => {
                            state.key_input.pop();
                        }
                        ConnectField::BaseUrl => {
                            state.base_url_input.pop();
                        }
                    }
                }
                _ => {}
            },
            ConnectScreen::OAuthBrowser { .. } => {
                if key.code == KeyCode::Esc {
                    let Some(state) = self.state.provider_connect.as_mut() else {
                        return false;
                    };
                    state.cancel_auth_attempt();
                    state.screen = ConnectScreen::List;
                    state.message = Some("OAuth flow cancelled".to_string());
                }
            }
            ConnectScreen::DeviceCode {
                verification_uri, ..
            } => match key.code {
                KeyCode::Esc => {
                    let Some(state) = self.state.provider_connect.as_mut() else {
                        return false;
                    };
                    state.cancel_auth_attempt();
                    state.screen = ConnectScreen::List;
                    state.message = Some("Device code flow cancelled".to_string());
                }
                KeyCode::Enter => {
                    let _ = ava_auth::browser::open_browser(verification_uri);
                }
                _ => {}
            },
        }
        false
    }

    /// Start a specific auth flow for a provider.
    pub(crate) fn start_auth_flow(
        state: &mut ProviderConnectState,
        provider_id: &str,
        flow: AuthFlow,
        app_tx: &mpsc::UnboundedSender<AppEvent>,
    ) {
        match flow {
            AuthFlow::Pkce => {
                let attempt = state.begin_auth_attempt();
                let tracker = state.auth_attempt_tracker();
                let pkce = ava_auth::pkce::generate_pkce();
                if let Some(cfg) = ava_auth::config::oauth_config(provider_id) {
                    let auth_url = ava_auth::config::build_auth_url(cfg, &pkce);
                    let _ = ava_auth::browser::open_browser(&auth_url);

                    let tx = app_tx.clone();
                    let pid = provider_id.to_string();
                    let port = cfg.redirect_port;
                    let path = cfg.redirect_path.to_string();
                    let token_url = cfg.token_url.to_string();
                    let client_id = cfg.client_id.to_string();
                    let redirect_path = cfg.redirect_path.to_string();
                    let redirect_port = cfg.redirect_port;
                    let callback_tracker = Arc::clone(&tracker);
                    let task = tokio::spawn(async move {
                        match ava_auth::callback::listen_for_callback(port, &path, 120).await {
                            Ok(callback) => {
                                if !auth_attempt_is_current(&callback_tracker, attempt) {
                                    return;
                                }
                                if callback.state != pkce.state {
                                    let _ = tx.send(AppEvent::OAuthError {
                                        provider: pid,
                                        error: "State mismatch (CSRF protection)".to_string(),
                                    });
                                    return;
                                }
                                let cfg = ava_auth::config::OAuthConfig {
                                    client_id: Box::leak(client_id.into_boxed_str()),
                                    authorization_url: "",
                                    token_url: Box::leak(token_url.into_boxed_str()),
                                    scopes: &[],
                                    redirect_port,
                                    redirect_path: Box::leak(redirect_path.into_boxed_str()),
                                    extra_params: &[],
                                    flow: AuthFlow::Pkce,
                                };
                                match ava_auth::tokens::exchange_code_for_tokens(
                                    &cfg,
                                    &callback.code,
                                    &pkce,
                                )
                                .await
                                {
                                    Ok(tokens) => {
                                        if auth_attempt_is_current(&callback_tracker, attempt) {
                                            let _ = tx.send(AppEvent::OAuthSuccess {
                                                provider: pid,
                                                tokens,
                                            });
                                        }
                                    }
                                    Err(e) => {
                                        if auth_attempt_is_current(&callback_tracker, attempt) {
                                            let _ = tx.send(AppEvent::OAuthError {
                                                provider: pid,
                                                error: e.to_string(),
                                            });
                                        }
                                    }
                                }
                            }
                            Err(e) => {
                                if auth_attempt_is_current(&callback_tracker, attempt) {
                                    let _ = tx.send(AppEvent::OAuthError {
                                        provider: pid,
                                        error: e.to_string(),
                                    });
                                }
                            }
                        }
                    });
                    state.set_auth_task(task.abort_handle());

                    state.screen = ConnectScreen::OAuthBrowser {
                        provider_id: provider_id.to_string(),
                        auth_url,
                        started: Instant::now(),
                    };
                    state.message = None;
                }
            }
            AuthFlow::DeviceCode => {
                let attempt = state.begin_auth_attempt();
                let tracker = state.auth_attempt_tracker();
                let pid = provider_id.to_string();
                state.message = Some("Requesting device code...".to_string());
                let tx = app_tx.clone();
                let task = tokio::spawn(async move {
                    let Some(cfg) = ava_auth::config::oauth_config(&pid) else {
                        let _ = tx.send(AppEvent::ProviderConnectFinished(
                            crate::event::ProviderConnectResult::InlineError(format!(
                                "Failed: {}",
                                ava_auth::AuthError::NoOAuthConfig(pid.clone())
                            )),
                        ));
                        return;
                    };

                    match ava_auth::device_code::request_device_code(cfg).await {
                        Ok(device) => {
                            let ready_device = device.clone();
                            let ready_provider_id = pid.clone();
                            if auth_attempt_is_current(&tracker, attempt) {
                                let _ = tx.send(AppEvent::ProviderConnectFinished(
                                    crate::event::ProviderConnectResult::DeviceCodeReady {
                                        provider_id: ready_provider_id,
                                        device: ready_device,
                                        attempt,
                                    },
                                ));
                            }

                            match ava_auth::device_code::poll_device_code(
                                cfg,
                                &device.device_code,
                                device.interval,
                                device.expires_in,
                            )
                            .await
                            {
                                Ok(Some(tokens)) => {
                                    if auth_attempt_is_current(&tracker, attempt) {
                                        let _ = tx.send(AppEvent::OAuthSuccess {
                                            provider: pid,
                                            tokens,
                                        });
                                    }
                                }
                                Ok(None) => {
                                    if auth_attempt_is_current(&tracker, attempt) {
                                        let _ = tx.send(AppEvent::OAuthError {
                                            provider: pid,
                                            error: "Device code expired".to_string(),
                                        });
                                    }
                                }
                                Err(e) => {
                                    if auth_attempt_is_current(&tracker, attempt) {
                                        let _ = tx.send(AppEvent::OAuthError {
                                            provider: pid,
                                            error: e.to_string(),
                                        });
                                    }
                                }
                            }
                        }
                        Err(err) => {
                            if auth_attempt_is_current(&tracker, attempt) {
                                let _ = tx.send(AppEvent::ProviderConnectFinished(
                                    crate::event::ProviderConnectResult::InlineError(format!(
                                        "Failed: {err}"
                                    )),
                                ));
                            }
                        }
                    }
                });
                state.set_auth_task(task.abort_handle());
            }
            AuthFlow::OpenAiHeadless => {
                let attempt = state.begin_auth_attempt();
                let tracker = state.auth_attempt_tracker();
                let pid = provider_id.to_string();
                state.message = Some("Requesting headless login code...".to_string());
                let tx = app_tx.clone();
                let task = tokio::spawn(async move {
                    let Some(cfg) = ava_auth::config::oauth_config(&pid) else {
                        let _ = tx.send(AppEvent::ProviderConnectFinished(
                            crate::event::ProviderConnectResult::InlineError(format!(
                                "Failed: {}",
                                ava_auth::AuthError::NoOAuthConfig(pid.clone())
                            )),
                        ));
                        return;
                    };

                    match ava_auth::openai_headless::request_code(cfg.client_id).await {
                        Ok(device) => {
                            let ready_device = device.clone();
                            let ready_provider_id = pid.clone();
                            if auth_attempt_is_current(&tracker, attempt) {
                                let _ = tx.send(AppEvent::ProviderConnectFinished(
                                    crate::event::ProviderConnectResult::DeviceCodeReady {
                                        provider_id: ready_provider_id,
                                        device: ready_device,
                                        attempt,
                                    },
                                ));
                            }

                            match ava_auth::openai_headless::poll_code(
                                cfg.client_id,
                                &device.device_code,
                                &device.user_code,
                                device.interval,
                            )
                            .await
                            {
                                Ok(Some(tokens)) => {
                                    if auth_attempt_is_current(&tracker, attempt) {
                                        let _ = tx.send(AppEvent::OAuthSuccess {
                                            provider: pid,
                                            tokens,
                                        });
                                    }
                                }
                                Ok(None) => {
                                    if auth_attempt_is_current(&tracker, attempt) {
                                        let _ = tx.send(AppEvent::OAuthError {
                                            provider: pid,
                                            error: "Headless login expired".to_string(),
                                        });
                                    }
                                }
                                Err(err) => {
                                    if auth_attempt_is_current(&tracker, attempt) {
                                        let _ = tx.send(AppEvent::OAuthError {
                                            provider: pid,
                                            error: err.to_string(),
                                        });
                                    }
                                }
                            }
                        }
                        Err(err) => {
                            if auth_attempt_is_current(&tracker, attempt) {
                                let _ = tx.send(AppEvent::ProviderConnectFinished(
                                    crate::event::ProviderConnectResult::InlineError(format!(
                                        "Failed: {err}"
                                    )),
                                ));
                            }
                        }
                    }
                });
                state.set_auth_task(task.abort_handle());
            }
            AuthFlow::ApiKey => {
                state.cancel_auth_attempt();
                state.screen = ConnectScreen::Configure(provider_id.to_string());
                state.key_input.clear();
                state.base_url_input.clear();
                state.active_field = ConnectField::ApiKey;
                state.message = Some("Loading provider settings...".to_string());
                let tx = app_tx.clone();
                let provider_id = provider_id.to_string();
                tokio::spawn(async move {
                    let base_url = ava_config::CredentialStore::load_default()
                        .await
                        .ok()
                        .and_then(|c| c.get(&provider_id))
                        .and_then(|c| c.base_url)
                        .unwrap_or_default();
                    let _ = tx.send(AppEvent::ProviderConnectFinished(
                        crate::event::ProviderConnectResult::ConfigureLoaded {
                            provider_id,
                            base_url,
                        },
                    ));
                });
            }
        }
    }
}
