use super::*;

impl App {
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

    pub(super) fn handle_hooks_command(
        &mut self,
        arg: Option<&str>,
    ) -> Option<(MessageKind, String)> {
        use crate::hooks::{HookContext, HookEvent, HookRunner};

        match arg {
            Some("reload") => {
                self.state.hooks.reload();
                let count = self.state.hooks.len();
                self.set_status(format!("Reloaded {count} hooks"), StatusLevel::Info);
                Some((MessageKind::System, format!("Reloaded {count} hooks")))
            }
            Some(sub) if sub.starts_with("dry-run") => {
                let rest = sub.strip_prefix("dry-run").unwrap_or("").trim();
                let parts: Vec<&str> = rest.splitn(2, ' ').collect();
                let event_str = parts.first().copied().unwrap_or("");

                if event_str.is_empty() {
                    return Some((
                        MessageKind::Error,
                        "Usage: /hooks dry-run <event> [tool_name]\nEvents: PreToolUse, PostToolUse, PostToolUseFailure, SessionStart,\nSessionEnd, Stop, SubagentStart, SubagentStop, Notification,\nConfigChange, PreCompact, PermissionRequest, PreModelSwitch,\nPostModelSwitch, BudgetWarning, UserPromptSubmit"
                            .to_string(),
                    ));
                }

                let Some(event) = HookEvent::from_str_loose(event_str) else {
                    return Some((
                        MessageKind::Error,
                        format!("Unknown event: {event_str}. Use /hooks dry-run for list."),
                    ));
                };

                let mut ctx = HookContext::for_event(&event);
                if let Some(tool_name) = parts.get(1) {
                    ctx.tool_name = Some(tool_name.to_string());
                }
                ctx.model = Some(self.state.agent.model_name.clone());

                let lines = HookRunner::dry_run(&self.state.hooks, &event, &ctx);
                if lines.is_empty() {
                    Some((
                        MessageKind::System,
                        format!("No hooks would fire for {}", event.label()),
                    ))
                } else {
                    let output = format!(
                        "Hooks that would fire for {} ({}):\n{}",
                        event.label(),
                        lines.len(),
                        lines.join("\n")
                    );
                    Some((MessageKind::System, output))
                }
            }
            Some("list") | None => {
                if self.state.hooks.is_empty() {
                    Some((
                        MessageKind::System,
                        "No hooks loaded.\nAdd .toml files to .ava/hooks/ or $XDG_CONFIG_HOME/ava/hooks/.\nRun /init to create example templates.".to_string(),
                    ))
                } else {
                    let mut lines = Vec::new();
                    lines.push(format!("Hooks ({}):", self.state.hooks.len()));
                    for hook in self.state.hooks.iter() {
                        let enabled = if hook.enabled { " " } else { "x" };
                        let desc = hook
                            .description
                            .clone()
                            .unwrap_or_else(|| "no description".to_string());
                        let source = hook.source.label();
                        let matcher = hook
                            .matcher
                            .as_deref()
                            .map(|m| format!(" [{m}]"))
                            .unwrap_or_default();
                        lines.push(format!(
                            "  [{enabled}] {}{matcher} -- {desc} (pri {}, {source})",
                            hook.event, hook.priority
                        ));
                    }
                    Some((MessageKind::System, lines.join("\n")))
                }
            }
            Some(sub) => Some((
                MessageKind::Error,
                format!("Unknown /hooks subcommand: {sub}. Use: list, reload, dry-run <event>"),
            )),
        }
    }

