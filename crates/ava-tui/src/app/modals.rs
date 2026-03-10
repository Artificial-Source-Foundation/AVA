use super::*;
use ava_auth::config::AuthFlow;
use crate::widgets::command_palette::CommandExec;
use crate::widgets::provider_connect::{ConnectField, ConnectScreen};
use crate::widgets::select_list::{handle_select_list_key, list_viewport_height, SelectListAction};
use std::time::Instant;

/// Estimate the model selector viewport height (70% of terminal, minus border).
fn modal_viewport_height() -> usize {
    let (_, rows) = crossterm::terminal::size().unwrap_or((80, 40));
    // Modal is 70% of terminal height, minus 2 for border
    ((rows as usize) * 70 / 100).saturating_sub(2)
}

impl App {
    /// Route paste events to the active modal (e.g., API key input field).
    pub(crate) fn handle_modal_paste(&mut self, value: &str) {
        // Only the ProviderConnect Configure screen has text input fields
        if let Some(ref mut pc) = self.state.provider_connect {
            if let ConnectScreen::Configure(_) = pc.screen {
                match pc.active_field {
                    ConnectField::ApiKey => pc.key_input.push_str(value),
                    ConnectField::BaseUrl => pc.base_url_input.push_str(value),
                }
                return;
            }
        }
        // For search-based modals, route paste to the search query
        match self.state.active_modal {
            Some(ModalType::ModelSelector) => {
                if let Some(ref mut sel) = self.state.model_selector {
                    sel.list.query.push_str(value);
                }
            }
            Some(ModalType::CommandPalette) => {
                self.state.command_palette.list.query.push_str(value);
            }
            Some(ModalType::ToolList) => {
                self.state.tool_list.list.query.push_str(value);
            }
            Some(ModalType::SessionList) => {
                self.state.session_list.list.query.push_str(value);
            }
            Some(ModalType::ProviderConnect) => {
                if let Some(ref mut pc) = self.state.provider_connect {
                    if let ConnectScreen::List = pc.screen {
                        pc.list.query.push_str(value);
                    }
                }
            }
            _ => {}
        }
    }

    pub(crate) fn handle_modal_key(
        &mut self,
        modal: ModalType,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        if key.kind != KeyEventKind::Press {
            return false;
        }

        match modal {
            ModalType::CommandPalette => self.handle_command_palette_key(key),
            ModalType::SessionList => self.handle_session_list_key(key),
            ModalType::ToolApproval => self.handle_tool_approval_key(key),
            ModalType::ModelSelector => self.handle_model_selector_key(key, app_tx),
            ModalType::ToolList => self.handle_tool_list_key(key),
            ModalType::ProviderConnect => self.handle_provider_connect_key(key, app_tx),
        }
    }

