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
                            self.state
                                .messages
                                .push(UiMessage::transient(MessageKind::System, text));
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
                self.set_status("Chat cleared", StatusLevel::Info);
                None
            }
            "/compact" => self.run_compact(arg),
            "/think" => match arg {
                Some("show") | Some("on") => {
                    self.state.agent.show_thinking = true;
                    self.state.toast.push("Thinking blocks visible");
                    None
                }
                Some("hide") | Some("off") => {
                    self.state.agent.show_thinking = false;
                    self.state.toast.push("Thinking blocks hidden");
                    None
                }
                Some(_) => Some((
                    MessageKind::Error,
                    "Usage: /think [show|on|hide|off] — toggle thinking visibility".to_string(),
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
            "/permissions" => {
                self.state.permission.permission_level =
                    self.state.permission.permission_level.toggle();
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
Available commands:
  /model [provider/model]  — show or switch model (alias: /models)
  /think [show|hide]       — toggle thinking block visibility
  /theme [name]            — cycle or switch theme (default/dracula/nord)
  /permissions             — toggle permission level
  /connect [provider]      — add provider credentials
  /providers               — show provider status
  /disconnect <provider>   — remove provider credentials
  /mcp [list]              — show MCP servers (scope + status)
  /mcp reload              — reload MCP config
  /mcp enable <name>       — enable a disabled MCP server
  /mcp disable <name>      — disable an MCP server (session-scoped)
  /new [title]             — start a new session (optional title)
  /sessions                — session picker
  /init                    — initialize project (tool, hook, and command templates)
  /commit                  — inspect commit readiness and suggest a message
  /export [filename]       — export conversation to file (.md or .json)
  /copy [all]              — copy last response (picks code block if multiple)
  /hooks [list|reload|dry-run <event>] — manage lifecycle hooks
  /btw [question]          — start a side conversation branch
  /btw end                 — restore original conversation
  /tasks                   — show background task list
  /later <message>         — queue a post-complete message
  /queue                   — show queued messages
  /shortcuts               — show keyboard shortcuts (Ctrl+?)
  /clear                   — clear chat
  /compact [focus]          — compact conversation to save context window
  /help                    — show this help";
                self.state
                    .messages
                    .push(UiMessage::transient(MessageKind::System, help));
                None
            }
            "/btw" => {
                if self.state.btw.active {
                    match arg {
                        Some("end") | Some("done") | Some("pop") => {
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
                            "Usage: /later <message> — queue a post-complete message".to_string(),
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
                        "Usage: /later <message> — queue a post-complete message".to_string(),
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
