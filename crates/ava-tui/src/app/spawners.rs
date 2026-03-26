use super::*;

impl App {
    pub(crate) fn spawn_model_selector_load(&self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        let catalog_state = self.state.model_catalog.clone();
        let recent_models = self.state.agent.recent_models.clone();
        let current_model = self.state.agent.model_name.clone();
        let current_provider = self.state.agent.provider_name.clone();
        let cli_agents = self
            .state
            .agent
            .stack_handle()
            .map(|s| s.cli_agents().to_vec())
            .unwrap_or_default();
        let app_tx_for_load = app_tx.clone();
        let load = async move {
            let credentials = ava_config::CredentialStore::load_default()
                .await
                .unwrap_or_default();
            let catalog = catalog_state.get().await;
            let mut effective_catalog = if catalog.is_empty() {
                ava_config::fallback_catalog()
            } else {
                catalog
            };
            effective_catalog.merge_fallback();
            let selector = ModelSelectorState::from_catalog_with_cli(
                &effective_catalog,
                &credentials,
                &recent_models,
                &current_model,
                &current_provider,
                &cli_agents,
            );
            let _ = app_tx_for_load.send(AppEvent::ModelSelectorLoaded(Ok(selector)));
        };

        if tokio::runtime::Handle::try_current().is_ok() {
            tokio::spawn(load);
        } else if let Ok(runtime) = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
        {
            runtime.block_on(load);
        } else {
            let _ = app_tx.send(AppEvent::ModelSelectorLoaded(Err(
                "Failed to start runtime for model selector".to_string(),
            )));
        }
    }

    pub(crate) fn spawn_model_switch(
        &self,
        provider: String,
        model: String,
        display: String,
        context: crate::event::ModelSwitchContext,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let Some(stack) = self.state.agent.stack_handle() else {
            let _ = app_tx.send(AppEvent::ModelSwitchFinished(
                crate::event::ModelSwitchResult {
                    provider,
                    model,
                    display,
                    result: Err("AgentStack not initialised".to_string()),
                    context,
                },
            ));
            return;
        };

        tokio::spawn(async move {
            let result = stack
                .switch_model(&provider, &model)
                .await
                .map(|_| ())
                .map_err(|err| err.to_string());
            let _ = app_tx.send(AppEvent::ModelSwitchFinished(
                crate::event::ModelSwitchResult {
                    provider,
                    model,
                    display,
                    result,
                    context,
                },
            ));
        });
    }

    pub(crate) fn spawn_mcp_server_list(&self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        let Some(stack) = self.state.agent.stack_handle() else {
            let _ = app_tx.send(AppEvent::McpServersLoaded(Err(
                "AgentStack not initialised".to_string(),
            )));
            return;
        };

        tokio::spawn(async move {
            let servers = stack.mcp_server_info().await;
            let _ = app_tx.send(AppEvent::McpServersLoaded(Ok(servers)));
        });
    }

    pub(crate) fn spawn_mcp_reload(&self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        let Some(stack) = self.state.agent.stack_handle() else {
            let _ = app_tx.send(AppEvent::CommandMessage(
                crate::event::CommandMessageResult {
                    kind: MessageKind::Error,
                    content: "Failed to reload MCP: AgentStack not initialised".to_string(),
                    status: Some((
                        StatusLevel::Error,
                        "Failed: AgentStack not initialised".to_string(),
                    )),
                    transient: false,
                },
            ));
            return;
        };

        tokio::spawn(async move {
            let result = stack.reload_mcp().await.map_err(|err| err.to_string());
            let (kind, content, status) = match result {
                Ok((servers, tools)) => {
                    let msg = format!("MCP reloaded: {servers} servers, {tools} tools");
                    (
                        MessageKind::System,
                        msg.clone(),
                        Some((StatusLevel::Info, msg)),
                    )
                }
                Err(err) => (
                    MessageKind::Error,
                    err.clone(),
                    Some((StatusLevel::Error, format!("Failed: {err}"))),
                ),
            };
            let _ = app_tx.send(AppEvent::CommandMessage(
                crate::event::CommandMessageResult {
                    kind,
                    content,
                    status,
                    transient: false,
                },
            ));
        });
    }

    pub(crate) fn spawn_credential_command(
        &self,
        command: ava_config::CredentialCommand,
        format: fn(Result<String, String>) -> crate::event::CommandMessageResult,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        tokio::spawn(async move {
            let mut store = ava_config::CredentialStore::load_default()
                .await
                .unwrap_or_default();
            let result = ava_config::execute_credential_command(command, &mut store)
                .await
                .map_err(|err| err.to_string());
            let _ = app_tx.send(AppEvent::CommandMessage(format(result)));
        });
    }

    // spawn_status_message, current_route_summary, build_status_summary removed — /status command was removed