    fn handle_command_palette_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut self.state.command_palette.list, key, vh);
        match action {
            SelectListAction::Cancelled => {
                self.state.command_palette.open = false;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                if let Some(item) = self.state.command_palette.list.selected_item() {
                    match &item.value {
                        CommandExec::Action(action) => {
                            let action = *action;
                            self.state.command_palette.open = false;
                            self.state.active_modal = None;
                            self.execute_command_action(action);
                        }
                        CommandExec::Slash(cmd) => {
                            let cmd = cmd.clone();
                            self.state.command_palette.open = false;
                            self.state.active_modal = None;
                            if let Some((kind, msg)) = self.handle_slash_command(&cmd) {
                                self.state
                                    .messages
                                    .push(UiMessage::new(kind, msg));
                            }
                        }
                    }
                } else {
                    self.state.command_palette.open = false;
                    self.state.active_modal = None;
                }
            }
            _ => {}
        }
        false
    }

    fn handle_session_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut self.state.session_list.list, key, vh);
        match action {
            SelectListAction::Cancelled => {
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                if let Some(session_id) = self.state.session_list.list.selected_value().copied() {
                    if session_id.is_nil() {
                        // "New Session" selected
                        let _ = self.state.session.create_session();
                        self.state.messages.messages.clear();
                        self.state.messages.reset_scroll();
                        self.set_status("New session created", StatusLevel::Info);
                    } else if self.state.session.switch_to(session_id).is_ok() {
                        self.state.messages.messages.clear();
                        self.state.messages.reset_scroll();
                        if let Some(ref session) = self.state.session.current_session {
                            for msg in &session.messages {
                                let kind = match msg.role {
                                    ava_types::Role::User => MessageKind::User,
                                    ava_types::Role::Assistant => MessageKind::Assistant,
                                    ava_types::Role::Tool => MessageKind::ToolResult,
                                    ava_types::Role::System => MessageKind::System,
                                };
                                self.state.messages.push(UiMessage::new(kind, msg.content.clone()));
                            }
                            // Restore model from session metadata
                            if let Some(meta) = session.metadata.as_object() {
                                let provider = meta.get("provider").and_then(|v| v.as_str());
                                let model = meta.get("model").and_then(|v| v.as_str());
                                if let (Some(p), Some(m)) = (provider, model) {
                                    let result = tokio::task::block_in_place(|| {
                                        tokio::runtime::Handle::current().block_on(
                                            self.state.agent.switch_model(p, m),
                                        )
                                    });
                                    if let Ok(desc) = result {
                                        self.set_status(
                                            format!("Session loaded — model: {desc}"),
                                            StatusLevel::Info,
                                        );
                                        return false;
                                    }
                                }
                            }
                        }
                        self.set_status("Session loaded", StatusLevel::Info);
                    }
                }
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            _ => {}
        }
        false
    }

    fn handle_tool_approval_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        use crate::state::permission::ApprovalStage;

        if self.state.permission.queue.is_empty() {
            self.state.active_modal = None;
            return false;
        }

        match self.state.permission.current_stage {
            ApprovalStage::Preview => {
                self.state.permission.current_stage = ApprovalStage::ActionSelect;
            }
            ApprovalStage::ActionSelect => match key.code {
                KeyCode::Char('a') => {
                    self.state.permission.approve_current_once();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                KeyCode::Char('s') => {
                    self.state.permission.approve_current_for_session();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                KeyCode::Char('r') => {
                    self.state.permission.current_stage = ApprovalStage::RejectionReason;
                }
                KeyCode::Char('y') => {
                    self.state.permission.yolo_mode = true;
                    while !self.state.permission.queue.is_empty() {
                        self.state.permission.approve_current_once();
                    }
                    self.state.active_modal = None;
                    self.set_status("YOLO mode enabled", StatusLevel::Info);
                }
                KeyCode::Esc => {
                    self.state.permission.reject_current();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                _ => {}
            },
            ApprovalStage::RejectionReason => match key.code {
                KeyCode::Enter => {
                    self.state.permission.reject_current();
                    if self.state.permission.queue.is_empty() {
                        self.state.active_modal = None;
                    }
                }
                KeyCode::Esc => {
                    self.state.permission.current_stage = ApprovalStage::ActionSelect;
                    self.state.permission.rejection_input.clear();
                }
                KeyCode::Char(ch) => {
                    self.state.permission.rejection_input.push(ch);
                }
                KeyCode::Backspace => {
                    self.state.permission.rejection_input.pop();
                }
                _ => {}
            },
        }
        false
    }

    fn handle_model_selector_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        _app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let selector = match self.state.model_selector {
            Some(ref mut s) => s,
            None => {
                self.state.active_modal = None;
                return false;
            }
        };

        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut selector.list, key, vh);
        match action {
            SelectListAction::Cancelled => {
                self.state.model_selector = None;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                let Some(selector) = self.state.model_selector.as_ref() else { return false; };
                if let Some(mv) = selector.list.selected_value() {
                    let provider = mv.provider.clone();
                    let model = mv.model.clone();
                    let display = mv.display.clone();

                    let result = tokio::task::block_in_place(|| {
                        tokio::runtime::Handle::current().block_on(
                            self.state.agent.switch_model(&provider, &model),
                        )
                    });

                    match result {
                        Ok(_) => {
                            self.set_status(
                                format!("Switched to {display}"),
                                StatusLevel::Info,
                            );
                        }
                        Err(err) => {
                            self.set_status(
                                format!("Failed: {err}"),
                                StatusLevel::Error,
                            );
                        }
                    }
                }
                self.state.model_selector = None;
                self.state.active_modal = None;
            }
            _ => {}
        }
        false
    }

    fn handle_tool_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut self.state.tool_list.list, key, vh);
        if action == SelectListAction::Cancelled {
            self.state.active_modal = None;
        }
        false
    }

    fn handle_provider_connect_key(&mut self, key: crossterm::event::KeyEvent, app_tx: mpsc::UnboundedSender<AppEvent>) -> bool {
        let state = match self.state.provider_connect {
            Some(ref mut s) => s,
            None => {
                self.state.active_modal = None;
                return false;
            }
        };

        match &state.screen.clone() {
            ConnectScreen::List => {
                // Handle custom keys (d=disconnect, t=test) before delegating
                if let KeyCode::Char('d') = key.code {
                    if state.list.query.is_empty() {
                        if let Some(provider) = state.selected_provider() {
                            let provider_id = provider.id.clone();
                            let result = tokio::task::block_in_place(|| {
                                tokio::runtime::Handle::current().block_on(async {
                                    let mut store = ava_config::CredentialStore::load_default()
                                        .await
                                        .unwrap_or_default();
                                    ava_config::execute_credential_command(
                                        ava_config::CredentialCommand::Remove {
                                            provider: provider_id,
                                        },
                                        &mut store,
                                    )
                                    .await
                                })
                            });
                            match result {
                                Ok(msg) => {
                                    self.set_status(&msg, StatusLevel::Info);
                                    let credentials = tokio::task::block_in_place(|| {
                                        tokio::runtime::Handle::current()
                                            .block_on(ava_config::CredentialStore::load_default())
                                    })
                                    .unwrap_or_default();
                                    if let Some(ref mut pc) = self.state.provider_connect {
                                        *pc = ProviderConnectState::from_credentials(&credentials);
                                    }
                                }
                                Err(err) => {
                                    self.set_status(
                                        format!("Failed: {err}"),
                                        StatusLevel::Error,
                                    );
                                }
                            }
                            return false;
                        }
                    }
                }
                if let KeyCode::Char('t') = key.code {
                    if state.list.query.is_empty() {
                        if let Some(provider) = state.selected_provider() {
                            let provider_id = provider.id.clone();
                            let result = tokio::task::block_in_place(|| {
                                tokio::runtime::Handle::current().block_on(async {
                                    let mut store = ava_config::CredentialStore::load_default()
                                        .await
                                        .unwrap_or_default();
                                    ava_config::execute_credential_command(
                                        ava_config::CredentialCommand::Test {
                                            provider: provider_id,
                                        },
                                        &mut store,
                                    )
                                    .await
                                })
                            });
                            match result {
                                Ok(msg) => self.set_status(&msg, StatusLevel::Info),
                                Err(err) => self.set_status(
                                    format!("Test failed: {err}"),
                                    StatusLevel::Error,
                                ),
                            }
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
            ConnectScreen::AuthMethodChoice { provider_id, selected } => {
                let flows: Vec<AuthFlow> = ava_auth::provider_info(provider_id)
                    .map(|i| i.auth_flows.to_vec())
                    .unwrap_or_else(|| vec![AuthFlow::ApiKey]);
                let sel = *selected;

                match key.code {
                    KeyCode::Esc => {
                        let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                        state.screen = ConnectScreen::List;
                        state.message = None;
                    }
                    KeyCode::Down => {
                        let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                        if let ConnectScreen::AuthMethodChoice { selected, .. } = &mut state.screen {
                            *selected = (*selected + 1) % flows.len();
                        }
                    }
                    KeyCode::Up => {
                        let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                        if let ConnectScreen::AuthMethodChoice { selected, .. } = &mut state.screen {
                            *selected = selected.saturating_sub(1);
                        }
                    }
                    KeyCode::Enter => {
                        if let Some(&flow) = flows.get(sel) {
                            let pid = provider_id.clone();
                            let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                            Self::start_auth_flow(state, &pid, flow, &app_tx);
                        }
                    }
                    _ => {}
                }
            },
            ConnectScreen::Configure(provider_id) => match key.code {
                KeyCode::Esc => {
                    let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                    state.screen = ConnectScreen::List;
                    state.key_input.clear();
                    state.base_url_input.clear();
                    state.message = None;
                }
                KeyCode::Tab | KeyCode::BackTab => {
                    let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                    state.active_field = match state.active_field {
                        ConnectField::ApiKey => ConnectField::BaseUrl,
                        ConnectField::BaseUrl => ConnectField::ApiKey,
                    };
                }
                KeyCode::Enter => {
                    let Some(state) = self.state.provider_connect.as_mut() else { return false; };
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

                    let result = tokio::task::block_in_place(|| {
                        tokio::runtime::Handle::current().block_on(async {
                            let mut store = ava_config::CredentialStore::load_default()
                                .await
                                .unwrap_or_default();
                            ava_config::execute_credential_command(
                                ava_config::CredentialCommand::Set {
                                    provider: provider.clone(),
                                    api_key,
                                    base_url,
                                },
                                &mut store,
                            )
                            .await
                        })
                    });

                    match result {
                        Ok(msg) => {
                            self.set_status(&msg, StatusLevel::Info);
                            self.state.provider_connect = None;
                            self.state.active_modal = None;
                        }
                        Err(err) => {
                            if let Some(ref mut pc) = self.state.provider_connect {
                                pc.message = Some(format!("Failed: {err}"));
                            }
                        }
                    }
                }
                KeyCode::Char(ch) => {
                    let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                    match state.active_field {
                        ConnectField::ApiKey => state.key_input.push(ch),
                        ConnectField::BaseUrl => state.base_url_input.push(ch),
                    }
                }
                KeyCode::Backspace => {
                    let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                    match state.active_field {
                        ConnectField::ApiKey => { state.key_input.pop(); }
                        ConnectField::BaseUrl => { state.base_url_input.pop(); }
                    }
                }
                _ => {}
            },
            ConnectScreen::OAuthBrowser { .. } => {
                if key.code == KeyCode::Esc {
                    let Some(state) = self.state.provider_connect.as_mut() else { return false; };
                    state.screen = ConnectScreen::List;
                    state.message = Some("OAuth flow cancelled".to_string());
                }
            }
            ConnectScreen::DeviceCode { verification_uri, .. } => match key.code {
                KeyCode::Esc => {
                    let Some(state) = self.state.provider_connect.as_mut() else { return false; };
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
    fn start_auth_flow(
        state: &mut ProviderConnectState,
        provider_id: &str,
        flow: AuthFlow,
        app_tx: &mpsc::UnboundedSender<AppEvent>,
    ) {
        match flow {
            AuthFlow::Pkce => {
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
                    tokio::spawn(async move {
                        match ava_auth::callback::listen_for_callback(port, &path, 120).await {
                            Ok(callback) => {
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
                                match ava_auth::tokens::exchange_code_for_tokens(&cfg, &callback.code, &pkce).await {
                                    Ok(tokens) => {
                                        let _ = tx.send(AppEvent::OAuthSuccess { provider: pid, tokens });
                                    }
                                    Err(e) => {
                                        let _ = tx.send(AppEvent::OAuthError { provider: pid, error: e.to_string() });
                                    }
                                }
                            }
                            Err(e) => {
                                let _ = tx.send(AppEvent::OAuthError { provider: pid, error: e.to_string() });
                            }
                        }
                    });

                    state.screen = ConnectScreen::OAuthBrowser {
                        provider_id: provider_id.to_string(),
                        auth_url,
                        started: Instant::now(),
                    };
                    state.message = None;
                }
            }
            AuthFlow::DeviceCode => {
                let pid = provider_id.to_string();
                let result = tokio::task::block_in_place(|| {
                    tokio::runtime::Handle::current().block_on(async {
                        if let Some(cfg) = ava_auth::config::oauth_config(&pid) {
                            ava_auth::device_code::request_device_code(cfg).await
                        } else {
                            Err(ava_auth::AuthError::NoOAuthConfig(pid.clone()))
                        }
                    })
                });
                match result {
                    Ok(device) => {
                        let tx = app_tx.clone();
                        let poll_pid = pid.clone();
                        let device_code = device.device_code.clone();
                        let interval = device.interval;
                        let expires = device.expires_in;
                        tokio::spawn(async move {
                            if let Some(cfg) = ava_auth::config::oauth_config(&poll_pid) {
                                match ava_auth::device_code::poll_device_code(cfg, &device_code, interval, expires).await {
                                    Ok(Some(tokens)) => {
                                        let _ = tx.send(AppEvent::OAuthSuccess { provider: poll_pid, tokens });
                                    }
                                    Ok(None) => {
                                        let _ = tx.send(AppEvent::OAuthError { provider: poll_pid, error: "Device code expired".to_string() });
                                    }
                                    Err(e) => {
                                        let _ = tx.send(AppEvent::OAuthError { provider: poll_pid, error: e.to_string() });
                                    }
                                }
                            }
                        });

                        state.screen = ConnectScreen::DeviceCode {
                            provider_id: pid,
                            user_code: device.user_code,
                            verification_uri: device.verification_uri,
                            started: Instant::now(),
                        };
                        state.message = None;
                    }
                    Err(err) => {
                        state.message = Some(format!("Failed: {err}"));
                    }
                }
            }
            AuthFlow::ApiKey => {
                let base_url = {
                    let credentials = tokio::task::block_in_place(|| {
                        tokio::runtime::Handle::current()
                            .block_on(ava_config::CredentialStore::load_default())
                    });
                    credentials
                        .ok()
                        .and_then(|c| c.get(provider_id))
                        .and_then(|c| c.base_url)
                        .unwrap_or_default()
                };
                state.screen = ConnectScreen::Configure(provider_id.to_string());
                state.key_input.clear();
                state.base_url_input = base_url;
                state.active_field = ConnectField::ApiKey;
                state.message = None;
            }
        }
    }
}
