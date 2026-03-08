use super::*;

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
                    self.execute_command_action(item.action);
                }
                self.state.command_palette.open = false;
                self.state.active_modal = None;
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
}
