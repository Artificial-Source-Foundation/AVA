use super::*;

impl App {
    /// Handle slash commands. Returns Some((kind, message)) if handled, None if not a slash command.
    pub(crate) fn handle_slash_command(&mut self, input: &str) -> Option<(MessageKind, String)> {
        let trimmed = input.trim();
        if !trimmed.starts_with('/') {
            return None;
        }

        let parts: Vec<&str> = trimmed.splitn(2, ' ').collect();
        let cmd = parts[0];
        let arg = parts.get(1).map(|s| s.trim());

        match cmd {
            "/model" => {
                if let Some(model_str) = arg {
                    // Parse "provider/model" format — split on first '/' only
                    if let Some((provider, model)) = model_str.split_once('/') {
                        let provider = provider.to_string();
                        let model = model.to_string();
                        let result = tokio::task::block_in_place(|| {
                            tokio::runtime::Handle::current().block_on(
                                self.state.agent.switch_model(&provider, &model)
                            )
                        });
                        match result {
                            Ok(desc) => {
                                self.set_status(format!("Switched to {desc}"), StatusLevel::Info);
                                Some((MessageKind::System, format!("Switched to {desc}")))
                            }
                            Err(err) => {
                                self.set_status(format!("Failed: {err}"), StatusLevel::Error);
                                Some((MessageKind::Error, format!("Failed to switch model: {err}")))
                            }
                        }
                    } else {
                        Some((MessageKind::Error,
                            "Invalid format. Use: /model provider/model (e.g., /model openrouter/anthropic/claude-sonnet-4)".to_string()
                        ))
                    }
                } else {
                    let display = self.state.agent.current_model_display();
                    Some((MessageKind::System, format!("Current model: {display}")))
                }
            }
            "/tools" => {
                match arg {
                    Some("reload") => {
                        let result = tokio::task::block_in_place(|| {
                            tokio::runtime::Handle::current()
                                .block_on(self.state.agent.reload_tools())
                        });
                        match result {
                            Ok(msg) => {
                                self.set_status(&msg, StatusLevel::Info);
                                Some((MessageKind::System, msg))
                            }
                            Err(err) => {
                                self.set_status(format!("Failed: {err}"), StatusLevel::Error);
                                Some((MessageKind::Error, err))
                            }
                        }
                    }
                    Some("init") => {
                        match self.state.agent.create_tool_templates() {
                            Ok(msg) => {
                                self.set_status(&msg, StatusLevel::Info);
                                Some((MessageKind::System, msg))
                            }
                            Err(err) => {
                                self.set_status(format!("Failed: {err}"), StatusLevel::Error);
                                Some((MessageKind::Error, err))
                            }
                        }
                    }
                    _ => {
                        // Show tool list modal
                        let tools = tokio::task::block_in_place(|| {
                            tokio::runtime::Handle::current()
                                .block_on(self.state.agent.list_tools_with_source())
                        });
                        self.state.tool_list = ToolListState {
                            items: tools
                                .into_iter()
                                .map(|(def, src)| {
                                    crate::widgets::tool_list::ToolListItem {
                                        name: def.name,
                                        description: def.description,
                                        source: src,
                                    }
                                })
                                .collect(),
                            selected: 0,
                            query: String::new(),
                            scroll: 0,
                        };
                        self.state.active_modal = Some(ModalType::ToolList);
                        None
                    }
                }
            }
            "/mcp" => {
                match arg {
                    Some("reload") => {
                        let result = tokio::task::block_in_place(|| {
                            tokio::runtime::Handle::current()
                                .block_on(self.state.agent.reload_mcp())
                        });
                        match result {
                            Ok(msg) => {
                                self.set_status(&msg, StatusLevel::Info);
                                Some((MessageKind::System, msg))
                            }
                            Err(err) => {
                                self.set_status(format!("Failed: {err}"), StatusLevel::Error);
                                Some((MessageKind::Error, err))
                            }
                        }
                    }
                    Some("list") | None => {
                        let servers = tokio::task::block_in_place(|| {
                            tokio::runtime::Handle::current()
                                .block_on(self.state.agent.mcp_server_info())
                        });
                        if servers.is_empty() {
                            Some((MessageKind::System, "No MCP servers connected".to_string()))
                        } else {
                            let lines: Vec<String> = servers
                                .iter()
                                .map(|s| format!("  {} ({} tools)", s.name, s.tool_count))
                                .collect();
                            Some((
                                MessageKind::System,
                                format!(
                                    "MCP servers ({}):\n{}",
                                    servers.len(),
                                    lines.join("\n")
                                ),
                            ))
                        }
                    }
                    Some(sub) => Some((
                        MessageKind::Error,
                        format!("Unknown /mcp subcommand: {sub}. Use: list, reload"),
                    )),
                }
            }
            "/help" => {
                let help = "\
Available commands:
  /model [provider/model]  — show or switch model
  /tools                   — list all tools
  /tools reload            — reload tools from disk
  /tools init              — create tool templates
  /mcp list                — show MCP servers
  /mcp reload              — reload MCP config
  /help                    — show this help";
                Some((MessageKind::System, help.to_string()))
            }
            _ => Some((MessageKind::Error, format!("Unknown command: {cmd}. Type /help for available commands."))),
        }
    }

    pub(crate) fn execute_command_action(&mut self, action: Action) {
        match action {
            Action::ModelSwitch => {
                self.state.model_selector = Some(ModelSelectorState::default());
                self.state.active_modal = Some(ModalType::ModelSelector);
            }
            Action::NewSession => {
                let _ = self.state.session.create_session();
                self.state.messages.messages.clear();
                self.set_status("New session created", StatusLevel::Info);
            }
            Action::SessionList => {
                let _ = self.state.session.list_recent(50);
                self.state.session_list.open = true;
                self.state.active_modal = Some(ModalType::SessionList);
            }
            Action::YoloToggle => {
                self.state.permission.yolo_mode = !self.state.permission.yolo_mode;
                let msg = if self.state.permission.yolo_mode {
                    "YOLO mode enabled"
                } else {
                    "YOLO mode disabled"
                };
                self.set_status(msg, StatusLevel::Info);
            }
            Action::ToggleSidebar => {
                self.state.show_sidebar = !self.state.show_sidebar;
            }
            Action::ClearMessages => {
                self.state.messages.messages.clear();
                self.state.messages.reset_scroll();
                self.set_status("Chat cleared", StatusLevel::Info);
            }
            Action::ForceCompact => {
                self.set_status("Context compaction requested", StatusLevel::Info);
            }
            Action::ScrollUp => self.state.messages.scroll_up(10),
            Action::ScrollDown => self.state.messages.scroll_down(10),
            Action::ScrollTop => self.state.messages.scroll_to_top(),
            Action::ScrollBottom => self.state.messages.scroll_to_bottom(),
            Action::Cancel => {
                if self.state.agent.is_running {
                    self.state.agent.abort();
                }
            }
            Action::Quit => {
                self.should_quit = true;
            }
            _ => {}
        }
    }
}
