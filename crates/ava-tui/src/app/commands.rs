use super::*;

impl App {
    /// Handle slash commands. Returns Some((kind, message)) if handled, None if not a slash command.
    pub(crate) fn handle_slash_command(
        &mut self,
        input: &str,
        app_tx: Option<mpsc::UnboundedSender<AppEvent>>,
    ) -> Option<(MessageKind, String)> {
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
                        if let Some(tx) = app_tx.clone() {
                            let display = format!("{provider}/{model}");
                            self.set_status(format!("Switching to {display}..."), StatusLevel::Info);
                            self.spawn_model_switch(
                                provider,
                                model,
                                display,
                                crate::event::ModelSwitchContext::SlashCommand,
                                tx,
                            );
                            None
                        } else {
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
                        }
                    } else {
                        Some((MessageKind::Error,
                            "Invalid format. Use: /model provider/model (e.g., /model openrouter/anthropic/claude-sonnet-4)".to_string()
                        ))
                    }
                } else {
                    // Open model selector modal
                    self.execute_command_action(Action::ModelSwitch, app_tx.clone());
                    None
                }
            }
            "/tools" => {
                match arg {
                    Some("reload") => {
                        if let Some(tx) = app_tx.clone() {
                            self.set_status("Reloading tools...", StatusLevel::Info);
                            self.spawn_tools_reload(tx);
                            None
                        } else {
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
                        self.state.active_modal = Some(ModalType::ToolList);
                        if let Some(tx) = app_tx.clone() {
                            self.state.tool_list = ToolListState::default();
                            self.set_status("Loading tools...", StatusLevel::Info);
                            self.spawn_tool_list_load(tx);
                        } else {
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
                        }
                        None
                    }
                }
            }
            "/mcp" => {
                match arg {
                    Some("reload") => {
                        if let Some(tx) = app_tx.clone() {
                            self.set_status("Reloading MCP...", StatusLevel::Info);
                            self.spawn_mcp_reload(tx);
                            None
                        } else {
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
                    }
                    Some("list") | None => {
                        if let Some(tx) = app_tx.clone() {
                            self.set_status("Loading MCP servers...", StatusLevel::Info);
                            self.spawn_mcp_server_list(tx);
                            None
                        } else {
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
                    }
                    Some(sub) => Some((
                        MessageKind::Error,
                        format!("Unknown /mcp subcommand: {sub}. Use: list, reload"),
                    )),
                }
            }
            "/connect" | "/providers" => {
                if let Some(tx) = app_tx.clone() {
                    self.state.provider_connect = Some(ProviderConnectState::from_credentials(
                        &ava_config::CredentialStore::default(),
                    ));
                    if let Some(provider) = arg {
                        let provider = provider.to_lowercase();
                        self.spawn_provider_connect_load(Some(provider), tx);
                    } else {
                        self.spawn_provider_connect_load(None, tx);
                    }
                } else {
                    let credentials = tokio::task::block_in_place(|| {
                        tokio::runtime::Handle::current()
                            .block_on(ava_config::CredentialStore::load_default())
                    })
                    .unwrap_or_default();
                    self.state.provider_connect = if let Some(provider) = arg {
                        Some(ProviderConnectState::for_provider(
                            &credentials,
                            &provider.to_lowercase(),
                        ))
                    } else {
                        Some(ProviderConnectState::from_credentials(&credentials))
                    };
                }
                self.state.active_modal = Some(ModalType::ProviderConnect);
                None
            }
            "/disconnect" => {
                if let Some(provider) = arg {
                    let provider = provider.to_lowercase();
                    if let Some(tx) = app_tx.clone() {
                        self.set_status(format!("Disconnecting {provider}..."), StatusLevel::Info);
                        self.spawn_credential_command(
                            ava_config::CredentialCommand::Remove { provider },
                            Self::format_standard_credential_result,
                            tx,
                        );
                        None
                    } else {
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
                    }
                } else {
                    Some((
                        MessageKind::Error,
                        "Usage: /disconnect <provider> (e.g., /disconnect openrouter)".to_string(),
                    ))
                }
            }
            "/credentials" => {
                match arg {
                    Some("list") | None => {
                        if let Some(tx) = app_tx.clone() {
                            self.set_status("Loading credentials...", StatusLevel::Info);
                            self.spawn_credential_command(
                                ava_config::CredentialCommand::List,
                                Self::format_credentials_list_result,
                                tx,
                            );
                            None
                        } else {
                            let result = tokio::task::block_in_place(|| {
                                tokio::runtime::Handle::current().block_on(async {
                                    let store = ava_config::CredentialStore::load_default()
                                        .await
                                        .unwrap_or_default();
                                    ava_config::execute_credential_command(
                                        ava_config::CredentialCommand::List,
                                        &mut store.clone(),
                                    )
                                    .await
                                })
                            });
                            match result {
                                Ok(msg) => Some((MessageKind::System, format!("Credentials:\n{msg}"))),
                                Err(err) => Some((MessageKind::Error, format!("Failed: {err}"))),
                            }
                        }
                    }
                    Some(sub) if sub.starts_with("add ") || sub.starts_with("set ") => {
                        let rest = sub.split_once(' ').map(|x| x.1).unwrap_or("");
                        let parts: Vec<&str> = rest.splitn(2, ' ').collect();
                        if parts.len() == 2 && !parts[0].is_empty() && !parts[1].is_empty() {
                            let provider = parts[0].to_lowercase();
                            let api_key = parts[1].to_string();
                            if let Some(tx) = app_tx.clone() {
                                self.set_status(format!("Saving credentials for {provider}..."), StatusLevel::Info);
                                self.spawn_credential_command(
                                    ava_config::CredentialCommand::Set {
                                        provider,
                                        api_key,
                                        base_url: None,
                                    },
                                    Self::format_standard_credential_result,
                                    tx,
                                );
                                None
                            } else {
                                let result = tokio::task::block_in_place(|| {
                                    tokio::runtime::Handle::current().block_on(async {
                                        let mut store = ava_config::CredentialStore::load_default()
                                            .await
                                            .unwrap_or_default();
                                        ava_config::execute_credential_command(
                                            ava_config::CredentialCommand::Set {
                                                provider,
                                                api_key,
                                                base_url: None,
                                            },
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
                            }
                        } else {
                            Some((
                                MessageKind::Error,
                                "Usage: /credentials add <provider> <api_key>".to_string(),
                            ))
                        }
                    }
                    Some(sub) if sub.starts_with("remove ") || sub.starts_with("rm ") => {
                        let provider = sub.split_once(' ').map(|x| x.1).unwrap_or("").trim().to_lowercase();
                        if provider.is_empty() {
                            Some((
                                MessageKind::Error,
                                "Usage: /credentials remove <provider>".to_string(),
                            ))
                        } else if let Some(tx) = app_tx.clone() {
                            self.set_status(format!("Removing credentials for {provider}..."), StatusLevel::Info);
                            self.spawn_credential_command(
                                ava_config::CredentialCommand::Remove { provider },
                                Self::format_standard_credential_result,
                                tx,
                            );
                            None
                        } else {
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
                        }
                    }
                    Some(sub) => Some((
                        MessageKind::Error,
                        format!("Unknown /credentials subcommand: {sub}. Use: list, add <provider> <key>, remove <provider>"),
                    )),
                }
            }
            "/status" => {
                if let Some(app_tx) = app_tx {
                    self.spawn_status_message(app_tx);
                    return None;
                }

                let tool_count = tokio::task::block_in_place(|| {
                    tokio::runtime::Handle::current()
                        .block_on(self.state.agent.list_tools_with_source())
                })
                .unwrap_or_default()
                .len();
                let status = self.build_status_summary(tool_count).render();
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
                self.execute_command_action(Action::SessionList, app_tx.clone());
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
                if let Some(app_tx) = app_tx {
                    self.spawn_commit_prep(app_tx);
                    return None;
                }

                match tokio::runtime::Handle::try_current() {
                    Ok(handle) => {
                        let result = tokio::task::block_in_place(|| {
                            handle.block_on(async {
                                tokio::task::spawn_blocking(super::git_commit::handle_commit_command)
                                    .await
                            })
                        });

                        match result {
                            Ok(output) => Some(output),
                            Err(err) => Some((
                                MessageKind::Error,
                                format!("Failed to inspect commit readiness: {err}"),
                            )),
                        }
                    }
                    Err(_) => Some(super::git_commit::handle_commit_command()),
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
            "/image" => {
                if let Some(path_str) = arg {
                    let path = std::path::Path::new(path_str);
                    match ava_types::ImageContent::from_file(path) {
                        Ok(img) => {
                            self.pending_images.push(img);
                            let count = self.pending_images.len();
                            self.set_status(
                                format!("Image attached ({count} pending). Type your prompt and press Enter."),
                                StatusLevel::Info,
                            );
                            Some((MessageKind::System, format!(
                                "Attached image: {} ({}). {count} image(s) pending — send a message to include them.",
                                path.display(), path.extension().and_then(|e| e.to_str()).unwrap_or("unknown")
                            )))
                        }
                        Err(e) => {
                            Some((MessageKind::Error, e))
                        }
                    }
                } else {
                    Some((MessageKind::Error,
                        "Usage: /image <path>\nSupported formats: png, jpg, jpeg, gif, webp\nExample: /image screenshot.png".to_string()
                    ))
                }
            }
            "/plan" => {
                if matches!(self.state.agent_mode, AgentMode::Plan) {
                    Some((MessageKind::System, "Already in Plan mode.".to_string()))
                } else {
                    self.state.agent_mode = AgentMode::Plan;
                    self.state.agent.set_mode(self.state.agent_mode);
                    self.set_status("Mode: Plan", StatusLevel::Info);
                    Some((MessageKind::System, "Switched to Plan mode \u{2014} will write plans to .ava/plans/".to_string()))
                }
            }
            "/code" => {
                if matches!(self.state.agent_mode, AgentMode::Code) {
                    Some((MessageKind::System, "Already in Code mode.".to_string()))
                } else {
                    self.state.agent_mode = AgentMode::Code;
                    self.state.agent.set_mode(self.state.agent_mode);
                    self.set_status("Mode: Code", StatusLevel::Info);
                    Some((MessageKind::System, "Switched to Code mode \u{2014} plan files at .ava/plans/".to_string()))
                }
            }
            "/plans" => {
                let plans_dir = std::env::current_dir()
                    .unwrap_or_default()
                    .join(".ava")
                    .join("plans");
                if !plans_dir.exists() {
                    Some((MessageKind::System, "No plans found. Switch to Plan mode and ask the agent to create a plan.".to_string()))
                } else {
                    match std::fs::read_dir(&plans_dir) {
                        Ok(entries) => {
                            let mut files: Vec<String> = entries
                                .filter_map(|e| e.ok())
                                .filter(|e| {
                                    e.path().extension().and_then(|ext| ext.to_str()) == Some("md")
                                })
                                .map(|e| {
                                    let name = e.file_name().to_string_lossy().to_string();
                                    let size = e.metadata().map(|m| m.len()).unwrap_or(0);
                                    format!("  {name} ({size} bytes)")
                                })
                                .collect();
                            files.sort();
                            if files.is_empty() {
                                Some((MessageKind::System, "No plan files in .ava/plans/. Switch to Plan mode to create one.".to_string()))
                            } else {
                                Some((MessageKind::System, format!(
                                    "Plans ({} files):\n{}",
                                    files.len(),
                                    files.join("\n")
                                )))
                            }
                        }
                        Err(err) => {
                            Some((MessageKind::Error, format!("Failed to read .ava/plans/: {err}")))
                        }
                    }
                }
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
  /credentials [list|add|remove] — manage provider API keys (redacted)
  /tools                   — list all tools
  /tools reload            — reload tools from disk
  /tools init              — create tool templates
  /mcp [list]              — show MCP servers
  /mcp reload              — reload MCP config
  /agents                  — show sub-agent configuration
  /sessions                — session picker
  /status                  — show session info
  /diff                    — show git changes
  /commit                  — inspect commit readiness and suggest a message
  /export [filename]       — export conversation to file (.md or .json)
  /copy [all]              — copy last response (picks code block if multiple)
  /image <path>            — attach image to next message (png/jpg/gif/webp)
  /plan                    — switch to Plan mode
  /code                    — switch to Code mode
  /plans                   — list plan files in .ava/plans/
  /commands [list|reload|init] — manage custom slash commands
  /hooks [list|reload|init|dry-run <event>] — manage lifecycle hooks
  /btw <question>          — ask a side question without interrupting the agent
  /bg [--branch] <goal>    — launch a goal as a background agent
  /praxis <goal>           — launch a Praxis multi-agent task
  /tasks                   — show background task list
  /clear                   — clear chat
  /compact [focus]          — compact conversation to save context window
  /help                    — show this help

Keyboard shortcuts:
  Tab / Shift+Tab          — cycle agent mode (Code/Plan)
  Ctrl+K / Ctrl+/          — command palette
  Ctrl+M                   — model selector
  Ctrl+B                   — move running agent to background
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
            "/hooks" => {
                self.handle_hooks_command(arg)
            }
            "/bg" => {
                if let Some(goal) = arg {
                    let trimmed = goal.trim_start();
                    let (isolated_branch, goal_text) = if let Some(rest) = trimmed.strip_prefix("--branch") {
                        (true, rest.trim_start())
                    } else {
                        (false, trimmed)
                    };
                    if goal_text.is_empty() {
                        Some((
                            MessageKind::Error,
                            "Usage: /bg [--branch] <goal> (e.g., /bg --branch refactor auth module)".to_string(),
                        ))
                    } else {
                        // Store the goal and return None — submit_goal will check pending_bg_goal
                        self.pending_bg_goal = Some(super::PendingBackgroundGoal {
                            goal: goal_text.to_string(),
                            isolated_branch,
                        });
                        None
                    }
                } else {
                    Some((
                        MessageKind::Error,
                        "Usage: /bg [--branch] <goal> (e.g., /bg --branch refactor auth module)"
                            .to_string(),
                    ))
                }
            }
            "/praxis" => {
                if let Some(goal) = arg {
                    let trimmed = goal.trim();
                    if trimmed.is_empty() {
                        Some((
                            MessageKind::Error,
                            "Usage: /praxis <goal> (e.g., /praxis parallelize this refactor)"
                                .to_string(),
                        ))
                    } else {
                        self.pending_praxis_goal = Some(super::PendingPraxisGoal {
                            goal: trimmed.to_string(),
                        });
                        None
                    }
                } else {
                    Some((
                        MessageKind::Error,
                        "Usage: /praxis <goal> (e.g., /praxis parallelize this refactor)"
                            .to_string(),
                    ))
                }
            }
            "/tasks" => {
                self.state.active_modal = Some(super::ModalType::TaskList);
                None
            }
            "/later" => {
                if let Some(text) = arg {
                    if text.is_empty() {
                        Some((MessageKind::Error, "Usage: /later <message> — queue a post-complete message".to_string()))
                    } else {
                        // Parse optional group: /later 2 message
                        let (group, message) = if let Some(rest) = text.strip_prefix(|c: char| c.is_ascii_digit()) {
                            let num_str = format!("{}{}", &text[..1], rest.chars().take_while(|c| c.is_ascii_digit()).collect::<String>());
                            let after = text[num_str.len()..].trim();
                            if after.is_empty() {
                                (1, text.to_string()) // Just a number, treat as message
                            } else {
                                (num_str.parse().unwrap_or(1), after.to_string())
                            }
                        } else {
                            (1, text.to_string())
                        };
                        self.send_queued_message(message, ava_types::MessageTier::PostComplete { group });
                        None // send_queued_message already displays the message
                    }
                } else {
                    Some((MessageKind::Error, "Usage: /later <message> — queue a post-complete message".to_string()))
                }
            }
            "/queue" => {
                if self.state.input.queue_display.is_empty() {
                    Some((MessageKind::System, "No messages queued.".to_string()))
                } else {
                    let mut lines = Vec::new();
                    for item in &self.state.input.queue_display.items {
                        let badge = match &item.tier {
                            ava_types::MessageTier::Steering => "[S]".to_string(),
                            ava_types::MessageTier::FollowUp => "[F]".to_string(),
                            ava_types::MessageTier::PostComplete { group } => format!("[G{group}]"),
                        };
                        let truncated = crate::text_utils::truncate_display(&item.text, 60);
                        lines.push(format!("{badge} {truncated}"));
                    }
                    Some((MessageKind::System, format!("Queued messages:\n{}", lines.join("\n"))))
                }
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
}

#[cfg(test)]
mod tests {
    use crate::app::{App, ModalType};
    use crate::event::{AppEvent, ModelSwitchContext};
    use tempfile::tempdir;
    use tokio::sync::mpsc;

    #[tokio::test]
    async fn slash_model_switch_uses_async_event_when_sender_available() {
        let temp = tempdir().expect("tempdir");
        let db_path = temp.path().join("data.db");
        let mut app = App::test_new(&db_path);
        let (tx, mut rx) = mpsc::unbounded_channel();

        let result = app.handle_slash_command("/model openrouter/test-model", Some(tx));

        assert!(result.is_none());
        match rx.recv().await {
            Some(AppEvent::ModelSwitchFinished(result)) => {
                assert_eq!(result.provider, "openrouter");
                assert_eq!(result.model, "test-model");
                assert!(matches!(result.context, ModelSwitchContext::SlashCommand));
                assert!(result.result.is_err());
            }
            other => panic!("unexpected event: {other:?}"),
        }
    }

    #[tokio::test]
    async fn slash_tools_opens_modal_and_loads_async() {
        let temp = tempdir().expect("tempdir");
        let db_path = temp.path().join("data.db");
        let mut app = App::test_new(&db_path);
        let (tx, mut rx) = mpsc::unbounded_channel();

        let result = app.handle_slash_command("/tools", Some(tx));

        assert!(result.is_none());
        assert!(matches!(app.state.active_modal, Some(ModalType::ToolList)));
        match rx.recv().await {
            Some(AppEvent::ToolListLoaded(Err(err))) => {
                assert!(err.contains("AgentStack not initialised"));
            }
            other => panic!("unexpected event: {other:?}"),
        }
    }
}
