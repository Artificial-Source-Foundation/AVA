use base64::Engine as _;

use ava_agent::stack::MCPServerInfo;

use super::*;

/// Format MCP server list for display.
pub(crate) fn format_mcp_server_list(servers: &[MCPServerInfo]) -> String {
    if servers.is_empty() {
        return "No MCP servers configured".to_string();
    }
    let lines: Vec<String> = servers
        .iter()
        .map(|s| {
            let icon = if s.enabled { "\u{2713}" } else { "\u{2717}" };
            let scope = s.scope.to_string();
            if s.enabled {
                format!(
                    "  {icon} {} ({scope}) \u{2014} {} tools",
                    s.name, s.tool_count
                )
            } else {
                format!("  {icon} {} ({scope}) \u{2014} disabled", s.name)
            }
        })
        .collect();
    format!("MCP Servers:\n{}", lines.join("\n"))
}

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

        // --- command.execute.before hook (request/response, blocking) ---
        // Fires before ANY slash command executes. Plugins may block execution by
        // returning `{"block": true, "reason": "..."}`.
        if let Ok(stack) = self.state.agent.stack() {
            let cmd_name = cmd.trim_start_matches('/').to_string();
            let arguments = arg.unwrap_or("").to_string();
            let pm = Arc::clone(&stack.plugin_manager);
            let block_reason = tokio::task::block_in_place(|| {
                tokio::runtime::Handle::current().block_on(async move {
                    pm.lock()
                        .await
                        .check_command_execute_before(&cmd_name, &arguments)
                        .await
                })
            });
            if let Some(reason) = block_reason {
                let cmd_name_display = cmd.trim_start_matches('/');
                return Some((
                    MessageKind::Error,
                    format!("Command '{cmd_name_display}' blocked by plugin: {reason}"),
                ));
            }
        }

        match cmd {
            "/model" | "/models" => {
                if let Some(model_str) = arg {
                    // Parse "provider/model" format — split on first '/' only
                    if let Some((provider, model)) = model_str.split_once('/') {
                        let provider = provider.to_string();
                        let model = model.to_string();
                        if let Some(tx) = app_tx.clone() {
                            let display = format!("{provider}/{model}");
                            self.set_status(
                                format!("Switching to {display}..."),
                                StatusLevel::Info,
                            );
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
                                tokio::runtime::Handle::current()
                                    .block_on(self.state.agent.switch_model(&provider, &model))
                            });
                            match result {
                                Ok(desc) => {
                                    self.set_status(
                                        format!("Switched to {desc}"),
                                        StatusLevel::Info,
                                    );
                                    Some((MessageKind::System, format!("Switched to {desc}")))
                                }
                                Err(err) => {
                                    self.set_status(format!("Failed: {err}"), StatusLevel::Error);
                                    Some((
                                        MessageKind::Error,
                                        format!("Failed to switch model: {err}"),
                                    ))
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
            "/mcp" => {
                let mcp_parts: Vec<&str> = arg.unwrap_or("list").splitn(2, ' ').collect();
                let mcp_sub = mcp_parts[0];
                let mcp_arg = mcp_parts.get(1).map(|s| s.trim());
                match mcp_sub {
                    "reload" => {
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
                    "list" => {
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
                            let text = format_mcp_server_list(&servers);
                            self.state.info_panel = Some(super::InfoPanelState {
                                title: "MCP Servers".to_string(),
                                content: text,
                                scroll: 0,
                            });
                            self.state.active_modal = Some(super::ModalType::InfoPanel);
                            None
                        }
                    }
                    "enable" => {
                        if let Some(name) = mcp_arg {
                            let result = tokio::task::block_in_place(|| {
                                tokio::runtime::Handle::current()
                                    .block_on(self.state.agent.mcp_enable_server(name))
                            });
                            match result {
                                Ok(true) => {
                                    self.set_status(
                                        format!("Enabled MCP server: {name}"),
                                        StatusLevel::Info,
                                    );
                                    Some((
                                        MessageKind::System,
                                        format!("Enabled MCP server: {name} (tools reloaded)"),
                                    ))
                                }
                                Ok(false) => Some((
                                    MessageKind::Error,
                                    format!("MCP server '{name}' is not disabled"),
                                )),
                                Err(err) => Some((
                                    MessageKind::Error,
                                    format!("Failed to enable '{name}': {err}"),
                                )),
                            }
                        } else {
                            Some((
                                MessageKind::Error,
                                "Usage: /mcp enable <name>".to_string(),
                            ))
                        }
                    }
                    "disable" => {
                        if let Some(name) = mcp_arg {
                            let result = tokio::task::block_in_place(|| {
                                tokio::runtime::Handle::current()
                                    .block_on(self.state.agent.mcp_disable_server(name))
                            });
                            match result {
                                Ok(true) => {
                                    self.set_status(
                                        format!("Disabled MCP server: {name}"),
                                        StatusLevel::Info,
                                    );
                                    Some((
                                        MessageKind::System,
                                        format!("Disabled MCP server: {name} (tools removed)"),
                                    ))
                                }
                                Ok(false) => Some((
                                    MessageKind::Error,
                                    format!("MCP server '{name}' not found"),
                                )),
                                Err(err) => Some((
                                    MessageKind::Error,
                                    format!("Failed to disable '{name}': {err}"),
                                )),
                            }
                        } else {
                            Some((
                                MessageKind::Error,
                                "Usage: /mcp disable <name>".to_string(),
                            ))
                        }
                    }
                    sub => Some((
                        MessageKind::Error,
                        format!(
                            "Unknown /mcp subcommand: {sub}. Use: list, reload, enable <name>, disable <name>"
                        ),
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
            "/new" => {
                self.state.messages.messages.clear();
                self.state.messages.reset_scroll();
                self.state.agent.clear_session_metrics();
                self.pending_images.clear();
                self.state.pending_image_count = 0;
                if let Some(title) = arg {
                    if let Some(ref session) = self.state.session.current_session {
                        let mut meta = session.metadata.clone();
                        meta["title"] = serde_json::json!(title);
                    }
                }
                self.set_status("New session started", StatusLevel::Info);
                Some((MessageKind::System, "Started new session".to_string()))
            }
            "/clear" => {
                self.state.messages.messages.clear();
                self.state.messages.reset_scroll();
                self.state.agent.clear_session_metrics();
                self.pending_images.clear();
                self.state.pending_image_count = 0;
                self.set_status("Chat cleared", StatusLevel::Info);
                None
            }
            "/compact" => self.run_compact(arg),
            "/think" => match arg {
                Some("show" | "on") => {
                    self.state.agent.show_thinking = true;
                    self.state.toast.push("Thinking blocks visible");
                    None
                }
                Some("hide" | "off") => {
                    self.state.agent.show_thinking = false;
                    self.state.toast.push("Thinking blocks hidden");
                    None
                }
                Some(_) => Some((
                    MessageKind::Error,
                    "Usage: /think [show|on|hide|off] \u{2014} toggle thinking visibility"
                        .to_string(),
                )),
                None => {
                    self.state.agent.show_thinking = !self.state.agent.show_thinking;
                    let label = if self.state.agent.show_thinking {
                        "visible"
                    } else {
                        "hidden"
                    };
                    self.state.toast.push(format!("Thinking blocks {label}"));
                    None
                }
            },
            "/sessions" => {
                self.execute_command_action(Action::SessionList, app_tx.clone());
                None
            }
            "/bookmark" | "/bm" => self.handle_bookmark_command(arg),
            "/permissions" => match arg {
                Some("list" | "rules") => {
                    let cwd = std::env::current_dir().unwrap_or_default();
                    let ruleset = ava_permissions::glob_rules::GlobRuleset::load_merged(&cwd);
                    let rules = ruleset.rules();
                    let content = if rules.is_empty() {
                        "No glob permission rules active.\n\n\
                         Define rules in:\n  \
                         ~/.ava/permissions.toml   (global)\n  \
                         .ava/permissions.toml     (project)\n  \
                         ~/.ava/config.yaml        (permissions.path_rules)"
                            .to_string()
                    } else {
                        let mut lines = vec![format!("{} active path rules:\n", rules.len())];
                        for (i, rule) in rules.iter().enumerate() {
                            let action = match rule.action {
                                ava_permissions::glob_rules::GlobAction::Allow => "allow",
                                ava_permissions::glob_rules::GlobAction::Ask => "ask",
                                ava_permissions::glob_rules::GlobAction::Deny => "deny",
                            };
                            lines.push(format!("  {}. {} -> {}", i + 1, rule.pattern, action));
                        }
                        lines.push(String::new());
                        lines.push("Sources (in priority order):".to_string());
                        lines.push("  1. ~/.ava/permissions.toml (global)".to_string());
                        lines.push("  2. .ava/permissions.toml  (project, allow->ask)".to_string());
                        lines.push("  3. config.yaml permissions.path_rules".to_string());
                        lines.join("\n")
                    };
                    self.state.active_modal = Some(ModalType::InfoPanel);
                    self.state.info_panel = Some(super::InfoPanelState {
                        title: "Permission Rules".to_string(),
                        content,
                        scroll: 0,
                    });
                    None
                }
                _ => {
                    self.state.permission.permission_level =
                        self.state.permission.permission_level.toggle();
                    let label = self.state.permission.permission_level.label();
                    self.set_status(format!("Permissions: {label}"), StatusLevel::Info);
                    None
                }
            },
            "/theme" => {
                match arg {
                    Some(name) => {
                        let names = Theme::all_names();
                        if names.iter().any(|n| n == name) {
                            self.state.theme = Theme::from_name(name);
                            self.set_status(format!("Theme: {name}"), StatusLevel::Info);
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
            "/plan" => {
                let sub = arg.unwrap_or("");
                match sub {
                    "view" => {
                        let plan = self.state.agent.plan_state().and_then(|ps| ps.get());
                        if let Some(plan) = plan {
                            let json = serde_json::to_string(&plan).unwrap_or_default();
                            let encoded = base64::engine::general_purpose::URL_SAFE_NO_PAD
                                .encode(json.as_bytes());
                            let url = format!("http://localhost:8080/#plan={encoded}");

                            let open_result = {
                                #[cfg(target_os = "linux")]
                                {
                                    std::process::Command::new("xdg-open")
                                        .arg(&url)
                                        .stdin(std::process::Stdio::null())
                                        .stdout(std::process::Stdio::null())
                                        .stderr(std::process::Stdio::null())
                                        .spawn()
                                }
                                #[cfg(target_os = "macos")]
                                {
                                    std::process::Command::new("open")
                                        .arg(&url)
                                        .stdin(std::process::Stdio::null())
                                        .stdout(std::process::Stdio::null())
                                        .stderr(std::process::Stdio::null())
                                        .spawn()
                                }
                                #[cfg(target_os = "windows")]
                                {
                                    std::process::Command::new("cmd")
                                        .args(["/c", "start", &url])
                                        .stdin(std::process::Stdio::null())
                                        .stdout(std::process::Stdio::null())
                                        .stderr(std::process::Stdio::null())
                                        .spawn()
                                }
                            };

                            match open_result {
                                Ok(_) => {
                                    self.set_status("Plan opened in browser", StatusLevel::Info);
                                    Some((
                                        MessageKind::System,
                                        "Plan opened in browser".to_string(),
                                    ))
                                }
                                Err(e) => {
                                    self.set_status(
                                        format!("Failed to open browser: {e}"),
                                        StatusLevel::Error,
                                    );
                                    Some((
                                        MessageKind::Error,
                                        format!("Failed to open browser: {e}"),
                                    ))
                                }
                            }
                        } else {
                            self.set_status(
                                "No active plan. Use Plan mode to create one.",
                                StatusLevel::Info,
                            );
                            Some((
                                MessageKind::System,
                                "No active plan. Use Plan mode to create one.".to_string(),
                            ))
                        }
                    }
                    _ => {
                        let plan = self.state.agent.plan_state().and_then(|ps| ps.get());
                        if let Some(plan) = plan {
                            let step_count = plan.steps.len();
                            let codename = plan
                                .codename
                                .as_deref()
                                .map(|c| format!("{c} \u{2014} "))
                                .unwrap_or_default();
                            let msg =
                                format!("Plan: {codename}{} ({step_count} steps)", plan.summary,);
                            self.set_status(&msg, StatusLevel::Info);
                            Some((MessageKind::System, msg))
                        } else {
                            self.set_status("No active plan", StatusLevel::Info);
                            Some((MessageKind::System, "No active plan".to_string()))
                        }
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
                                tokio::task::spawn_blocking(
                                    super::git_commit::handle_commit_command,
                                )
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
            "/export" => self.export_conversation(arg),
            "/copy" => {
                let force_all = arg.map(|a| a.eq_ignore_ascii_case("all")).unwrap_or(false);
                self.copy_last_response_with_mode(force_all);
                None
            }
            "/shortcuts" | "/keys" | "/keybinds" => {
                self.show_shortcuts_overlay();
                None
            }
            "/help" => {
                let help = "\
/model [provider/model]  \u{2014} show or switch model (alias: /models)
/think [show|hide]       \u{2014} toggle thinking block visibility
/theme [name]            \u{2014} cycle or switch theme (default/dracula/nord)
/permissions [list]      \u{2014} toggle level or list glob rules
/connect [provider]      \u{2014} add provider credentials
/providers               \u{2014} show provider status
/disconnect <provider>   \u{2014} remove provider credentials
/mcp [list]              \u{2014} show MCP servers (scope + status)
/mcp reload              \u{2014} reload MCP config
/mcp enable <name>       \u{2014} enable a disabled MCP server
/mcp disable <name>      \u{2014} disable an MCP server (session-scoped)
/new [title]             \u{2014} start a new session (optional title)
/sessions                \u{2014} session picker
/bookmark [label]        \u{2014} bookmark current point (list/clear/remove)
/plan [view]              \u{2014} show plan status or open in browser
/commit                  \u{2014} inspect commit readiness and suggest a message
/export [filename]       \u{2014} export conversation to file (.md or .json)
/copy [all]              \u{2014} copy last response (picks code block if multiple)
/plugin                  \u{2014} list installed plugins
/hooks [list|reload|dry-run <event>] \u{2014} manage lifecycle hooks
/init                    \u{2014} create example project templates
/btw [question]          \u{2014} start a side conversation branch
/btw end                 \u{2014} restore original conversation
/tasks                   \u{2014} show background task list
/later <message>         \u{2014} queue a post-complete message
/queue                   \u{2014} show queued messages
/shortcuts               \u{2014} show keyboard shortcuts (Ctrl+?)
/clear                   \u{2014} clear chat
/compact [focus]         \u{2014} compact conversation to save context window
/help                    \u{2014} show this help";
                self.state.info_panel = Some(super::InfoPanelState {
                    title: "Help \u{2014} Available Commands".to_string(),
                    content: help.to_string(),
                    scroll: 0,
                });
                self.state.active_modal = Some(super::ModalType::InfoPanel);
                None
            }
            "/btw" => {
                if self.state.btw.active {
                    match arg {
                        Some("end" | "done" | "pop") => {
                            self.end_btw_branch();
                            None
                        }
                        _ => {
                            Some((
                                MessageKind::System,
                                "Already in a /btw branch. Use /btw end (or Ctrl+Z) to restore the original conversation.".to_string(),
                            ))
                        }
                    }
                } else {
                    self.start_btw_branch(arg.map(|s| s.to_string()));
                    None
                }
            }
            "/plugin" | "/plugins" => {
                let text = crate::plugin_commands::format_plugin_list_inline();
                self.state.info_panel = Some(super::InfoPanelState {
                    title: "Plugins".to_string(),
                    content: text,
                    scroll: 0,
                });
                self.state.active_modal = Some(super::ModalType::InfoPanel);
                None
            }
            "/hooks" => self.handle_hooks_command(arg),
            "/tasks" => {
                self.state.active_modal = Some(super::ModalType::TaskList);
                None
            }
            "/later" => {
                if let Some(text) = arg {
                    if text.is_empty() {
                        Some((
                            MessageKind::Error,
                            "Usage: /later <message> \u{2014} queue a post-complete message"
                                .to_string(),
                        ))
                    } else {
                        // Parse optional group: /later 2 message
                        let (group, message) =
                            if let Some(rest) = text.strip_prefix(|c: char| c.is_ascii_digit()) {
                                let num_str = format!(
                                    "{}{}",
                                    &text[..1],
                                    rest.chars()
                                        .take_while(|c| c.is_ascii_digit())
                                        .collect::<String>()
                                );
                                let after = text[num_str.len()..].trim();
                                if after.is_empty() {
                                    (1, text.to_string()) // Just a number, treat as message
                                } else {
                                    (num_str.parse().unwrap_or(1), after.to_string())
                                }
                            } else {
                                (1, text.to_string())
                            };
                        self.send_queued_message(
                            message,
                            ava_types::MessageTier::PostComplete { group },
                        );
                        None // send_queued_message already displays the message
                    }
                } else {
                    Some((
                        MessageKind::Error,
                        "Usage: /later <message> \u{2014} queue a post-complete message"
                            .to_string(),
                    ))
                }
            }
            "/queue" => {
                let text = if self.state.input.queue_display.is_empty() {
                    "No messages queued.".to_string()
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
                    format!("Queued messages:\n{}", lines.join("\n"))
                };
                self.state
                    .messages
                    .push(UiMessage::transient(MessageKind::System, text));
                None
            }
            "/init" => {
                let cwd = std::env::current_dir().unwrap_or_default();
                let ava_dir = cwd.join(".ava");
                let mut created = Vec::new();

                // Detect tech stack
                let mut stack_info = Vec::new();
                if cwd.join("Cargo.toml").exists() {
                    stack_info.push("Rust (Cargo)");
                }
                if cwd.join("package.json").exists() {
                    stack_info.push("Node.js (npm/yarn/pnpm)");
                }
                if cwd.join("pyproject.toml").exists() || cwd.join("requirements.txt").exists() {
                    stack_info.push("Python");
                }
                if cwd.join("go.mod").exists() {
                    stack_info.push("Go");
                }
                if cwd.join("Makefile").exists() || cwd.join("Justfile").exists() {
                    stack_info.push("Make/Just");
                }
                let stack_str = if stack_info.is_empty() {
                    "Unknown".to_string()
                } else {
                    stack_info.join(", ")
                };

                // Create .ava/ directory
                let _ = std::fs::create_dir_all(&ava_dir);

                // Create AGENTS.md if it doesn't exist
                let agents_path = cwd.join("AGENTS.md");
                if !agents_path.exists() {
                    let project_name = cwd.file_name().unwrap_or_default().to_string_lossy();
                    let agents_content = format!(
                        "# {project_name}\n\n\
                         ## Stack\n\
                         {stack_str}\n\n\
                         ## Conventions\n\
                         - Follow existing code style and patterns\n\
                         - Write tests for new functionality\n\
                         - Keep changes focused and minimal\n\n\
                         ## Build & Test\n\
                         ```bash\n\
                         # Add your build/test commands here\n\
                         ```\n"
                    );
                    if std::fs::write(&agents_path, agents_content).is_ok() {
                        created.push("AGENTS.md");
                    }
                }

                // Create .ava/mcp.json if it doesn't exist
                let mcp_path = ava_dir.join("mcp.json");
                if !mcp_path.exists() {
                    let mcp_content = "{\n  \"mcpServers\": {}\n}\n";
                    if std::fs::write(&mcp_path, mcp_content).is_ok() {
                        created.push(".ava/mcp.json");
                    }
                }

                // Create example TOML tools directory
                let tools_dir = ava_dir.join("tools");
                let _ = std::fs::create_dir_all(&tools_dir);
                let hello_path = tools_dir.join("hello.toml");
                if !hello_path.exists() {
                    let hello_content = "\
[tool]\nname = \"hello\"\ndescription = \"Example custom tool \u{2014} prints a greeting\"\n\n\
[tool.parameters.name]\ntype = \"string\"\ndescription = \"Name to greet\"\nrequired = true\n\n\
[execute]\ncommand = \"echo 'Hello, {{name}}!'\"\n";
                    if std::fs::write(&hello_path, hello_content).is_ok() {
                        created.push(".ava/tools/hello.toml");
                    }
                }

                if created.is_empty() {
                    Some((
                        MessageKind::System,
                        "Project already initialized \u{2014} all config files exist.".to_string(),
                    ))
                } else {
                    let mut msg =
                        format!("Detected: {stack_str}\n\nCreated project configuration:\n");
                    for file in &created {
                        msg.push_str(&format!("  \u{2713} {file}\n"));
                    }
                    msg.push_str("\nEdit these files to customize AVA for your project.");
                    Some((MessageKind::System, msg))
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
                    // Fuzzy match: treat typos near "/model" as the model command
                    let cmd_lower = cmd.to_ascii_lowercase();
                    if cmd_lower.starts_with("/mod") || cmd_lower.starts_with("/mode") {
                        self.execute_command_action(Action::ModelSwitch, app_tx.clone());
                        None
                    } else {
                        Some((
                            MessageKind::Error,
                            format!("Unknown command: {cmd}. Type /help for available commands."),
                        ))
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::app::App;
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
}
