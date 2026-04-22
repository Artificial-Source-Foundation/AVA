use super::*;

fn apply_device_code_ready(
    pc: &mut crate::widgets::provider_connect::ProviderConnectState,
    provider_id: String,
    device: ava_auth::device_code::DeviceCodeResponse,
    attempt: u64,
) {
    if pc.current_auth_attempt() == attempt {
        pc.screen = crate::widgets::provider_connect::ConnectScreen::DeviceCode {
            provider_id,
            user_code: device.user_code,
            verification_uri: device.verification_uri,
            started: std::time::Instant::now(),
        };
        pc.message = None;
    }
}

impl App {
    pub(super) fn handle_runtime_event(
        &mut self,
        event: AppEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        agent_tx: mpsc::UnboundedSender<ava_agent::AgentEvent>,
    ) {
        match event {
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
                    let crate::event::SessionLoadResult {
                        session,
                        restore_model,
                        restore_primary_agent_id,
                        restore_primary_agent_prompt,
                    } = loaded;
                    self.state.session.current_session = Some(session.clone());
                    self.state.agent.apply_session_summary(&session);
                    self.state.messages.messages.clear();
                    self.state.messages.reset_scroll();
                    for msg in crate::app::session_messages_to_ui_messages(&session.messages) {
                        self.state.messages.push(msg);
                    }
                    self.state.agent.set_primary_agent_profile(
                        restore_primary_agent_id,
                        restore_primary_agent_prompt,
                        Some(app_tx.clone()),
                    );
                    if let Some((provider, model)) = restore_model {
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
                    attempt,
                } => {
                    if let Some(ref mut pc) = self.state.provider_connect {
                        apply_device_code_ready(pc, provider_id, device, attempt);
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
                    store.set_oauth_tokens(&provider, &tokens);
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
            AppEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
                ..
            } => {
                self.handle_interactive_request_cleared(
                    &request_id,
                    request_kind,
                    timed_out,
                    app_tx.clone(),
                );
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
            AppEvent::Quit
            | AppEvent::Key(_)
            | AppEvent::Paste(_)
            | AppEvent::Resize(_, _)
            | AppEvent::Mouse(_)
            | AppEvent::Tick => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::apply_device_code_ready;
    use crate::widgets::provider_connect::{ConnectScreen, ProviderConnectState};
    use ava_config::CredentialStore;

    #[test]
    fn stale_device_code_ready_is_ignored_after_cancel() {
        let mut state = ProviderConnectState::for_provider(&CredentialStore::default(), "openai");
        let attempt = state.begin_auth_attempt();
        state.cancel_auth_attempt();
        state.message = Some("cancelled".to_string());

        apply_device_code_ready(
            &mut state,
            "openai".to_string(),
            ava_auth::device_code::DeviceCodeResponse {
                device_code: "device-auth".to_string(),
                user_code: "CODE-123".to_string(),
                verification_uri: "https://auth.openai.com/codex/device".to_string(),
                expires_in: 300,
                interval: 5,
            },
            attempt,
        );

        assert!(matches!(
            state.screen,
            ConnectScreen::AuthMethodChoice { .. }
        ));
        assert_eq!(state.message.as_deref(), Some("cancelled"));
    }
}
