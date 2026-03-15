use super::*;

impl App {
    pub(super) fn handle_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) -> bool {
        // Ctrl+Z ends btw branch (no-op when btw is not active)
        if key
            .modifiers
            .contains(crossterm::event::KeyModifiers::CONTROL)
            && key.code == KeyCode::Char('z')
        {
            if self.state.btw.active {
                self.end_btw_branch();
            }
            return false;
        }

        if let Some(modal) = self.state.active_modal {
            return self.handle_modal_key(modal, key, app_tx);
        }

        if key.code == KeyCode::Esc
            && matches!(
                self.state.view_mode,
                ViewMode::SubAgent { .. }
                    | ViewMode::BackgroundTask { .. }
                    | ViewMode::PraxisTask { .. }
            )
        {
            self.state.view_mode = ViewMode::Main;
            self.state.messages.reset_scroll();
            return false;
        }

        if key.code == KeyCode::Esc && !self.state.agent.is_running {
            if let Some(last) = self.last_esc_time {
                if last.elapsed() < std::time::Duration::from_millis(500) {
                    self.last_esc_time = None;
                    self.open_rewind_modal();
                    return false;
                }
            }
            self.last_esc_time = Some(std::time::Instant::now());
        }

        if let Some(action) = self.state.keybinds.action_for(key) {
            match action {
                Action::Quit => return true,
                Action::Cancel => {
                    if let ViewMode::PraxisTask { task_id, .. } = self.state.view_mode {
                        if let Some(task) = self.state.praxis.task_mut(task_id) {
                            if let Some(cancel) = task.cancel.take() {
                                cancel.cancel();
                                task.status = crate::state::praxis::PraxisTaskStatus::Failed;
                                task.messages.push(UiMessage::new(
                                    MessageKind::System,
                                    "Praxis task cancelled by user.",
                                ));
                                self.set_status(
                                    format!("Praxis task #{task_id} cancelled"),
                                    StatusLevel::Warn,
                                );
                                return false;
                            }
                        }
                    }
                    if self.state.agent.is_running {
                        self.state.agent.abort();
                        self.state.input.queue_display.clear_steering();
                    } else if !self.state.input.buffer.is_empty() {
                        self.state.input.clear();
                    } else {
                        return true;
                    }
                }
                Action::ScrollUp => {
                    let half_page = (self.state.messages.visible_height / 2).max(1);
                    self.state.messages.scroll_up(half_page);
                }
                Action::ScrollDown => {
                    let half_page = (self.state.messages.visible_height / 2).max(1);
                    self.state.messages.scroll_down(half_page);
                }
                Action::ScrollTop => {
                    if !self.state.input.buffer.is_empty() {
                        self.state.input.move_home();
                    } else {
                        self.state.messages.scroll_to_top();
                    }
                }
                Action::ScrollBottom => {
                    if !self.state.input.buffer.is_empty() {
                        self.state.input.move_end();
                    } else {
                        self.state.messages.scroll_to_bottom();
                    }
                }
                Action::ToggleSidebar => self.state.show_sidebar = !self.state.show_sidebar,
                Action::ModeNext => {
                    self.state.agent_mode = self.state.agent_mode.cycle_next();
                    self.state.agent.set_mode(self.state.agent_mode);
                    self.set_status(
                        format!("Mode: {}", self.state.agent_mode.label()),
                        StatusLevel::Info,
                    );
                }
                Action::ModePrev => {
                    self.state.agent_mode = self.state.agent_mode.cycle_prev();
                    self.state.agent.set_mode(self.state.agent_mode);
                    self.set_status(
                        format!("Mode: {}", self.state.agent_mode.label()),
                        StatusLevel::Info,
                    );
                }
                Action::PermissionToggle => {
                    self.state.permission.permission_level =
                        self.state.permission.permission_level.toggle();
                    self.set_status(
                        format!(
                            "Permissions: {}",
                            self.state.permission.permission_level.label()
                        ),
                        StatusLevel::Info,
                    );
                }
                Action::CommandPalette => {
                    self.state.command_palette.open = true;
                    self.state.command_palette.list.query.clear();
                    self.state.command_palette.list.selected = 0;
                    self.state.active_modal = Some(ModalType::CommandPalette);
                }
                Action::ModelSwitch => {
                    self.execute_command_action(Action::ModelSwitch, Some(app_tx.clone()));
                }
                Action::NewSession => {
                    let _ = self.state.session.create_session();
                    self.state.agent.clear_session_metrics();
                    self.state.messages.messages.clear();
                    self.set_status("New session created", StatusLevel::Info);
                }
                Action::SessionList => {
                    self.execute_command_action(Action::SessionList, Some(app_tx.clone()));
                }
                Action::ToggleThinking => {
                    self.state.agent.cycle_thinking();
                }
                Action::ExpandThinking => {
                    self.state.messages.toggle_all_thinking();
                }
                Action::VoiceToggle => {
                    #[cfg(feature = "voice")]
                    self.toggle_voice(app_tx);
                }
                Action::CopyLastResponse => {
                    self.copy_last_response();
                }
                Action::BackgroundAgent => {
                    if self.state.agent.is_running {
                        self.background_current_agent(app_tx.clone());
                    } else {
                        self.set_status(
                            "No running agent to background (use /bg <goal>)",
                            StatusLevel::Warn,
                        );
                    }
                }
                Action::SubmitFollowUp => {
                    if self.state.agent.is_running {
                        if let Some(text) = self.state.input.submit() {
                            self.send_queued_message(text, ava_types::MessageTier::FollowUp);
                        }
                    } else {
                        self.state.input.insert_char('\n');
                    }
                }
                Action::SubmitPostComplete => {
                    if self.state.agent.is_running {
                        if let Some(text) = self.state.input.submit() {
                            let group = self
                                .state
                                .agent
                                .message_tx
                                .as_ref()
                                .map(|_| 1u32)
                                .unwrap_or(1);
                            self.send_queued_message(
                                text,
                                ava_types::MessageTier::PostComplete { group },
                            );
                        }
                    } else {
                        self.set_status(
                            "No running agent — post-complete messages require a running agent",
                            StatusLevel::Warn,
                        );
                    }
                }
                _ => {}
            }
            return false;
        }

        if self.state.input.has_slash_autocomplete() {
            match key.code {
                KeyCode::Esc => {
                    self.state.input.dismiss_autocomplete();
                    return false;
                }
                KeyCode::Up => {
                    self.state.input.autocomplete_prev();
                    return false;
                }
                KeyCode::Down => {
                    self.state.input.autocomplete_next();
                    return false;
                }
                KeyCode::Tab => {
                    if let Some(value) = self.state.input.autocomplete_selected_value() {
                        let completed = format!("/{}", value);
                        self.state.input.buffer = completed.clone();
                        self.state.input.cursor = completed.len();
                        self.state.input.autocomplete = None;
                    }
                    return false;
                }
                KeyCode::Enter if key.modifiers == KeyModifiers::NONE => {
                    if let Some(value) = self.state.input.autocomplete_selected_value() {
                        let cmd = format!("/{}", value);
                        self.state.input.clear();
                        if let Some((kind, msg)) =
                            self.handle_slash_command(&cmd, Some(app_tx.clone()))
                        {
                            self.state.messages.push(UiMessage::new(kind, msg));
                        }
                    }
                    return false;
                }
                _ => {}
            }
        }

        if self.state.input.has_mention_autocomplete() {
            match key.code {
                KeyCode::Esc => {
                    self.state.input.autocomplete = None;
                    return false;
                }
                KeyCode::Up => {
                    self.state.input.autocomplete_prev();
                    return false;
                }
                KeyCode::Down => {
                    self.state.input.autocomplete_next();
                    return false;
                }
                KeyCode::Tab | KeyCode::Enter if key.modifiers == KeyModifiers::NONE => {
                    if let Some(value) = self.state.input.autocomplete_selected_value() {
                        let is_folder = value.ends_with('/');
                        let is_codebase = value.starts_with("codebase:");
                        let attachment = if is_codebase {
                            let query = value.strip_prefix("codebase:").unwrap_or(&value);
                            ava_types::ContextAttachment::CodebaseQuery {
                                query: query.to_string(),
                            }
                        } else if is_folder {
                            ava_types::ContextAttachment::Folder {
                                path: std::path::PathBuf::from(value.trim_end_matches('/')),
                            }
                        } else {
                            ava_types::ContextAttachment::File {
                                path: std::path::PathBuf::from(&value),
                            }
                        };

                        let before_cursor = &self.state.input.buffer[..self.state.input.cursor];
                        if let Some(at_pos) = before_cursor.rfind('@') {
                            let after_cursor =
                                self.state.input.buffer[self.state.input.cursor..].to_string();
                            self.state.input.buffer =
                                format!("{}{}", &self.state.input.buffer[..at_pos], after_cursor,);
                            self.state.input.cursor = at_pos;
                        }
                        self.state.input.autocomplete = None;
                        self.state.input.add_attachment(attachment);
                    }
                    return false;
                }
                _ => {}
            }
        }

        if key.code == KeyCode::Tab && key.modifiers == KeyModifiers::NONE {
            self.state.agent_mode = self.state.agent_mode.cycle_next();
            self.state.agent.set_mode(self.state.agent_mode);
            self.set_status(
                format!("Mode: {}", self.state.agent_mode.label()),
                StatusLevel::Info,
            );
            return false;
        }
        if key.code == KeyCode::BackTab {
            self.state.agent_mode = self.state.agent_mode.cycle_prev();
            self.state.agent.set_mode(self.state.agent_mode);
            self.set_status(
                format!("Mode: {}", self.state.agent_mode.label()),
                StatusLevel::Info,
            );
            return false;
        }

        match key.code {
            KeyCode::Enter if key.modifiers == KeyModifiers::NONE => {
                if self.state.agent.is_running {
                    if let Some(text) = self.state.input.submit() {
                        self.send_queued_message(text, ava_types::MessageTier::Steering);
                    }
                } else {
                    if self.state.input.buffer.is_empty()
                        && matches!(self.state.view_mode, ViewMode::Main)
                    {
                        let last_completed = self
                            .state
                            .agent
                            .sub_agents
                            .iter()
                            .rposition(|sa| !sa.is_running && !sa.session_messages.is_empty());
                        if let Some(idx) = last_completed {
                            self.enter_sub_agent_view(idx);
                            return false;
                        }
                    }
                    if let Some(goal) = self.state.input.submit() {
                        self.submit_goal(goal, app_tx, agent_tx);
                    }
                }
            }
            KeyCode::Enter => self.state.input.insert_char('\n'),
            KeyCode::Char('o') if key.modifiers == KeyModifiers::CONTROL => {
                if self.state.input.toggle_paste_expansion() {
                    self.set_status("Paste expanded inline", StatusLevel::Info);
                }
            }
            KeyCode::Char(ch)
                if key.modifiers == KeyModifiers::NONE || key.modifiers == KeyModifiers::SHIFT =>
            {
                self.state.input.insert_char(ch)
            }
            KeyCode::Backspace => self.state.input.delete_backward_with_paste(),
            KeyCode::Delete => self.state.input.delete_forward(),
            KeyCode::Left => self.state.input.move_left(),
            KeyCode::Right => self.state.input.move_right(),
            KeyCode::Up => {
                if self.state.input.buffer.is_empty() {
                    self.state.messages.scroll_up(1);
                } else if !self.state.input.move_up() {
                    self.state.input.history_up();
                }
            }
            KeyCode::Down => {
                if self.state.input.buffer.is_empty() {
                    self.state.messages.scroll_down(1);
                } else if !self.state.input.move_down() {
                    self.state.input.history_down();
                }
            }
            _ => {}
        }

        false
    }
}
