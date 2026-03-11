use super::*;
use chrono::Local;

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
                self.run_compact(arg)
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
            "/agents" => {
                self.open_agent_list();
                None
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
            "/theme" => {
                match arg {
                    Some(name) => {
                        let names = Theme::all_names();
                        if names.iter().any(|n| n == name) {
                            self.state.theme = Theme::from_name(name);
                            self.set_status(
                                format!("Theme: {name}"),
                                StatusLevel::Info,
                            );
                            Some((MessageKind::System, format!("Switched to theme: {name}")))
                        } else {
                            let available = names.join(", ");
                            Some((
                                MessageKind::Error,
                                format!("Unknown theme: {name}. Available: {available}"),
                            ))
                        }
                    }
                    None => {
                        // Open theme selector modal
                        self.open_theme_selector();
                        None
                    }
                }
            }
            "/commit" => {
                let status_output = std::process::Command::new("git")
                    .args(["status", "--short"])
                    .output();

                match status_output {
                    Ok(result) => {
                        let text = String::from_utf8_lossy(&result.stdout);
                        if text.trim().is_empty() {
                            Some((MessageKind::System, "Nothing to commit — working tree clean.".to_string()))
                        } else {
                            let line_count = text.lines().count();
                            let output = format!(
                                "Git status ({line_count} files):\n{text}\n\
                                 To commit, ask the agent: \"commit these changes with message: ...\"\n\
                                 Or use the bash tool: git add -A && git commit -m \"message\""
                            );
                            Some((MessageKind::System, output.trim_end().to_string()))
                        }
                    }
                    Err(err) => {
                        Some((MessageKind::Error, format!("Failed to run git status: {err}")))
                    }
                }
            }
            "/export" => {
                self.export_conversation(arg)
            }
            "/copy" => {
                let force_all = arg.map(|a| a.eq_ignore_ascii_case("all")).unwrap_or(false);
                self.copy_last_response_with_mode(force_all);
                None
            }
            "/help" => {
                let help = "\
Available commands:
  /model [provider/model]  — show or switch model
  /think [level]           — set thinking level (off/low/med/high/max)
  /theme [name]            — cycle or switch theme (default/dracula/nord)
  /permissions             — toggle permission level
  /connect [provider]      — add provider credentials
  /providers               — show provider status
  /disconnect <provider>   — remove provider credentials
  /tools                   — list all tools
  /tools reload            — reload tools from disk
  /tools init              — create tool templates
  /mcp [list]              — show MCP servers
  /mcp reload              — reload MCP config
  /agents                  — show sub-agent configuration
  /sessions                — session picker
  /status                  — show session info
  /diff                    — show git changes
  /commit                  — show git status for committing
  /export [filename]       — export conversation to file (.md or .json)
  /copy [all]              — copy last response (picks code block if multiple)
  /commands [list|reload|init] — manage custom slash commands
  /btw <question>          — ask a side question without interrupting the agent
  /clear                   — clear chat
  /compact [focus]          — compact conversation to save context window
  /help                    — show this help

Keyboard shortcuts:
  Tab / Shift+Tab          — cycle agent mode (Code/Plan)
  Ctrl+K / Ctrl+/          — command palette
  Ctrl+M                   — model selector
  Ctrl+T                   — cycle thinking level
  Ctrl+Y                   — copy last response to clipboard
  Ctrl+N                   — new session
  Ctrl+L                   — session picker
  Ctrl+S                   — toggle sidebar
  Ctrl+C                   — cancel / clear input / quit";
                Some((MessageKind::System, help.to_string()))
            }
            "/btw" => {
                if let Some(question) = arg {
                    self.handle_btw_query(question.to_string());
                    None
                } else {
                    Some((
                        MessageKind::Error,
                        "Usage: /btw <question> (e.g., /btw what does the retry logic do?)".to_string(),
                    ))
                }
            }
            "/commands" => {
                self.handle_commands_command(arg)
            }
            _ => {
                // Check custom commands before reporting unknown
                let cmd_name = cmd.trim_start_matches('/');
                if self.state.custom_commands.find(cmd_name).is_some() {
                    // Custom command found — signal redirect via None.
                    // The caller (submit_goal) will call try_resolve_custom_command().
                    None
                } else {
                    Some((MessageKind::Error, format!("Unknown command: {cmd}. Type /help for available commands.")))
                }
            }
        }
    }

    /// Try to resolve a custom command from user input.
    /// Returns `Some(Ok(prompt))` if a custom command was found and resolved,
    /// `Some(Err(msg))` if found but had a parameter error,
    /// `None` if no custom command matched.
    pub(crate) fn try_resolve_custom_command(&self, input: &str) -> Option<Result<String, String>> {
        use crate::state::custom_commands::CustomCommandRegistry;

        let trimmed = input.trim();
        if !trimmed.starts_with('/') {
            return None;
        }

        let parts: Vec<&str> = trimmed.splitn(2, ' ').collect();
        let cmd_name = parts[0].trim_start_matches('/');
        let args = parts.get(1).map(|s| s.trim()).unwrap_or("");

        let custom_cmd = self.state.custom_commands.find(cmd_name)?;
        Some(CustomCommandRegistry::resolve_prompt(custom_cmd, args))
    }

    /// Handle the `/commands` built-in command.
    fn handle_commands_command(&mut self, arg: Option<&str>) -> Option<(MessageKind, String)> {
        use crate::state::custom_commands::CustomCommandRegistry;

        match arg {
            Some("reload") => {
                self.state.custom_commands.reload();
                self.sync_custom_command_autocomplete();
                let count = self.state.custom_commands.commands.len();
                self.set_status(format!("Reloaded {count} custom commands"), StatusLevel::Info);
                Some((MessageKind::System, format!("Reloaded {count} custom commands")))
            }
            Some("init") => {
                match CustomCommandRegistry::create_templates() {
                    Ok(msg) => {
                        // Reload after creating templates
                        self.state.custom_commands.reload();
                        self.sync_custom_command_autocomplete();
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
                let commands = &self.state.custom_commands.commands;
                if commands.is_empty() {
                    Some((
                        MessageKind::System,
                        "No custom commands found.\n\
                         Add .toml files to .ava/commands/ or ~/.ava/commands/.\n\
                         Run /commands init to create an example."
                            .to_string(),
                    ))
                } else {
                    let mut lines = Vec::new();
                    lines.push(format!("Custom commands ({}):", commands.len()));
                    for cmd in commands {
                        let source = cmd.source.label();
                        let params = if cmd.params.is_empty() {
                            String::new()
                        } else {
                            let param_names: Vec<&str> =
                                cmd.params.iter().map(|p| p.name.as_str()).collect();
                            format!(" [{}]", param_names.join(", "))
                        };
                        lines.push(format!(
                            "  /{}{} — {} ({})",
                            cmd.name, params, cmd.description, source
                        ));
                    }
                    Some((MessageKind::System, lines.join("\n")))
                }
            }
            Some(sub) => Some((
                MessageKind::Error,
                format!("Unknown /commands subcommand: {sub}. Use: list, reload, init"),
            )),
        }
    }

    /// Sync custom commands into the input autocomplete list.
    pub(crate) fn sync_custom_command_autocomplete(&mut self) {
        use crate::widgets::autocomplete::AutocompleteItem;

        self.state.input.custom_slash_items = self
            .state
            .custom_commands
            .commands
            .iter()
            .map(|cmd| {
                let detail = if cmd.description.is_empty() {
                    format!("Custom command ({})", cmd.source.label())
                } else {
                    cmd.description.clone()
                };
                AutocompleteItem::new(&cmd.name, detail)
            })
            .collect();
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
                // Delegate to the /compact slash command for context usage display
                if let Some((kind, msg)) = self.handle_slash_command("/compact") {
                    self.state.messages.push(UiMessage::new(kind, msg));
                }
            }
            Action::ScrollUp => self.state.messages.scroll_up(10),
            Action::ScrollDown => self.state.messages.scroll_down(10),
            Action::ScrollTop => self.state.messages.scroll_to_top(),
            Action::ScrollBottom => self.state.messages.scroll_to_bottom(),
            Action::CopyLastResponse => {
                self.copy_last_response();
            }
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

    /// Export the current conversation to a file (markdown or JSON).
    fn export_conversation(&self, filename_arg: Option<&str>) -> Option<(MessageKind, String)> {
        let messages = &self.state.messages.messages;
        if messages.is_empty() {
            return Some((MessageKind::Error, "No messages to export.".to_string()));
        }

        let now = Local::now();
        let model = self.state.agent.current_model_display();
        let session_name = self
            .state
            .session
            .current_session
            .as_ref()
            .map(|s| format!("{}", s.id))
            .unwrap_or_else(|| now.format("%Y-%m-%d").to_string());

        // Determine filename
        let filename = match filename_arg {
            Some(name) => name.to_string(),
            None => now.format("ava-session-%Y-%m-%d-%H-%M.md").to_string(),
        };

        let is_json = filename.ends_with(".json");
        let msg_count = messages.len();

        let content = if is_json {
            self.export_as_json(messages, &session_name, &model, &now)
        } else {
            self.export_as_markdown(messages, &session_name, &model, &now)
        };

        // Write to current working directory
        let path = std::env::current_dir()
            .unwrap_or_else(|_| std::path::PathBuf::from("."))
            .join(&filename);

        match std::fs::write(&path, content) {
            Ok(()) => {
                let display_path = path.display();
                Some((
                    MessageKind::System,
                    format!("Exported conversation to {display_path} ({msg_count} messages)"),
                ))
            }
            Err(err) => Some((
                MessageKind::Error,
                format!("Failed to export: {err}"),
            )),
        }
    }

    fn export_as_markdown(
        &self,
        messages: &[UiMessage],
        session_name: &str,
        model: &str,
        now: &chrono::DateTime<Local>,
    ) -> String {
        let mut out = String::new();
        let date_str = now.format("%Y-%m-%d %H:%M:%S").to_string();
        let msg_count = messages.len();

        out.push_str(&format!("# AVA Session — {session_name}\n"));
        out.push_str(&format!("Model: {model}\n"));
        out.push_str(&format!("Date: {date_str}\n"));
        out.push_str(&format!("Messages: {msg_count}\n"));
        out.push_str("\n---\n\n");

        for msg in messages {
            match msg.kind {
                MessageKind::User => {
                    out.push_str("## User\n");
                    out.push_str(&msg.content);
                    out.push_str("\n\n---\n\n");
                }
                MessageKind::Assistant => {
                    out.push_str("## Assistant\n");
                    out.push_str(&msg.content);
                    out.push_str("\n\n---\n\n");
                }
                MessageKind::ToolCall => {
                    let tool_name = msg.content.split_whitespace().next().unwrap_or("unknown");
                    let rest = msg.content[tool_name.len()..].trim_start();
                    out.push_str(&format!("## Tool Call: {tool_name}\n"));
                    if !rest.is_empty() {
                        out.push_str("```yaml\n");
                        out.push_str(rest);
                        out.push_str("\n```\n");
                    }
                    out.push('\n');
                }
                MessageKind::ToolResult => {
                    out.push_str("## Tool Result\n");
                    // Truncate tool results to 500 chars
                    if msg.content.len() > 500 {
                        out.push_str(&msg.content[..500]);
                        out.push_str("\n... (truncated)\n");
                    } else {
                        out.push_str(&msg.content);
                        out.push('\n');
                    }
                    out.push_str("\n---\n\n");
                }
                MessageKind::Thinking => {
                    out.push_str("## Thinking\n");
                    out.push_str(&msg.content);
                    out.push_str("\n\n");
                }
                MessageKind::Error => {
                    out.push_str(&format!("**Error:** {}\n\n", msg.content));
                }
                MessageKind::System => {
                    out.push_str(&format!("*{system}*\n\n", system = msg.content));
                }
                MessageKind::SubAgent => {
                    out.push_str("## Sub-Agent\n");
                    out.push_str(&msg.content);
                    out.push_str("\n\n---\n\n");
                }
            }
        }

        out
    }

    fn export_as_json(
        &self,
        messages: &[UiMessage],
        session_name: &str,
        model: &str,
        now: &chrono::DateTime<Local>,
    ) -> String {
        let date_str = now.to_rfc3339();

        let json_messages: Vec<serde_json::Value> = messages
            .iter()
            .map(|msg| {
                let role = match msg.kind {
                    MessageKind::User => "user",
                    MessageKind::Assistant => "assistant",
                    MessageKind::ToolCall => "tool_call",
                    MessageKind::ToolResult => "tool_result",
                    MessageKind::Thinking => "thinking",
                    MessageKind::Error => "error",
                    MessageKind::System => "system",
                    MessageKind::SubAgent => "sub_agent",
                };

                let mut obj = serde_json::json!({
                    "role": role,
                    "content": msg.content,
                });

                // Add tool name for tool calls
                if msg.kind == MessageKind::ToolCall {
                    let tool_name = msg.content.split_whitespace().next().unwrap_or("unknown");
                    let rest = msg.content[tool_name.len()..].trim_start();
                    obj["name"] = serde_json::json!(tool_name);
                    obj["input"] = serde_json::json!(rest);
                }

                // Add model name for assistant messages
                if let Some(ref model_name) = msg.model_name {
                    obj["model"] = serde_json::json!(model_name);
                }

                obj
            })
            .collect();

        let export = serde_json::json!({
            "session": session_name,
            "model": model,
            "date": date_str,
            "messages": json_messages,
        });

        serde_json::to_string_pretty(&export).unwrap_or_else(|e| format!("{{\"error\": \"{e}\"}}"))
    }

    /// Run the `/compact` command: condense conversation messages to save context window.
    ///
    /// Converts UI messages to ava_types::Message, estimates token counts,
    /// applies the sliding window condensation strategy, then replaces the UI
    /// message list with a summary message followed by the surviving messages.
    ///
    /// If `focus` is provided, messages containing the focus keywords are
    /// prioritized (kept even when older messages are dropped).
    fn run_compact(&mut self, focus: Option<&str>) -> Option<(MessageKind, String)> {
        use ava_context::strategies::CondensationStrategy;
        use ava_context::{SlidingWindowStrategy, ToolTruncationStrategy};

        let ui_messages = &self.state.messages.messages;
        if ui_messages.is_empty() {
            return Some((
                MessageKind::System,
                "Nothing to compact -- conversation is empty.".to_string(),
            ));
        }

        // Convert UI messages to ava_types::Message for token estimation
        let typed_messages: Vec<ava_types::Message> = ui_messages
            .iter()
            .map(|ui| {
                let role = match ui.kind {
                    MessageKind::User => ava_types::Role::User,
                    MessageKind::Assistant => ava_types::Role::Assistant,
                    MessageKind::ToolCall => ava_types::Role::Assistant,
                    MessageKind::ToolResult => ava_types::Role::Tool,
                    MessageKind::Thinking => ava_types::Role::Assistant,
                    MessageKind::Error => ava_types::Role::System,
                    MessageKind::System => ava_types::Role::System,
                    MessageKind::SubAgent => ava_types::Role::Assistant,
                };
                ava_types::Message::new(role, &ui.content)
            })
            .collect();

        // Estimate tokens before compaction
        let before_tokens: usize = typed_messages
            .iter()
            .map(ava_context::estimate_tokens_for_message)
            .sum();
        let before_count = ui_messages.len();

        // Determine context window and check if compaction is needed
        let context_window = self.state.agent.context_window.unwrap_or(128_000);
        let usage_pct = before_tokens as f64 / context_window as f64 * 100.0;

        // If usage is below 50%, compaction is not needed (unless forced with focus)
        if usage_pct < 50.0 && focus.is_none() {
            self.set_status(
                format!("Context: {}% -- no compaction needed", usage_pct as u64),
                StatusLevel::Info,
            );
            return Some((
                MessageKind::System,
                format!(
                    "Context usage is low ({:.0}%), no compaction needed.\n\
                     {before_tokens} tokens across {before_count} messages.",
                    usage_pct,
                ),
            ));
        }

        // Target: keep ~50% of context window (or 75% of current tokens if smaller)
        let target_tokens = (context_window / 2).min(before_tokens * 3 / 4);

        // Stage 1: Truncate large tool results
        let truncated = ToolTruncationStrategy::default()
            .condense(&typed_messages, target_tokens)
            .unwrap_or_else(|_| typed_messages.clone());

        // Stage 2: Sliding window -- drop oldest messages that don't fit
        let condensed = SlidingWindowStrategy
            .condense(&truncated, target_tokens)
            .unwrap_or_else(|_| truncated);

        // If focus keywords are provided, check which messages to preserve.
        // We keep any message whose content contains a focus keyword, even if
        // sliding window would have dropped it.
        let final_messages = if let Some(focus_text) = focus {
            let keywords: Vec<&str> = focus_text.split_whitespace().collect();
            let mut kept_indices: Vec<bool> = vec![false; typed_messages.len()];

            // Mark messages that survived sliding window
            let condensed_set: std::collections::HashSet<String> =
                condensed.iter().map(|m| m.content.clone()).collect();
            for (i, msg) in typed_messages.iter().enumerate() {
                if condensed_set.contains(&msg.content) {
                    kept_indices[i] = true;
                }
            }

            // Also keep messages matching focus keywords
            for (i, msg) in typed_messages.iter().enumerate() {
                if !kept_indices[i] {
                    let content_lower = msg.content.to_lowercase();
                    if keywords
                        .iter()
                        .any(|kw| content_lower.contains(&kw.to_lowercase()))
                    {
                        kept_indices[i] = true;
                    }
                }
            }

            // Rebuild: collect kept messages, re-estimate tokens, drop oldest if still over budget
            let mut focused: Vec<ava_types::Message> = typed_messages
                .iter()
                .zip(kept_indices.iter())
                .filter(|(_, &kept)| kept)
                .map(|(m, _)| m.clone())
                .collect();

            // If we're still over budget, trim from the front (oldest)
            let mut total: usize = focused
                .iter()
                .map(ava_context::estimate_tokens_for_message)
                .sum();
            while total > target_tokens && focused.len() > 1 {
                let removed = focused.remove(0);
                total -= ava_context::estimate_tokens_for_message(&removed);
            }
            focused
        } else {
            condensed
        };

        // Estimate tokens after compaction
        let after_tokens: usize = final_messages
            .iter()
            .map(ava_context::estimate_tokens_for_message)
            .sum();
        let after_count = final_messages.len();
        let saved_tokens = before_tokens.saturating_sub(after_tokens);
        let dropped_count = before_count.saturating_sub(after_count);

        if dropped_count == 0 {
            self.set_status("Already compact".to_string(), StatusLevel::Info);
            return Some((
                MessageKind::System,
                format!(
                    "Conversation is already compact.\n\
                     {before_tokens} tokens across {before_count} messages.",
                ),
            ));
        }

        // Build a summary of what was removed
        let summary = if let Some(focus_text) = focus {
            format!(
                "Compacted conversation (focus: \"{focus_text}\"). \
                 Saved ~{saved_tokens} tokens (was {before_tokens}, now {after_tokens}). \
                 Dropped {dropped_count} messages, kept {after_count}."
            )
        } else {
            format!(
                "Compacted conversation. \
                 Saved ~{saved_tokens} tokens (was {before_tokens}, now {after_tokens}). \
                 Dropped {dropped_count} messages, kept {after_count}."
            )
        };

        // Map condensed ava_types::Message back to UI messages.
        // We match by content to find the original UI message (preserving kind, model_name, etc.)
        // For messages that were truncated (tool results), we use the truncated content.
        let mut new_ui_messages: Vec<UiMessage> = Vec::with_capacity(after_count + 1);

        // Add compaction summary as first message
        new_ui_messages.push(UiMessage::new(MessageKind::System, &summary));

        // Match each surviving message back to the original UI message
        for condensed_msg in &final_messages {
            // Find the best matching original UI message by content
            let matching_ui = ui_messages
                .iter()
                .find(|ui| {
                    // Exact match
                    ui.content == condensed_msg.content
                })
                .or_else(|| {
                    // Partial match (for truncated tool results)
                    ui_messages.iter().find(|ui| {
                        condensed_msg.content.len() > 10
                            && ui.content.starts_with(
                                &condensed_msg.content
                                    [..condensed_msg.content.len().min(50)],
                            )
                    })
                });

            if let Some(original) = matching_ui {
                let mut rebuilt = original.clone();
                // If the content was truncated by the strategy, use the truncated version
                if rebuilt.content != condensed_msg.content {
                    rebuilt.content = condensed_msg.content.clone();
                }
                rebuilt.is_streaming = false;
                new_ui_messages.push(rebuilt);
            } else {
                // Fallback: create a new UI message with the condensed content
                let kind = match condensed_msg.role {
                    ava_types::Role::User => MessageKind::User,
                    ava_types::Role::Assistant => MessageKind::Assistant,
                    ava_types::Role::Tool => MessageKind::ToolResult,
                    ava_types::Role::System => MessageKind::System,
                };
                new_ui_messages.push(UiMessage::new(kind, &condensed_msg.content));
            }
        }

        // Replace the UI message list
        self.state.messages.messages = new_ui_messages;
        self.state.messages.reset_scroll();

        self.set_status(
            format!("Compacted: saved ~{saved_tokens} tokens"),
            StatusLevel::Info,
        );
        // Return None because we already inserted the summary message directly
        None
    }
}
