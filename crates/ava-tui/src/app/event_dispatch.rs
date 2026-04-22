use super::*;

impl App {
    pub(super) fn handle_event(
        &mut self,
        event: AppEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        match event {
            AppEvent::Quit => self.should_quit = true,
            AppEvent::Key(key) if key.kind == KeyEventKind::Press => {
                if self.handle_key(key, app_tx, agent_tx) {
                    self.should_quit = true;
                }
            }
            AppEvent::Key(_) => {}
            AppEvent::Paste(value) => {
                if self.state.active_modal.is_some() {
                    self.handle_modal_paste(&value);
                } else {
                    self.state.input.handle_paste(value);
                }
            }
            AppEvent::Resize(_, _) => {
                // Terminal resized — flag for full clear before next draw
                self.needs_clear = true;
            }
            AppEvent::Mouse(mouse) => {
                use crossterm::event::MouseEventKind;
                self.state.mouse_position = Some(MousePosition {
                    column: mouse.column,
                    row: mouse.row,
                });
                if let Some(modal) = self.state.active_modal {
                    self.handle_modal_mouse(modal, mouse, app_tx.clone());
                } else {
                    match mouse.kind {
                        MouseEventKind::ScrollUp => self.state.messages.scroll_up(1),
                        MouseEventKind::ScrollDown => self.state.messages.scroll_down(1),
                        MouseEventKind::Down(crossterm::event::MouseButton::Left) => {
                            if let Some(target) = self
                                .state
                                .sidebar_click_targets
                                .iter()
                                .find(|target| {
                                    target.x.contains(&mouse.column)
                                        && target.y.contains(&mouse.row)
                                })
                                .cloned()
                            {
                                match target.action {
                                    SidebarClickAction::ToggleMcpServer { name, enabled } => {
                                        let result = if enabled {
                                            tokio::task::block_in_place(|| {
                                                tokio::runtime::Handle::current().block_on(
                                                    self.state.agent.mcp_disable_server(&name),
                                                )
                                            })
                                        } else {
                                            tokio::task::block_in_place(|| {
                                                tokio::runtime::Handle::current().block_on(
                                                    self.state.agent.mcp_enable_server(&name),
                                                )
                                            })
                                        };
                                        match result {
                                            Ok(true) => {
                                                self.state.mcp_servers =
                                                    tokio::task::block_in_place(|| {
                                                        tokio::runtime::Handle::current()
                                                            .block_on(
                                                                self.state.agent.mcp_server_info(),
                                                            )
                                                            .unwrap_or_default()
                                                    });
                                                self.set_status(
                                                    if enabled {
                                                        format!("Disabled MCP server: {name}")
                                                    } else {
                                                        format!("Enabled MCP server: {name}")
                                                    },
                                                    StatusLevel::Info,
                                                );
                                            }
                                            Ok(false) => {
                                                self.set_status(
                                                    format!("No change for MCP server: {name}"),
                                                    StatusLevel::Warn,
                                                );
                                            }
                                            Err(err) => {
                                                self.set_status(
                                                    format!(
                                                        "Failed to update MCP server {name}: {err}"
                                                    ),
                                                    StatusLevel::Error,
                                                );
                                            }
                                        }
                                    }
                                    SidebarClickAction::RefreshLsp => {
                                        if !self.lsp_refresh_inflight {
                                            self.lsp_refresh_inflight = true;
                                            self.last_lsp_refresh_at =
                                                Some(std::time::Instant::now());
                                            self.spawn_lsp_sidebar_refresh(app_tx.clone());
                                        }
                                    }
                                    SidebarClickAction::OpenSubAgent { index } => {
                                        self.enter_sub_agent_view(index);
                                    }
                                }
                                return;
                            }
                            if let Some(idx) = self.state.messages.message_index_at_row(mouse.row) {
                                enum ClickedMessage {
                                    Main(crate::state::messages::MessageKind),
                                    SubAgent {
                                        call_id: String,
                                        session_id: Option<String>,
                                        description: String,
                                    },
                                }

                                let clicked_message = match &self.state.view_mode {
                                    ViewMode::Main => {
                                        self.state.messages.messages.get(idx).map(|msg| {
                                            match msg.kind.clone() {
                                                crate::state::messages::MessageKind::SubAgent => {
                                                    ClickedMessage::SubAgent {
                                                        call_id: msg
                                                            .sub_agent
                                                            .as_ref()
                                                            .map(|sub| sub.call_id.clone())
                                                            .unwrap_or_default(),
                                                        session_id: msg
                                                            .sub_agent
                                                            .as_ref()
                                                            .and_then(|sub| sub.session_id.clone()),
                                                        description: msg
                                                            .sub_agent
                                                            .as_ref()
                                                            .map(|sub| sub.description.clone())
                                                            .unwrap_or_else(|| msg.content.clone()),
                                                    }
                                                }
                                                kind => ClickedMessage::Main(kind),
                                            }
                                        })
                                    }
                                    ViewMode::SubAgent { agent_index, .. } => self
                                        .state
                                        .agent
                                        .sub_agents
                                        .get(*agent_index)
                                        .and_then(|sa| sa.session_messages.get(idx))
                                        .map(|msg| match msg.kind.clone() {
                                            crate::state::messages::MessageKind::SubAgent => {
                                                ClickedMessage::SubAgent {
                                                    call_id: msg
                                                        .sub_agent
                                                        .as_ref()
                                                        .map(|sub| sub.call_id.clone())
                                                        .unwrap_or_default(),
                                                    session_id: msg
                                                        .sub_agent
                                                        .as_ref()
                                                        .and_then(|sub| sub.session_id.clone()),
                                                    description: msg
                                                        .sub_agent
                                                        .as_ref()
                                                        .map(|sub| sub.description.clone())
                                                        .unwrap_or_else(|| msg.content.clone()),
                                                }
                                            }
                                            kind => ClickedMessage::Main(kind),
                                        }),
                                    ViewMode::BackgroundTask { task_id, .. } => {
                                        let bg = self
                                            .state
                                            .background
                                            .lock()
                                            .unwrap_or_else(|e| e.into_inner());
                                        bg.tasks
                                            .iter()
                                            .find(|task| task.id == *task_id)
                                            .and_then(|task| task.messages.get(idx))
                                            .map(|msg| match msg.kind.clone() {
                                                crate::state::messages::MessageKind::SubAgent => {
                                                    ClickedMessage::SubAgent {
                                                        call_id: msg
                                                            .sub_agent
                                                            .as_ref()
                                                            .map(|sub| sub.call_id.clone())
                                                            .unwrap_or_default(),
                                                        session_id: msg
                                                            .sub_agent
                                                            .as_ref()
                                                            .and_then(|sub| sub.session_id.clone()),
                                                        description: msg
                                                            .sub_agent
                                                            .as_ref()
                                                            .map(|sub| sub.description.clone())
                                                            .unwrap_or_else(|| msg.content.clone()),
                                                    }
                                                }
                                                kind => ClickedMessage::Main(kind),
                                            })
                                    }
                                };

                                if let Some(clicked_message) = clicked_message {
                                    match clicked_message {
                                        ClickedMessage::SubAgent {
                                            call_id,
                                            session_id,
                                            description,
                                        } => {
                                            let agent_index = if !call_id.is_empty() {
                                                self.state
                                                    .agent
                                                    .sub_agents
                                                    .iter()
                                                    .rposition(|sa| sa.call_id == call_id)
                                            } else if let Some(session_id) = session_id {
                                                self.state.agent.sub_agents.iter().rposition(|sa| {
                                                    sa.session_id.as_deref()
                                                        == Some(session_id.as_str())
                                                })
                                            } else {
                                                self.state.agent.sub_agents.iter().rposition(|sa| {
                                                    subagent_descriptions_match(
                                                        &sa.description,
                                                        &description,
                                                    )
                                                })
                                            };
                                            if let Some(agent_index) = agent_index {
                                                self.enter_sub_agent_view(agent_index);
                                            }
                                        }
                                        ClickedMessage::Main(
                                            crate::state::messages::MessageKind::Thinking,
                                        ) => {
                                            self.toggle_active_thinking_at(idx);
                                        }
                                        ClickedMessage::Main(
                                            crate::state::messages::MessageKind::ToolCall
                                            | crate::state::messages::MessageKind::ToolResult,
                                        ) => {
                                            self.toggle_active_tool_group_at(idx);
                                        }
                                        ClickedMessage::Main(_) => {}
                                    }
                                }
                            }
                        }
                        _ => {}
                    }
                }
            }
            AppEvent::Tick => {
                self.flush_token_buffer();
                if self.state.feature_lsp_enabled && !self.lsp_refresh_inflight {
                    let should_refresh = self
                        .last_lsp_refresh_at
                        .map(|at| at.elapsed() >= std::time::Duration::from_secs(3))
                        .unwrap_or(true);
                    if should_refresh {
                        self.lsp_refresh_inflight = true;
                        self.last_lsp_refresh_at = Some(std::time::Instant::now());
                        self.spawn_lsp_sidebar_refresh(app_tx.clone());
                    }
                }
                if let Some(ref msg) = self.state.status_message {
                    if msg.is_expired() {
                        self.state.status_message = None;
                    }
                }
                if let Some(ref todo_state) = self.state.todo_state {
                    self.state.todo_items = todo_state.get();
                }
                {
                    let mut bg = self
                        .state
                        .background
                        .lock()
                        .unwrap_or_else(|e| e.into_inner());
                    bg.expire_notification();
                    if let Some((ref text, _)) = bg.notification {
                        let should_set = self
                            .state
                            .status_message
                            .as_ref()
                            .map(|m| m.text != *text)
                            .unwrap_or(true);
                        if should_set {
                            let text = text.clone();
                            drop(bg);
                            let is_failure = text.contains("failed");
                            let level = if is_failure {
                                StatusLevel::Error
                            } else {
                                StatusLevel::Info
                            };
                            self.set_status(text, level);
                        }
                    }
                }
            }
            AppEvent::AgentRunEvent { .. }
            | AppEvent::AgentRunDone { .. }
            | AppEvent::BackgroundCleanupResult { .. }
            | AppEvent::TokenUsage(..)
            | AppEvent::ModelSelectorLoaded(..)
            | AppEvent::ModelSwitchFinished(..)
            | AppEvent::ToolListLoaded(..)
            | AppEvent::McpServersLoaded(..)
            | AppEvent::LspEntriesLoaded(..)
            | AppEvent::CommandMessage(..)
            | AppEvent::SessionListLoaded(..)
            | AppEvent::SessionLoaded(..)
            | AppEvent::ProviderConnectLoaded(..)
            | AppEvent::ProviderConnectFinished(..)
            | AppEvent::ShellResult(..)
            | AppEvent::VoiceReady(..)
            | AppEvent::VoiceError(..)
            | AppEvent::VoiceAmplitude(..)
            | AppEvent::VoiceSilenceDetected
            | AppEvent::OAuthSuccess { .. }
            | AppEvent::OAuthError { .. }
            | AppEvent::InteractiveRequestCleared { .. }
            | AppEvent::HookResult { .. }
            | AppEvent::ReviewFinished(..) => {
                self.handle_runtime_event(event, app_tx, agent_tx);
            }
        }
    }
}
