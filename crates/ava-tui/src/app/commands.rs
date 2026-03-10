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
                    // Open model selector modal
                    self.execute_command_action(Action::ModelSwitch);
                    None
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
                        })
                        .unwrap_or_default();
                        let tool_items: Vec<crate::widgets::tool_list::ToolListItem> = tools
                            .into_iter()
                            .map(|(def, src)| {
                                crate::widgets::tool_list::ToolListItem {
                                    name: def.name,
                                    description: def.description,
                                    source: src,
                                }
                            })
                            .collect();
                        self.state.tool_list = ToolListState::from_items(tool_items);
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
                        })
                        .unwrap_or_default();
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
            "/connect" | "/providers" => {
                let credentials = tokio::task::block_in_place(|| {
                    tokio::runtime::Handle::current()
                        .block_on(ava_config::CredentialStore::load_default())
                })
                .unwrap_or_default();

                if let Some(provider) = arg {
                    // Direct configure for specific provider
                    let provider = provider.to_lowercase();
                    self.state.provider_connect =
                        Some(ProviderConnectState::for_provider(&credentials, &provider));
                } else {
                    // Show provider list
                    self.state.provider_connect =
                        Some(ProviderConnectState::from_credentials(&credentials));
                }
                self.state.active_modal = Some(ModalType::ProviderConnect);
                None
            }
            "/disconnect" => {
                if let Some(provider) = arg {
                    let provider = provider.to_lowercase();
                    let result = tokio::task::block_in_place(|| {
                        tokio::runtime::Handle::current().block_on(async {
                            let mut store = ava_config::CredentialStore::load_default()
                                .await
                                .unwrap_or_default();
                            ava_config::execute_credential_command(
                                ava_config::CredentialCommand::Remove { provider },
                                &mut store,
                            )
                            .await
                        })
                    });
                    match result {
                        Ok(msg) => {
                            self.set_status(&msg, StatusLevel::Info);
                            Some((MessageKind::System, msg))
                        }
                        Err(err) => {
                            self.set_status(format!("Failed: {err}"), StatusLevel::Error);
                            Some((MessageKind::Error, format!("Failed: {err}")))
                        }
                    }
                } else {
                    Some((
                        MessageKind::Error,
                        "Usage: /disconnect <provider> (e.g., /disconnect openrouter)".to_string(),
                    ))
                }
            }
            "/status" => {
                let model = self.state.agent.current_model_display();
                let tokens_in = self.state.agent.tokens_used.input;
                let tokens_out = self.state.agent.tokens_used.output;
                let cost = self.state.agent.cost;
                let turn = self.state.agent.current_turn;

                let session_id = self
                    .state
                    .session
                    .current_session
                    .as_ref()
                    .map(|s| format!("{}", s.id))
                    .unwrap_or_else(|| "none".to_string());

                let tool_count = tokio::task::block_in_place(|| {
                    tokio::runtime::Handle::current()
                        .block_on(self.state.agent.list_tools_with_source())
                })
                .unwrap_or_default()
                .len();

                let mcp_count = self.state.agent.mcp_tool_count;

                let cwd = std::env::current_dir()
                    .map(|p| p.display().to_string())
                    .unwrap_or_else(|_| "unknown".to_string());

                let status = format!(
                    "Model: {model}\n\
                     Tokens: {tokens_in} in / {tokens_out} out (${cost:.2})\n\
                     Session: {session_id} ({turn} turns)\n\
                     Tools: {tool_count} total ({mcp_count} MCP)\n\
                     Working directory: {cwd}"
                );
                Some((MessageKind::System, status))
            }
            "/diff" => {
                let diff_stat = std::process::Command::new("git")
                    .args(["diff", "--stat"])
                    .output();

                let status_short = std::process::Command::new("git")
                    .args(["status", "--short"])
                    .output();

                let mut output = String::new();

                match diff_stat {
                    Ok(ref result) if !result.stdout.is_empty() => {
                        output.push_str(&String::from_utf8_lossy(&result.stdout));
                    }
                    Ok(_) => {
                        output.push_str("No staged/unstaged changes.\n");
                    }
                    Err(err) => {
                        return Some((
                            MessageKind::Error,
                            format!("Failed to run git diff: {err}"),
                        ));
                    }
                }

                if let Ok(ref result) = status_short {
                    let status_text = String::from_utf8_lossy(&result.stdout);
                    let untracked_count = status_text
                        .lines()
                        .filter(|l| l.starts_with("??"))
                        .count();
                    if untracked_count > 0 {
                        output.push_str(&format!("{untracked_count} untracked files\n"));
                    }
                }

                Some((MessageKind::System, output.trim_end().to_string()))
            }
            "/clear" => {
                self.state.messages.messages.clear();
                self.state.messages.reset_scroll();
                self.set_status("Chat cleared", StatusLevel::Info);
                None
            }
            "/compact" => {
                self.set_status("Context compaction requested", StatusLevel::Info);
                Some((MessageKind::System, "Context compaction requested".to_string()))
            }
            "/think" => {
                match arg {
                    Some(level_str) => {
                        if let Some(level) = ava_types::ThinkingLevel::from_str_loose(level_str) {
                            self.state.agent.set_thinking_level(level);
                            let label = level.label();
                            Some((MessageKind::System, format!("Thinking level set to {label}")))
                        } else {
                            Some((MessageKind::Error,
                                "Invalid level. Use: /think off|low|med|high|max".to_string()
                            ))
                        }
                    }
                    None => {
                        let label = self.state.agent.cycle_thinking();
                        Some((MessageKind::System, format!("Thinking level: {label}")))
                    }
                }
            }
            "/sessions" => {
                self.execute_command_action(Action::SessionList);
                None
            }
            "/permissions" => {
                self.state.permission.permission_level = self.state.permission.permission_level.toggle();
                let label = self.state.permission.permission_level.label();
                self.set_status(format!("Permissions: {label}"), StatusLevel::Info);
                None
            }
            "/help" => {
                let help = "\
Available commands:
  /model [provider/model]  — show or switch model
  /think [level]           — set thinking level (off/low/med/high/max)
  /permissions             — toggle permission level
  /connect [provider]      — add provider credentials
  /providers               — show provider status
  /disconnect <provider>   — remove provider credentials
  /tools                   — list all tools
  /tools reload            — reload tools from disk
  /tools init              — create tool templates
  /mcp [list]              — show MCP servers
  /mcp reload              — reload MCP config
  /sessions                — session picker
  /status                  — show session info
  /diff                    — show git changes
  /clear                   — clear chat
  /compact                 — compact context
  /help                    — show this help

Keyboard shortcuts:
  Tab / Shift+Tab          — cycle agent mode (Code/Plan/Architect)
  Ctrl+K / Ctrl+/          — command palette
  Ctrl+M                   — model selector
  Ctrl+T                   — cycle thinking level
  Ctrl+N                   — new session
  Ctrl+L                   — session picker
  Ctrl+S                   — toggle sidebar
  Ctrl+C                   — cancel / clear input / quit";
                Some((MessageKind::System, help.to_string()))
            }
            _ => Some((MessageKind::Error, format!("Unknown command: {cmd}. Type /help for available commands."))),
        }
    }

    pub(crate) fn execute_command_action(&mut self, action: Action) {
        match action {
            Action::ModelSwitch => {
                // Load credentials + catalog for dynamic model list
                let (credentials, catalog) = tokio::task::block_in_place(|| {
                    tokio::runtime::Handle::current().block_on(async {
                        let creds = ava_config::CredentialStore::load_default()
                            .await
                            .unwrap_or_default();
                        let cat = self.state.model_catalog.get().await;
                        (creds, cat)
                    })
                });
                // Use fallback catalog if dynamic fetch hasn't completed yet,
                // then merge fallback entries for providers not in models.dev
                // (copilot, coding plan providers, etc.)
                let mut effective_catalog = if catalog.is_empty() {
                    ava_config::fallback_catalog()
                } else {
                    catalog
                };
                effective_catalog.merge_fallback();
                self.state.model_selector = Some(ModelSelectorState::from_catalog(
                    &effective_catalog,
                    &credentials,
                    &self.state.agent.recent_models,
                    &self.state.agent.model_name,
                    &self.state.agent.provider_name,
                ));
                self.state.active_modal = Some(ModalType::ModelSelector);
            }
            Action::NewSession => {
                let _ = self.state.session.create_session();
                self.state.messages.messages.clear();
                self.set_status("New session created", StatusLevel::Info);
            }
            Action::SessionList => {
                if let Ok(sessions) = self.state.session.list_recent(50) {
                    self.state.session_list.update_sessions(&sessions);
                }
                self.state.session_list.open = true;
                self.state.active_modal = Some(ModalType::SessionList);
            }
            Action::PermissionToggle => {
                self.state.permission.permission_level = self.state.permission.permission_level.toggle();
                self.set_status(
                    format!("Permissions: {}", self.state.permission.permission_level.label()),
                    StatusLevel::Info,
                );
            }
            Action::ModeNext => {
                self.state.agent_mode = self.state.agent_mode.cycle_next();
                self.state.agent.set_mode(self.state.agent_mode);
                self.set_status(format!("Mode: {}", self.state.agent_mode.label()), StatusLevel::Info);
            }
            Action::ModePrev => {
                self.state.agent_mode = self.state.agent_mode.cycle_prev();
                self.state.agent.set_mode(self.state.agent_mode);
                self.set_status(format!("Mode: {}", self.state.agent_mode.label()), StatusLevel::Info);
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