    pub(super) fn handle_bookmark_command(
        &mut self,
        arg: Option<&str>,
    ) -> Option<(MessageKind, String)> {
        match arg {
            Some("list") | None => {
                // List bookmarks for current session
                match self.state.session.list_bookmarks() {
                    Ok(bookmarks) if bookmarks.is_empty() => Some((
                        MessageKind::System,
                        "No bookmarks in this session.".to_string(),
                    )),
                    Ok(bookmarks) => {
                        let mut lines = vec![format!("Bookmarks ({}):", bookmarks.len())];
                        for bm in &bookmarks {
                            let short_id = &bm.id.to_string()[..8];
                            lines.push(format!(
                                "  [{}] \"{}\" at message #{}",
                                short_id, bm.label, bm.message_index
                            ));
                        }
                        Some((MessageKind::System, lines.join("\n")))
                    }
                    Err(err) => Some((
                        MessageKind::Error,
                        format!("Failed to list bookmarks: {err}"),
                    )),
                }
            }
            Some("clear") => match self.state.session.clear_bookmarks() {
                Ok(count) => {
                    self.set_status(format!("Cleared {count} bookmarks"), StatusLevel::Info);
                    Some((MessageKind::System, format!("Cleared {count} bookmarks")))
                }
                Err(err) => Some((
                    MessageKind::Error,
                    format!("Failed to clear bookmarks: {err}"),
                )),
            },
            Some(sub) if sub.starts_with("remove ") => {
                let id_prefix = sub.trim_start_matches("remove ").trim();
                if id_prefix.is_empty() {
                    return Some((
                        MessageKind::Error,
                        "Usage: /bookmark remove <id-prefix>".to_string(),
                    ));
                }
                // Find bookmark by ID prefix
                match self.state.session.list_bookmarks() {
                    Ok(bookmarks) => {
                        let matches: Vec<_> = bookmarks
                            .iter()
                            .filter(|bm| bm.id.to_string().starts_with(id_prefix))
                            .collect();
                        match matches.len() {
                            0 => Some((
                                MessageKind::Error,
                                format!("No bookmark matching '{id_prefix}'"),
                            )),
                            1 => {
                                let bm = matches[0];
                                let label = bm.label.clone();
                                match self.state.session.remove_bookmark(bm.id) {
                                    Ok(()) => {
                                        self.set_status(
                                            format!("Removed bookmark: {label}"),
                                            StatusLevel::Info,
                                        );
                                        Some((
                                            MessageKind::System,
                                            format!("Removed bookmark: {label}"),
                                        ))
                                    }
                                    Err(err) => Some((
                                        MessageKind::Error,
                                        format!("Failed to remove bookmark: {err}"),
                                    )),
                                }
                            }
                            n => Some((
                                MessageKind::Error,
                                format!(
                                    "Ambiguous: {n} bookmarks match '{id_prefix}'. Be more specific."
                                ),
                            )),
                        }
                    }
                    Err(err) => Some((
                        MessageKind::Error,
                        format!("Failed to list bookmarks: {err}"),
                    )),
                }
            }
            Some(label) => {
                // Add a bookmark at the current message index
                let message_index = self.state.messages.messages.len().saturating_sub(1);
                match self.state.session.add_bookmark(label, message_index) {
                    Ok(bm) => {
                        let short_id = &bm.id.to_string()[..8];
                        self.set_status(format!("Bookmarked: {label}"), StatusLevel::Info);
                        Some((
                            MessageKind::System,
                            format!(
                                "Bookmark added: \"{}\" at message #{} [{}]",
                                bm.label, bm.message_index, short_id
                            ),
                        ))
                    }
                    Err(err) => {
                        Some((MessageKind::Error, format!("Failed to add bookmark: {err}")))
                    }
                }
            }
        }
    }

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

    pub(crate) fn execute_command_action(
        &mut self,
        action: Action,
        app_tx: Option<mpsc::UnboundedSender<AppEvent>>,
    ) {
        match action {
            Action::ModelSwitch => {
                if let Some(tx) = app_tx {
                    self.state.model_selector = Some(ModelSelectorState::default());
                    self.state.active_modal = Some(ModalType::ModelSelector);
                    self.spawn_model_selector_load(tx);
                } else {
                    let (credentials, catalog) = tokio::task::block_in_place(|| {
                        tokio::runtime::Handle::current().block_on(async {
                            let creds = ava_config::CredentialStore::load_default()
                                .await
                                .unwrap_or_default();
                            let cat = self.state.model_catalog.get().await;
                            (creds, cat)
                        })
                    });
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
            }
            Action::NewSession => {
                let _ = self.state.session.create_session();
                self.state.agent.clear_session_metrics();
                self.state.messages.messages.clear();
                self.state.view_mode = ViewMode::Main;
                self.state.messages.reset_scroll();
                self.set_status("New session created", StatusLevel::Info);
            }
            Action::SessionList => {
                self.state.session_list.update_sessions(&[]);
                self.state.session_list.open = true;
                self.state.active_modal = Some(ModalType::SessionList);
                if let Some(tx) = app_tx {
                    self.spawn_session_list_load(tx);
                } else if let Ok(sessions) = self.state.session.list_recent(50) {
                    self.state.session_list.update_sessions(&sessions);
                }
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
            Action::ModeNext => {
                self.state.agent_mode = self.state.agent_mode.cycle_next();
                self.state
                    .agent
                    .set_mode(self.state.agent_mode, app_tx.clone());
                self.set_status(
                    format!("Mode: {}", self.state.agent_mode.label()),
                    StatusLevel::Info,
                );
            }
            Action::ModePrev => {
                self.state.agent_mode = self.state.agent_mode.cycle_prev();
                self.state
                    .agent
                    .set_mode(self.state.agent_mode, app_tx.clone());
                self.set_status(
                    format!("Mode: {}", self.state.agent_mode.label()),
                    StatusLevel::Info,
                );
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
                if let Some((kind, msg)) = self.handle_slash_command("/compact", app_tx.clone()) {
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

    pub(super) fn format_standard_credential_result(
        result: Result<String, String>,
    ) -> crate::event::CommandMessageResult {
        match result {
            Ok(msg) => crate::event::CommandMessageResult {
                kind: MessageKind::System,
                content: msg.clone(),
                status: Some((StatusLevel::Info, msg)),
                transient: false,
            },
            Err(err) => crate::event::CommandMessageResult {
                kind: MessageKind::Error,
                content: format!("Failed: {err}"),
                status: Some((StatusLevel::Error, format!("Failed: {err}"))),
                transient: false,
            },
        }
    }
}
