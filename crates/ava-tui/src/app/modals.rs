use super::*;
use crate::widgets::command_palette::CommandExec;
use crate::widgets::provider_connect::{ConnectField, ConnectScreen};

impl App {
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
            ModalType::ProviderConnect => self.handle_provider_connect_key(key),
        }
    }

    fn handle_command_palette_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        match key.code {
            KeyCode::Esc => {
                self.state.command_palette.open = false;
                self.state.active_modal = None;
            }
            KeyCode::Down => {
                let filtered = self.state.command_palette.filtered();
                if !filtered.is_empty() {
                    self.state.command_palette.selected =
                        (self.state.command_palette.selected + 1) % filtered.len();
                }
            }
            KeyCode::Up => {
                self.state.command_palette.selected =
                    self.state.command_palette.selected.saturating_sub(1);
            }
            KeyCode::Enter => {
                let filtered = self.state.command_palette.filtered();
                if let Some(item) = filtered.get(self.state.command_palette.selected) {
                    match &item.exec {
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
            KeyCode::Char(ch) => {
                self.state.command_palette.query.push(ch);
                self.state.command_palette.selected = 0;
            }
            KeyCode::Backspace => {
                self.state.command_palette.query.pop();
                self.state.command_palette.selected = 0;
            }
            _ => {}
        }
        false
    }

    fn handle_session_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        match key.code {
            KeyCode::Esc => {
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            KeyCode::Down => {
                let sessions = &self.state.session.sessions;
                if !sessions.is_empty() {
                    self.state.session_list.selected =
                        (self.state.session_list.selected + 1) % sessions.len();
                }
            }
            KeyCode::Up => {
                self.state.session_list.selected =
                    self.state.session_list.selected.saturating_sub(1);
            }
            KeyCode::Enter => {
                if let Some(session) = self.state.session.sessions.get(self.state.session_list.selected) {
                    let session_id = session.id;
                    if self.state.session.switch_to(session_id).is_ok() {
                        // Restore messages from the loaded session
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
                        }
                    }
                }
                self.state.session_list.open = false;
                self.state.active_modal = None;
            }
            KeyCode::Char(ch) => {
                self.state.session_list.query.push(ch);
                self.state.session_list.selected = 0;
            }
            KeyCode::Backspace => {
                self.state.session_list.query.pop();
                self.state.session_list.selected = 0;
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
                // Any key moves to action selection
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
                    // Approve all pending
                    while !self.state.permission.queue.is_empty() {
                        self.state.permission.approve_current_once();
                    }
                    self.state.active_modal = None;
                    self.set_status("YOLO mode enabled", StatusLevel::Info);
                }
                KeyCode::Esc => {
                    // Cancel/reject without reason
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
                    // Cancel rejection, go back
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

        match key.code {
            KeyCode::Esc => {
                self.state.model_selector = None;
                self.state.active_modal = None;
            }
            KeyCode::Down => {
                let count = selector.filtered().len();
                if count > 0 {
                    selector.selected = (selector.selected + 1) % count;
                }
            }
            KeyCode::Up => {
                selector.selected = selector.selected.saturating_sub(1);
            }
            KeyCode::Enter => {
                let filtered = selector.filtered();
                if let Some(item) = filtered.get(selector.selected) {
                    let provider = item.provider.clone();
                    let model = item.model.clone();

                    let display = item.display.clone();

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
            KeyCode::Char(ch) => {
                selector.query.push(ch);
                selector.selected = 0;
            }
            KeyCode::Backspace => {
                selector.query.pop();
                selector.selected = 0;
            }
            _ => {}
        }
        false
    }

    fn handle_tool_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        match key.code {
            KeyCode::Esc => {
                self.state.active_modal = None;
            }
            KeyCode::Down => {
                let count = self.state.tool_list.filtered().len();
                if count > 0 {
                    self.state.tool_list.selected =
                        (self.state.tool_list.selected + 1) % count;
                }
            }
            KeyCode::Up => {
                self.state.tool_list.selected =
                    self.state.tool_list.selected.saturating_sub(1);
            }
            KeyCode::Char(ch) => {
                self.state.tool_list.query.push(ch);
                self.state.tool_list.selected = 0;
            }
            KeyCode::Backspace => {
                self.state.tool_list.query.pop();
                self.state.tool_list.selected = 0;
            }
            _ => {}
        }
        false
    }

    fn handle_provider_connect_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let state = match self.state.provider_connect {
            Some(ref mut s) => s,
            None => {
                self.state.active_modal = None;
                return false;
            }
        };

        match &state.screen.clone() {
            ConnectScreen::List => match key.code {
                KeyCode::Esc => {
                    self.state.provider_connect = None;
                    self.state.active_modal = None;
                }
                KeyCode::Down => {
                    let count = state.providers.len();
                    if count > 0 {
                        state.selected = (state.selected + 1) % count;
                    }
                }
                KeyCode::Up => {
                    state.selected = state.selected.saturating_sub(1);
                }
                KeyCode::Enter => {
                    if let Some(provider) = state.providers.get(state.selected) {
                        let provider_id = provider.id.clone();
                        let auth_flow = provider.auth_flow;

                        match auth_flow {
                            ava_auth::config::AuthFlow::Pkce => {
                                // Start PKCE browser OAuth flow
                                let pkce = ava_auth::pkce::generate_pkce();
                                if let Some(cfg) = ava_auth::config::oauth_config(&provider_id) {
                                    let auth_url = ava_auth::config::build_auth_url(cfg, &pkce);
                                    let _ = ava_auth::browser::open_browser(&auth_url);
                                    state.screen = ConnectScreen::OAuthBrowser {
                                        provider_id,
                                        auth_url,
                                        elapsed_secs: 0,
                                    };
                                    state.message = None;
                                }
                            }
                            ava_auth::config::AuthFlow::DeviceCode => {
                                // Start device code flow
                                let pid = provider_id.clone();
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
                                        state.screen = ConnectScreen::DeviceCode {
                                            provider_id,
                                            user_code: device.user_code,
                                            verification_uri: device.verification_uri,
                                            elapsed_secs: 0,
                                        };
                                        state.message = None;
                                    }
                                    Err(err) => {
                                        state.message = Some(format!("Failed: {err}"));
                                    }
                                }
                            }
                            ava_auth::config::AuthFlow::ApiKey => {
                                // Standard API key input
                                let base_url = {
                                    let credentials = tokio::task::block_in_place(|| {
                                        tokio::runtime::Handle::current()
                                            .block_on(ava_config::CredentialStore::load_default())
                                    });
                                    credentials
                                        .ok()
                                        .and_then(|c| c.get(&provider_id))
                                        .and_then(|c| c.base_url)
                                        .unwrap_or_default()
                                };
                                state.screen = ConnectScreen::Configure(provider_id);
                                state.key_input.clear();
                                state.base_url_input = base_url;
                                state.active_field = ConnectField::ApiKey;
                                state.message = None;
                            }
                        }
                    }
                }
                KeyCode::Char('d') => {
                    // Disconnect selected provider
                    if let Some(provider) = state.providers.get(state.selected) {
                        let provider_id = provider.id.clone();
                        let result = tokio::task::block_in_place(|| {
                            tokio::runtime::Handle::current().block_on(async {
                                let mut store = ava_config::CredentialStore::load_default()
                                    .await
                                    .unwrap_or_default();
                                ava_config::execute_credential_command(
                                    ava_config::CredentialCommand::Remove {
                                        provider: provider_id.clone(),
                                    },
                                    &mut store,
                                )
                                .await
                            })
                        });
                        match result {
                            Ok(msg) => {
                                self.set_status(&msg, StatusLevel::Info);
                                // Refresh provider list
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
                    }
                }
                KeyCode::Char('t') => {
                    // Test selected provider
                    if let Some(provider) = state.providers.get(state.selected) {
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
                    }
                }
                _ => {}
            },
            ConnectScreen::Configure(provider_id) => match key.code {
                KeyCode::Esc => {
                    // Go back to list
                    let state = self.state.provider_connect.as_mut().unwrap();
                    state.screen = ConnectScreen::List;
                    state.key_input.clear();
                    state.base_url_input.clear();
                    state.message = None;
                }
                KeyCode::Tab | KeyCode::BackTab => {
                    let state = self.state.provider_connect.as_mut().unwrap();
                    state.active_field = match state.active_field {
                        ConnectField::ApiKey => ConnectField::BaseUrl,
                        ConnectField::BaseUrl => ConnectField::ApiKey,
                    };
                }
                KeyCode::Enter => {
                    let state = self.state.provider_connect.as_mut().unwrap();
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
                    let state = self.state.provider_connect.as_mut().unwrap();
                    match state.active_field {
                        ConnectField::ApiKey => state.key_input.push(ch),
                        ConnectField::BaseUrl => state.base_url_input.push(ch),
                    }
                }
                KeyCode::Backspace => {
                    let state = self.state.provider_connect.as_mut().unwrap();
                    match state.active_field {
                        ConnectField::ApiKey => { state.key_input.pop(); }
                        ConnectField::BaseUrl => { state.base_url_input.pop(); }
                    }
                }
                _ => {}
            },
            ConnectScreen::OAuthBrowser { .. } => match key.code {
                KeyCode::Esc => {
                    let state = self.state.provider_connect.as_mut().unwrap();
                    state.screen = ConnectScreen::List;
                    state.message = Some("OAuth flow cancelled".to_string());
                }
                _ => {}
            },
            ConnectScreen::DeviceCode { verification_uri, .. } => match key.code {
                KeyCode::Esc => {
                    let state = self.state.provider_connect.as_mut().unwrap();
                    state.screen = ConnectScreen::List;
                    state.message = Some("Device code flow cancelled".to_string());
                }
                KeyCode::Enter => {
                    // Open browser to verification URI
                    let _ = ava_auth::browser::open_browser(verification_uri);
                }
                _ => {}
            },
        }
        false
    }
}