    pub(crate) fn spawn_commit_prep(&self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        tokio::spawn(async move {
            let result = tokio::task::spawn_blocking(super::git_commit::handle_commit_command)
                .await
                .unwrap_or_else(|err| {
                    (
                        MessageKind::Error,
                        format!("Failed to inspect commit readiness: {err}"),
                    )
                });
            let _ = app_tx.send(AppEvent::CommandMessage(
                crate::event::CommandMessageResult {
                    kind: result.0,
                    content: result.1,
                    status: None,
                    transient: false,
                },
            ));
        });
    }

    pub(crate) fn spawn_provider_connect_load(
        &self,
        provider: Option<String>,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let cli_agents = self
            .state
            .agent
            .stack_handle()
            .map(|s| s.cli_agents().to_vec())
            .unwrap_or_default();
        tokio::spawn(async move {
            let credentials = ava_config::CredentialStore::load_default()
                .await
                .unwrap_or_default();
            let state = if let Some(provider) = provider {
                ProviderConnectState::for_provider(&credentials, &provider)
            } else {
                ProviderConnectState::from_credentials_with_cli(&credentials, &cli_agents)
            };
            let _ = app_tx.send(AppEvent::ProviderConnectLoaded(Ok(state)));
        });
    }

    pub(crate) fn spawn_session_list_load(&self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        let db_path = self.state.session.db_path().to_path_buf();
        tokio::spawn(async move {
            let result = tokio::task::spawn_blocking(move || {
                let manager =
                    ava_session::SessionManager::new(&db_path).map_err(|err| err.to_string())?;
                manager.list_recent(50).map_err(|err| err.to_string())
            })
            .await
            .map_err(|err| err.to_string())
            .and_then(|result| result);
            let _ = app_tx.send(AppEvent::SessionListLoaded(result));
        });
    }

    pub(crate) fn spawn_session_load(
        &self,
        session_id: uuid::Uuid,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let db_path = self.state.session.db_path().to_path_buf();
        tokio::spawn(async move {
            let result = tokio::task::spawn_blocking(move || {
                let manager =
                    ava_session::SessionManager::new(&db_path).map_err(|err| err.to_string())?;
                let session = manager
                    .get(session_id)
                    .map_err(|err| err.to_string())?
                    .ok_or_else(|| format!("Session {session_id} not found"))?;
                let restore_model = session.metadata.as_object().and_then(|meta| {
                    Some((
                        meta.get("provider")?.as_str()?.to_string(),
                        meta.get("model")?.as_str()?.to_string(),
                    ))
                });
                Ok(crate::event::SessionLoadResult {
                    session,
                    restore_model,
                })
            })
            .await
            .map_err(|err| err.to_string())
            .and_then(
                |result: std::result::Result<crate::event::SessionLoadResult, String>| result,
            );
            let _ = app_tx.send(AppEvent::SessionLoaded(result));
        });
    }

    /// Spawn a code review on working directory changes. Results appear as chat messages.
    pub(crate) fn spawn_review_pass(&mut self, app_tx: mpsc::UnboundedSender<AppEvent>) {
        use ava_praxis::review::{
            build_review_system_prompt, collect_diff, format_text, parse_review_output,
            run_review_agent, DiffMode, Severity,
        };

        let Some(stack) = self.state.agent.stack_handle() else {
            self.state.messages.push(UiMessage::transient(
                MessageKind::Error,
                "Cannot review: agent not initialised".to_string(),
            ));
            return;
        };

        self.state.messages.push(UiMessage::transient(
            MessageKind::System,
            "Running code review on working directory changes...".to_string(),
        ));

        tokio::spawn(async move {
            // Collect diff
            let review_context = match collect_diff(&DiffMode::Working).await {
                Ok(ctx) if ctx.diff.is_empty() => {
                    let _ = app_tx.send(AppEvent::ReviewFinished(None));
                    return;
                }
                Ok(ctx) => ctx,
                Err(e) => {
                    let _ = app_tx.send(AppEvent::ReviewFinished(Some(Err(e))));
                    return;
                }
            };

            // Get provider
            let (provider_name, model_name) = stack.current_model().await;
            let resolved_provider = match stack
                .router
                .route_required(&provider_name, &model_name)
                .await
            {
                Ok(p) => p,
                Err(e) => {
                    let _ = app_tx.send(AppEvent::ReviewFinished(Some(Err(e.to_string()))));
                    return;
                }
            };

            let system_prompt = build_review_system_prompt("bugs");
            let platform = std::sync::Arc::new(ava_platform::StandardPlatform);

            let output = match run_review_agent(
                resolved_provider,
                platform,
                &review_context,
                &system_prompt,
                5,
            )
            .await
            {
                Ok(o) => o,
                Err(e) => {
                    let _ = app_tx.send(AppEvent::ReviewFinished(Some(Err(e))));
                    return;
                }
            };

            let result = parse_review_output(&output);
            let has_actionable = result
                .issues
                .iter()
                .any(|i| matches!(i.severity, Severity::Critical | Severity::Warning));

            let formatted = format_text(&result);
            let _ = app_tx.send(AppEvent::ReviewFinished(Some(Ok((
                formatted,
                has_actionable,
            )))));
        });
    }
}
