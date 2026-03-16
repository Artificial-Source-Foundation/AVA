use super::*;
use crate::widgets::command_palette::CommandExec;
use crate::widgets::provider_connect::{ConnectField, ConnectScreen};
use crate::widgets::select_list::{
    handle_select_list_key, handle_select_list_mouse, list_viewport_height, SelectListAction,
    SelectListMouseAction,
};
use ava_auth::config::AuthFlow;
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
            Some(ModalType::ThemeSelector) => {
                if let Some(ref mut ts) = self.state.theme_selector {
                    ts.query.push_str(value);
                }
            }
            Some(ModalType::AgentList) => {
                if let Some(ref mut al) = self.state.agent_list {
                    al.query.push_str(value);
                }
            }
            Some(ModalType::Question) => {
                if let Some(ref mut q) = self.state.question {
                    if q.options.is_empty() {
                        q.input.push_str(value);
                    }
                }
            }
            Some(ModalType::Rewind | ModalType::DiffPreview | ModalType::InfoPanel) => {
                // These modals have no text input — ignore paste
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
            ModalType::CommandPalette => self.handle_command_palette_key(key, app_tx.clone()),
            ModalType::SessionList => self.handle_session_list_key(key, app_tx),
            ModalType::ToolApproval => self.handle_tool_approval_key(key),
            ModalType::ModelSelector => self.handle_model_selector_key(key, app_tx),
            ModalType::ToolList => self.handle_tool_list_key(key),
            ModalType::ProviderConnect => self.handle_provider_connect_key(key, app_tx),
            ModalType::ThemeSelector => self.handle_theme_selector_key(key),
            ModalType::AgentList => self.handle_agent_list_key(key),
            ModalType::Question => self.handle_question_key(key),
            ModalType::CopyPicker => self.handle_copy_picker_key(key),
            ModalType::Rewind => self.handle_rewind_key(key),
            ModalType::TaskList => self.handle_task_list_key(key),
            ModalType::DiffPreview => self.handle_diff_preview_key(key),
            ModalType::InfoPanel => self.handle_info_panel_key(key),
        }
    }

    /// Route mouse events to the active modal's SelectListState for hover/click/scroll.
    pub(crate) fn handle_modal_mouse(
        &mut self,
        modal: ModalType,
        mouse: crossterm::event::MouseEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let vh = list_viewport_height(modal_viewport_height());

        match modal {
            ModalType::CommandPalette => {
                let action =
                    handle_select_list_mouse(&mut self.state.command_palette.list, mouse, vh);
                if action == SelectListMouseAction::Clicked {
                    // Simulate Enter — execute the selected command
                    let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                    self.handle_command_palette_key(enter, app_tx);
                }
            }
            ModalType::SessionList => {
                let action = handle_select_list_mouse(&mut self.state.session_list.list, mouse, vh);
                if action == SelectListMouseAction::Clicked {
                    let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                    self.handle_session_list_key(enter, app_tx);
                }
            }
            ModalType::ModelSelector => {
                if let Some(ref mut selector) = self.state.model_selector {
                    let action = handle_select_list_mouse(&mut selector.list, mouse, vh);
                    if action == SelectListMouseAction::Clicked {
                        let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                        self.handle_model_selector_key(enter, app_tx);
                    }
                }
            }
            ModalType::ThemeSelector => {
                if let Some(ref mut selector) = self.state.theme_selector {
                    let action = handle_select_list_mouse(selector, mouse, vh);
                    if action == SelectListMouseAction::Clicked {
                        let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                        self.handle_theme_selector_key(enter);
                    } else if action == SelectListMouseAction::Scrolled {
                        // Live preview on scroll (same as Moved in key handler)
                        if let Some(name) = self
                            .state
                            .theme_selector
                            .as_ref()
                            .and_then(|s| s.selected_value().cloned())
                        {
                            self.state.theme = Theme::from_name(&name);
                        }
                    }
                }
            }
            ModalType::ToolList => {
                handle_select_list_mouse(&mut self.state.tool_list.list, mouse, vh);
            }
            ModalType::ProviderConnect => {
                if let Some(ref mut pc) = self.state.provider_connect {
                    if matches!(pc.screen, ConnectScreen::List) {
                        let action = handle_select_list_mouse(&mut pc.list, mouse, vh);
                        if action == SelectListMouseAction::Clicked {
                            let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                            self.handle_provider_connect_key(enter, app_tx);
                        }
                    }
                }
            }
            ModalType::AgentList => {
                if let Some(ref mut selector) = self.state.agent_list {
                    let action = handle_select_list_mouse(selector, mouse, vh);
                    if action == SelectListMouseAction::Clicked {
                        let enter = crossterm::event::KeyEvent::from(KeyCode::Enter);
                        self.handle_agent_list_key(enter);
                    }
                }
            }
            ModalType::InfoPanel => {
                // Handle mouse scroll for the info panel
                if let crossterm::event::MouseEventKind::ScrollDown = mouse.kind {
                    if let Some(ref mut panel) = self.state.info_panel {
                        let content_lines = panel.content.lines().count() as u16;
                        let vh = modal_viewport_height() as u16;
                        let max_scroll = content_lines.saturating_sub(vh.saturating_sub(2));
                        panel.scroll = (panel.scroll + 3).min(max_scroll);
                    }
                } else if let crossterm::event::MouseEventKind::ScrollUp = mouse.kind {
                    if let Some(ref mut panel) = self.state.info_panel {
                        panel.scroll = panel.scroll.saturating_sub(3);
                    }
                }
            }
            // Non-SelectList modals — no mouse handling
            ModalType::ToolApproval
            | ModalType::Question
            | ModalType::CopyPicker
            | ModalType::Rewind
            | ModalType::TaskList
            | ModalType::DiffPreview => {}
        }
    }

    fn handle_command_palette_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
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
                            self.execute_command_action(action, Some(app_tx.clone()));
                        }
                        CommandExec::Slash(cmd) => {
                            let cmd = cmd.clone();
                            self.state.command_palette.open = false;
                            self.state.active_modal = None;
                            if let Some((kind, msg)) =
                                self.handle_slash_command(&cmd, Some(app_tx.clone()))
                            {
                                self.state.messages.push(UiMessage::new(kind, msg));
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

    fn handle_session_list_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
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
                    } else {
                        self.spawn_session_load(session_id, app_tx.clone());
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
                    self.state.permission.permission_level =
                        crate::state::permission::PermissionLevel::AutoApprove;
                    while !self.state.permission.queue.is_empty() {
                        self.state.permission.approve_current_once();
                    }
                    self.state.active_modal = None;
                    self.set_status("Auto-approve enabled", StatusLevel::Info);
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
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let Some(ref mut selector) = self.state.model_selector else {
            self.state.active_modal = None;
            return false;
        };

        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(&mut selector.list, key, vh);
        match action {
            SelectListAction::Cancelled => {
                self.state.model_selector = None;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                let Some(selector) = self.state.model_selector.as_ref() else {
                    return false;
                };
                if let Some(mv) = selector.list.selected_value() {
                    let provider = mv.provider.clone();
                    let model = mv.model.clone();
                    let display = mv.display.clone();
                    self.spawn_model_switch(
                        provider,
                        model,
                        display,
                        crate::event::ModelSwitchContext::Selector,
                        app_tx,
                    );
                }
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

    fn handle_provider_connect_key(
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

    fn handle_agent_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut selector) = self.state.agent_list else {
            self.state.active_modal = None;
            return false;
        };

        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(selector, key, vh);
        match action {
            SelectListAction::Cancelled | SelectListAction::Selected => {
                self.state.agent_list = None;
                self.state.active_modal = None;
            }
            _ => {}
        }
        false
    }

    fn handle_question_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut q) = self.state.question else {
            self.state.active_modal = None;
            return false;
        };

        if q.options.is_empty() {
            // Free-text input mode
            match key.code {
                KeyCode::Enter => {
                    let answer = q.input.clone();
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(answer);
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
                }
                KeyCode::Esc => {
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(String::new());
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
                }
                KeyCode::Char(ch) => {
                    q.input.push(ch);
                }
                KeyCode::Backspace => {
                    q.input.pop();
                }
                _ => {}
            }
        } else {
            // Options selection mode
            match key.code {
                KeyCode::Up => {
                    q.selected = q.selected.saturating_sub(1);
                }
                KeyCode::Down => {
                    if q.selected + 1 < q.options.len() {
                        q.selected += 1;
                    }
                }
                KeyCode::Enter => {
                    let answer = q.options.get(q.selected).cloned().unwrap_or_default();
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(answer);
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
                }
                KeyCode::Esc => {
                    if let Some(reply) = q.reply.take() {
                        let _ = reply.send(String::new());
                    }
                    self.state.question = None;
                    self.state.active_modal = None;
                }
                _ => {}
            }
        }
        false
    }

    fn handle_copy_picker_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(picker) = self.state.copy_picker.take() else {
            self.state.active_modal = None;
            return false;
        };

        match key.code {
            KeyCode::Esc => {
                self.state.active_modal = None;
            }
            KeyCode::Char('a') => {
                self.state.active_modal = None;
                let line_count = picker.full_content.lines().count();
                self.copy_to_clipboard(
                    &picker.full_content,
                    Some(format!(
                        "Copied entire response ({line_count} lines) to clipboard"
                    )),
                );
            }
            KeyCode::Char(ch) if ch.is_ascii_digit() && ch != '0' => {
                let idx = (ch as usize) - ('1' as usize);
                if let Some(block) = picker.blocks.get(idx) {
                    self.state.active_modal = None;
                    let lang = if block.language.is_empty() {
                        "code"
                    } else {
                        &block.language
                    };
                    let line_count = block.content.lines().count();
                    let label = format!("Copied {lang} block ({line_count} lines) to clipboard");
                    self.copy_to_clipboard(&block.content, Some(label));
                } else {
                    // Index out of range — put picker back
                    self.state.copy_picker = Some(picker);
                }
            }
            _ => {
                // Unrecognized key — put picker back
                self.state.copy_picker = Some(picker);
            }
        }
        false
    }

    fn handle_rewind_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        use crate::state::rewind::RewindOption;

        match key.code {
            KeyCode::Esc => {
                self.state.rewind.close();
                self.state.active_modal = None;
            }
            KeyCode::Up => {
                self.state.rewind.select_prev();
            }
            KeyCode::Down => {
                self.state.rewind.select_next();
            }
            KeyCode::Enter => {
                let option = self.state.rewind.selected();
                self.execute_rewind(option);
            }
            KeyCode::Char('1') => self.execute_rewind(RewindOption::RestoreCodeAndConversation),
            KeyCode::Char('2') => self.execute_rewind(RewindOption::RestoreConversation),
            KeyCode::Char('3') => self.execute_rewind(RewindOption::RestoreCode),
            KeyCode::Char('4') => self.execute_rewind(RewindOption::SummarizeFromHere),
            KeyCode::Char('5') => {
                self.state.rewind.close();
                self.state.active_modal = None;
            }
            _ => {}
        }
        false
    }

    fn handle_theme_selector_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut selector) = self.state.theme_selector else {
            self.state.active_modal = None;
            return false;
        };

        let vh = list_viewport_height(modal_viewport_height());
        let action = handle_select_list_key(selector, key, vh);
        match action {
            SelectListAction::Cancelled => {
                // Revert to the original theme saved before preview
                if let Some(original) = self.state.theme_before_preview.take() {
                    self.state.theme = original;
                }
                self.state.theme_selector = None;
                self.state.active_modal = None;
            }
            SelectListAction::Selected => {
                // Confirm the previewed theme
                if let Some(name) = self
                    .state
                    .theme_selector
                    .as_ref()
                    .and_then(|s| s.selected_value().cloned())
                {
                    self.state.theme = Theme::from_name(&name);
                    self.set_status(format!("Theme: {name}"), StatusLevel::Info);
                }
                self.state.theme_before_preview = None;
                self.state.theme_selector = None;
                self.state.active_modal = None;
            }
            SelectListAction::Moved => {
                // Live preview: apply the highlighted theme immediately
                if let Some(name) = self
                    .state
                    .theme_selector
                    .as_ref()
                    .and_then(|s| s.selected_value().cloned())
                {
                    self.state.theme = Theme::from_name(&name);
                }
            }
            _ => {}
        }
        false
    }

    fn handle_task_list_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        match key.code {
            KeyCode::Esc => {
                self.state.active_modal = None;
            }
            KeyCode::Up => {
                let mut bg = self
                    .state
                    .background
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                bg.select_prev();
            }
            KeyCode::Down => {
                let mut bg = self
                    .state
                    .background
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                bg.select_next();
            }
            KeyCode::Enter => {
                let task_id = {
                    let bg = self
                        .state
                        .background
                        .lock()
                        .unwrap_or_else(|e| e.into_inner());
                    bg.selected_task_id()
                };
                if let Some(id) = task_id {
                    self.state.active_modal = None;
                    self.enter_background_task_view(id);
                }
            }
            _ => {}
        }
        false
    }

    fn handle_diff_preview_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut preview) = self.state.diff_preview else {
            self.state.active_modal = None;
            return false;
        };

        match key.code {
            KeyCode::Char('y') => {
                preview.accept_selected();
            }
            KeyCode::Char('n') => {
                preview.reject_selected();
            }
            KeyCode::Char('a') => {
                preview.accept_all();
            }
            KeyCode::Char('d') => {
                preview.reject_all();
            }
            KeyCode::Char('j') | KeyCode::Down => {
                preview.select_next();
            }
            KeyCode::Char('k') | KeyCode::Up => {
                preview.select_prev();
            }
            KeyCode::Tab => {
                preview.next_file();
            }
            KeyCode::Enter => {
                // Apply accepted hunks
                if let Some(preview) = self.state.diff_preview.take() {
                    let results = preview.apply_accepted();
                    let count = results.len();
                    for (path, content) in results {
                        if let Err(e) = std::fs::write(&path, &content) {
                            self.set_status(
                                format!("Failed to write {}: {e}", path.display()),
                                StatusLevel::Error,
                            );
                        }
                    }
                    if count > 0 {
                        self.set_status(
                            format!("Applied accepted hunks to {count} file(s)"),
                            StatusLevel::Info,
                        );
                    } else {
                        self.set_status("No hunks accepted", StatusLevel::Info);
                    }
                }
                self.state.active_modal = None;
            }
            KeyCode::Esc => {
                // Reject all and close
                self.state.diff_preview = None;
                self.state.active_modal = None;
                self.set_status("Diff preview cancelled", StatusLevel::Info);
            }
            _ => {}
        }
        false
    }

    fn handle_info_panel_key(&mut self, key: crossterm::event::KeyEvent) -> bool {
        let Some(ref mut panel) = self.state.info_panel else {
            self.state.active_modal = None;
            return false;
        };

        let content_lines = panel.content.lines().count() as u16;
        let vh = modal_viewport_height() as u16;
        // Reserve 2 lines for title + footer
        let max_scroll = content_lines.saturating_sub(vh.saturating_sub(2));

        match key.code {
            KeyCode::Esc | KeyCode::Char('q') => {
                self.state.info_panel = None;
                self.state.active_modal = None;
            }
            KeyCode::Down | KeyCode::Char('j') => {
                panel.scroll = (panel.scroll + 1).min(max_scroll);
            }
            KeyCode::Up | KeyCode::Char('k') => {
                panel.scroll = panel.scroll.saturating_sub(1);
            }
            KeyCode::PageDown => {
                panel.scroll = (panel.scroll + vh.saturating_sub(4)).min(max_scroll);
            }
            KeyCode::PageUp => {
                panel.scroll = panel.scroll.saturating_sub(vh.saturating_sub(4));
            }
            KeyCode::Home | KeyCode::Char('g') => {
                panel.scroll = 0;
            }
            KeyCode::End | KeyCode::Char('G') => {
                panel.scroll = max_scroll;
            }
            _ => {}
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
                                match ava_auth::tokens::exchange_code_for_tokens(
                                    &cfg,
                                    &callback.code,
                                    &pkce,
                                )
                                .await
                                {
                                    Ok(tokens) => {
                                        let _ = tx.send(AppEvent::OAuthSuccess {
                                            provider: pid,
                                            tokens,
                                        });
                                    }
                                    Err(e) => {
                                        let _ = tx.send(AppEvent::OAuthError {
                                            provider: pid,
                                            error: e.to_string(),
                                        });
                                    }
                                }
                            }
                            Err(e) => {
                                let _ = tx.send(AppEvent::OAuthError {
                                    provider: pid,
                                    error: e.to_string(),
                                });
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
                state.message = Some("Requesting device code...".to_string());
                let tx = app_tx.clone();
                tokio::spawn(async move {
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
                            let poll_tx = tx.clone();
                            let poll_pid = pid.clone();
                            let device_code = device.device_code.clone();
                            let interval = device.interval;
                            let expires = device.expires_in;
                            tokio::spawn(async move {
                                if let Some(cfg) = ava_auth::config::oauth_config(&poll_pid) {
                                    match ava_auth::device_code::poll_device_code(
                                        cfg,
                                        &device_code,
                                        interval,
                                        expires,
                                    )
                                    .await
                                    {
                                        Ok(Some(tokens)) => {
                                            let _ = poll_tx.send(AppEvent::OAuthSuccess {
                                                provider: poll_pid,
                                                tokens,
                                            });
                                        }
                                        Ok(None) => {
                                            let _ = poll_tx.send(AppEvent::OAuthError {
                                                provider: poll_pid,
                                                error: "Device code expired".to_string(),
                                            });
                                        }
                                        Err(e) => {
                                            let _ = poll_tx.send(AppEvent::OAuthError {
                                                provider: poll_pid,
                                                error: e.to_string(),
                                            });
                                        }
                                    }
                                }
                            });

                            let _ = tx.send(AppEvent::ProviderConnectFinished(
                                crate::event::ProviderConnectResult::DeviceCodeReady {
                                    provider_id: pid,
                                    device,
                                },
                            ));
                        }
                        Err(err) => {
                            let _ = tx.send(AppEvent::ProviderConnectFinished(
                                crate::event::ProviderConnectResult::InlineError(format!(
                                    "Failed: {err}"
                                )),
                            ));
                        }
                    }
                });
            }
            AuthFlow::ApiKey => {
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
