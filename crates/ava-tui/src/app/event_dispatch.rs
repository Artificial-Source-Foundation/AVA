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
                                }
                                return;
                            }
                            if let Some(idx) = self.state.messages.message_index_at_row(mouse.row) {
                                if let Some(msg) = self.state.messages.messages.get(idx) {
                                    match msg.kind {
                                        // Toggle thinking block expand/collapse.
                                        crate::state::messages::MessageKind::Thinking => {
                                            self.state.messages.toggle_thinking_at(idx);
                                        }
                                        // Toggle tool action group expand/collapse.
                                        crate::state::messages::MessageKind::ToolCall
                                        | crate::state::messages::MessageKind::ToolResult => {
                                            self.state.messages.toggle_tool_group_at(idx);
                                        }
                                        _ => {}
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
            AppEvent::AgentRunEvent { run_id, event } => {
                self.route_agent_event(run_id, event, app_tx, agent_tx);
            }
            AppEvent::AgentRunDone { run_id, result } => {
                self.finish_routed_run(run_id, result, app_tx.clone());
            }
            AppEvent::BackgroundCleanupResult { task_id, result } => {
                if let Err(err) = result {
                    self.set_status(
                        format!("Background task #{task_id} cleanup failed: {err}"),
                        StatusLevel::Error,
                    );
                }
            }
            AppEvent::TokenUsage(usage) => {
                self.state.agent.tokens_used = usage;
            }
            AppEvent::ModelSelectorLoaded(result) => match result {
                Ok(selector) => {
                    self.state.model_selector = Some(selector);
                }
                Err(err) => {
                    self.set_status(format!("Failed to load models: {err}"), StatusLevel::Error);
                }
            },
            AppEvent::ModelSwitchFinished(result) => match result.result {
                Ok(()) => {
                    let desc = self
                        .state
                        .agent
                        .apply_switched_model(&result.provider, &result.model);
                    match result.context {
                        crate::event::ModelSwitchContext::Selector => {
                            self.state.model_selector = None;
                            self.state.active_modal = None;
                            self.set_status(
                                format!("Switched to {}", result.display),
                                StatusLevel::Info,
                            );
                        }
                        crate::event::ModelSwitchContext::SessionRestore => {
                            self.set_status(
                                format!("Session loaded - model: {desc}"),
                                StatusLevel::Info,
                            );
                        }
                        crate::event::ModelSwitchContext::SlashCommand => {
                            self.set_status(format!("Switched to {desc}"), StatusLevel::Info);
                            self.state.messages.push(UiMessage::new(
                                MessageKind::System,
                                format!("Switched to {desc}"),
                            ));
                        }
                    }
                }
                Err(err) => {
                    let status = format!("Failed: {err}");
                    match result.context {
                        crate::event::ModelSwitchContext::Selector => {
                            self.state.model_selector = None;
                            self.state.active_modal = None;
                        }
                        crate::event::ModelSwitchContext::SlashCommand => {
                            self.state.messages.push(UiMessage::new(
                                MessageKind::Error,
                                format!("Failed to switch model: {err}"),
                            ));
                        }
                        crate::event::ModelSwitchContext::SessionRestore => {}
                    }
                    self.set_status(status, StatusLevel::Error);
                }
            },
            AppEvent::ToolListLoaded(result) => match result {
                Ok(items) => {
                    let count = items.len();
                    self.state.tool_list = ToolListState::from_items(items);
                    self.set_status(format!("Loaded {count} tools"), StatusLevel::Info);
                }
                Err(err) => {
                    self.set_status(format!("Failed to load tools: {err}"), StatusLevel::Error);
                }
            },
            AppEvent::McpServersLoaded(result) => match result {
                Ok(servers) => {
                    let count = servers.len();
                    self.state.mcp_servers = servers.clone();
                    let content = super::commands::format_mcp_server_list(&servers);
                    self.set_status(format!("Loaded {count} MCP servers"), StatusLevel::Info);
                    self.state.info_panel = Some(super::InfoPanelState {
                        title: "MCP Servers".to_string(),
                        content,
                        scroll: 0,
                    });
                    self.state.active_modal = Some(super::ModalType::InfoPanel);
                }
                Err(err) => self.set_status(
                    format!("Failed to load MCP servers: {err}"),
                    StatusLevel::Error,
                ),
            },
            AppEvent::LspEntriesLoaded(result) => {
                self.lsp_refresh_inflight = false;
                match result {
                    Ok(entries) => {
                        self.state.lsp_entries = entries;
                    }
                    Err(err) => {
                        self.set_status(
                            format!("Failed to refresh LSP rows: {err}"),
                            StatusLevel::Error,
                        );
                    }
                }
            }
            AppEvent::CommandMessage(result) => {
                if let Some((level, text)) = result.status {
                    self.set_status(text, level);
                }
                if result.transient && !result.content.contains('\n') {
                    // Short single-line transient messages go to toast overlay
                    self.state.toast.push(result.content);
                } else {
                    let msg = if result.transient {
                        UiMessage::transient(result.kind, result.content)
                    } else {
                        UiMessage::new(result.kind, result.content)
                    };
                    self.state.messages.push(msg);
                }
            }
            AppEvent::SessionListLoaded(result) => match result {
                Ok(sessions) => {
                    self.state.session_list.update_sessions(&sessions);
                }
                Err(err) => self.set_status(
                    format!("Failed to load sessions: {err}"),
                    StatusLevel::Error,
                ),
            },
            AppEvent::SessionLoaded(result) => match result {
                Ok(loaded) => {
                    self.state.session.current_session = Some(loaded.session.clone());
                    self.state.agent.apply_session_summary(&loaded.session);
                    self.state.messages.messages.clear();
                    self.state.messages.reset_scroll();
                    for msg in &loaded.session.messages {
                        let kind = match msg.role {
                            ava_types::Role::User => MessageKind::User,
                            ava_types::Role::Assistant => MessageKind::Assistant,
                            ava_types::Role::Tool => MessageKind::ToolResult,
                            ava_types::Role::System => MessageKind::System,
                        };
                        self.state
                            .messages
                            .push(UiMessage::new(kind, msg.content.clone()));
                    }
                    if let Some((provider, model)) = loaded.restore_model {
                        self.spawn_model_switch(
                            provider,
                            model,
                            self.state.agent.current_model_display(),
                            crate::event::ModelSwitchContext::SessionRestore,
                            app_tx,
                        );
                    } else {
                        self.set_status("Session loaded", StatusLevel::Info);
                    }
                }
                Err(err) => {
                    self.set_status(format!("Failed to load session: {err}"), StatusLevel::Error);
                }
            },
            AppEvent::ProviderConnectLoaded(result) => match result {
                Ok(state) => {
                    self.state.provider_connect = Some(state);
                }
                Err(err) => self.set_status(
                    format!("Failed to load providers: {err}"),
                    StatusLevel::Error,
                ),
            },
            AppEvent::ProviderConnectFinished(result) => match result {
                crate::event::ProviderConnectResult::Loaded(state) => {
                    self.state.provider_connect = Some(state);
                }
                crate::event::ProviderConnectResult::Refreshed { state, status } => {
                    self.state.provider_connect = Some(state);
                    self.set_status(status, StatusLevel::Info);
                }
                crate::event::ProviderConnectResult::Tested(result) => match result {
                    Ok(msg) => self.set_status(&msg, StatusLevel::Info),
                    Err(err) => self.set_status(format!("Test failed: {err}"), StatusLevel::Error),
                },
                crate::event::ProviderConnectResult::Saved(result) => match result {
                    Ok(msg) => {
                        self.set_status(&msg, StatusLevel::Info);
                        self.state.provider_connect = None;
                        self.state.active_modal = None;
                        // Hot-reload credentials into the agent stack so newly
                        // added providers are usable without restarting.
                        if let Some(stack) = self.state.agent.stack_handle() {
                            tokio::spawn(async move {
                                if let Ok(store) = ava_config::CredentialStore::load_default().await
                                {
                                    stack.router.update_credentials(store).await;
                                }
                            });
                        }
                    }
                    Err(err) => {
                        if let Some(ref mut pc) = self.state.provider_connect {
                            pc.message = Some(format!("Failed: {err}"));
                        }
                    }
                },
                crate::event::ProviderConnectResult::OAuthStored { provider, result } => {
                    match result {
                        Ok(()) => {
                            self.set_status(
                                format!("Connected to {}", ava_config::provider_name(&provider)),
                                StatusLevel::Info,
                            );
                            self.state.provider_connect = None;
                            self.state.active_modal = None;
                            // Hot-reload credentials after OAuth flow
                            if let Some(stack) = self.state.agent.stack_handle() {
                                tokio::spawn(async move {
                                    if let Ok(store) =
                                        ava_config::CredentialStore::load_default().await
                                    {
                                        stack.router.update_credentials(store).await;
                                    }
                                });
                            }
                        }
                        Err(err) => {
                            self.set_status(
                                format!("Failed to save credentials: {err}"),
                                StatusLevel::Error,
                            );
                        }
                    }
                }
                crate::event::ProviderConnectResult::ConfigureLoaded {
                    provider_id,
                    base_url,
                } => {
                    if let Some(ref mut pc) = self.state.provider_connect {
                        pc.screen =
                            crate::widgets::provider_connect::ConnectScreen::Configure(provider_id);
                        pc.key_input.clear();
                        pc.base_url_input = base_url;
                        pc.active_field = crate::widgets::provider_connect::ConnectField::ApiKey;
                        pc.message = None;
                    }
                }
                crate::event::ProviderConnectResult::DeviceCodeReady {
                    provider_id,
                    device,
                } => {
                    if let Some(ref mut pc) = self.state.provider_connect {
                        pc.screen = crate::widgets::provider_connect::ConnectScreen::DeviceCode {
                            provider_id,
                            user_code: device.user_code,
                            verification_uri: device.verification_uri,
                            started: std::time::Instant::now(),
                        };
                        pc.message = None;
                    }
                }
                crate::event::ProviderConnectResult::InlineError(err) => {
                    if let Some(ref mut pc) = self.state.provider_connect {
                        pc.message = Some(err);
                    }
                }
            },
            AppEvent::ShellResult(kind, content) => {
                self.state.messages.push(UiMessage::new(kind, content));
            }
            AppEvent::VoiceReady(text) => {
                self.state.voice.phase = VoicePhase::Idle;
                self.state.voice.recording_start = None;
                self.state.voice.amplitude = 0.0;
                if !text.trim().is_empty() {
                    self.state.input.insert_str(text.trim());
                    if self.state.voice.auto_submit {
                        if let Some(goal) = self.state.input.submit() {
                            self.submit_goal(goal, app_tx, agent_tx);
                        }
                    }
                }
            }
            AppEvent::VoiceError(err) => {
                self.state.voice.phase = VoicePhase::Idle;
                self.state.voice.recording_start = None;
                self.state.voice.amplitude = 0.0;
                self.state.voice.error = Some(err.clone());
                self.set_status(format!("Voice: {err}"), StatusLevel::Error);
            }
            AppEvent::VoiceAmplitude(amp) => {
                self.state.voice.amplitude = amp;
            }
            AppEvent::VoiceSilenceDetected =>
            {
                #[cfg(feature = "voice")]
                if self.state.voice.phase == VoicePhase::Recording {
                    self.stop_and_transcribe(app_tx);
                }
            }
            AppEvent::OAuthSuccess { provider, tokens } => {
                let tx = app_tx.clone();
                tokio::spawn(async move {
                    let mut store = ava_config::CredentialStore::load_default()
                        .await
                        .unwrap_or_default();
                    store.set_oauth(
                        &provider,
                        &tokens.access_token,
                        tokens.refresh_token.as_deref(),
                        tokens.expires_at,
                    );
                    let _ = tx.send(AppEvent::ProviderConnectFinished(
                        crate::event::ProviderConnectResult::OAuthStored {
                            provider,
                            result: store.save_default().await.map_err(|err| err.to_string()),
                        },
                    ));
                });
            }
            AppEvent::OAuthError { provider, error } => {
                self.set_status(
                    format!("{}: {error}", ava_config::provider_name(&provider)),
                    StatusLevel::Error,
                );
                if let Some(ref mut pc) = self.state.provider_connect {
                    pc.screen = crate::widgets::provider_connect::ConnectScreen::List;
                    pc.message = Some(format!("Failed: {error}"));
                }
            }
            AppEvent::Question(req) => {
                self.state.question = Some(QuestionState {
                    question: req.question,
                    options: req.options.clone(),
                    selected: 0,
                    input: String::new(),
                    reply: Some(req.reply),
                });
                self.state.active_modal = Some(ModalType::Question);
            }
            AppEvent::ToolApproval(req) => {
                let inspection = Some(crate::state::permission::InspectionInfo {
                    risk_level: req.inspection.risk_level,
                    tags: req.inspection.tags,
                    warnings: req.inspection.warnings,
                });
                self.state
                    .permission
                    .enqueue(crate::state::permission::ApprovalRequest {
                        call: req.call,
                        approve_tx: req.reply,
                        inspection,
                    });
                self.state.active_modal = Some(ModalType::ToolApproval);
            }
            AppEvent::PlanProposal(req) => {
                self.state.plan_approval = Some(
                    crate::state::plan_approval::PlanApprovalState::new(req.plan, req.reply),
                );
                self.state.active_modal = Some(ModalType::PlanApproval);
            }
            AppEvent::HookResult {
                event,
                result,
                description,
            } => match result {
                HookResult::Block(reason) => {
                    self.set_status(
                        format!("Hook blocked {event}: {reason}"),
                        StatusLevel::Error,
                    );
                    debug!(hook = %description, event = %event, reason = %reason, "hook blocked action");
                }
                HookResult::Error(msg) => {
                    self.set_status(format!("Hook error ({event}): {msg}"), StatusLevel::Error);
                    debug!(hook = %description, event = %event, error = %msg, "hook error");
                }
                HookResult::Allow => {
                    debug!(hook = %description, event = %event, "hook allowed");
                }
            },
            AppEvent::ReviewFinished(result) => {
                match result {
                    None => {
                        self.state.messages.push(UiMessage::transient(
                            MessageKind::System,
                            "No changes to review.".to_string(),
                        ));
                    }
                    Some(Ok((formatted, has_actionable))) => {
                        let kind = if has_actionable {
                            MessageKind::Error
                        } else {
                            MessageKind::System
                        };
                        self.state
                            .messages
                            .push(UiMessage::transient(kind, formatted));
                        if has_actionable {
                            // Auto-fix: submit review findings as a new goal
                            self.state.messages.push(UiMessage::transient(
                                MessageKind::System,
                                "Submitting review findings for auto-fix...".to_string(),
                            ));
                            // The user can handle this by submitting the fix manually,
                            // or we could auto-submit here. For now, just show the findings.
                        }
                    }
                    Some(Err(e)) => {
                        self.state.messages.push(UiMessage::transient(
                            MessageKind::Error,
                            format!("Review failed: {e}"),
                        ));
                    }
                }
            }
        }
    }
}
