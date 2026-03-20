mod stack_config;
mod stack_run;
pub(crate) mod stack_tools;

pub use stack_config::{AgentRunResult, AgentStackConfig};
pub use stack_tools::MCPServerInfo;

use std::path::PathBuf;
use std::sync::Arc;

use ava_codebase::indexer::{index_project, index_workspace};
use ava_codebase::CodebaseIndex;
use ava_config::AgentsConfig;
use ava_config::ConfigManager;
use ava_llm::provider::LLMProvider;
use ava_llm::ModelRouter;
use ava_mcp::config::{load_merged_mcp_config_with_scope, McpServerScope};
use ava_memory::MemorySystem;
use ava_permissions::inspector::{DefaultInspector, InspectionContext, PermissionInspector};
use ava_permissions::policy::PermissionPolicy;
use ava_permissions::tags::core_tool_profiles;
use ava_permissions::PermissionSystem;
use ava_platform::{Platform, StandardPlatform};
use ava_plugin::PluginManager;
use ava_session::SessionManager;
use ava_tools::core::plan::PlanBridge;
use ava_tools::core::question::QuestionBridge;
use ava_tools::core::{
    register_custom_tools, register_plan_tool, register_question_tool, register_todo_tools,
};
use ava_tools::permission_middleware::{convert_tool_source, ApprovalBridge, SharedToolSources};
use ava_tools::registry::{ToolRegistry, ToolSource};
use ava_types::{AvaError, PlanState, QueuedMessage, Result, ThinkingLevel, TodoState};
use tokio::sync::{mpsc, RwLock};
use tracing::{error, info, instrument, warn};

use stack_tools::{
    build_tool_registry, init_mcp, init_mcp_with_disabled, resolve_workspace_roots, MCPRuntime,
};

pub struct AgentStack {
    pub router: Arc<ModelRouter>,
    pub tools: Arc<RwLock<ToolRegistry>>,
    pub session_manager: Arc<SessionManager>,
    pub memory: Arc<MemorySystem>,
    pub config: ConfigManager,
    pub platform: Arc<dyn Platform>,
    pub codebase_index: Arc<RwLock<Option<Arc<CodebaseIndex>>>>,
    provider_override: RwLock<Option<String>>,
    model_override: RwLock<Option<String>>,
    routing_locked: RwLock<bool>,
    max_turns: usize,
    max_budget_usd: f64,
    #[allow(dead_code)] // Field retained for future use; accessor removed as dead code
    yolo: bool,
    injected_provider: Option<Arc<dyn LLMProvider>>,
    mcp: Arc<RwLock<Option<MCPRuntime>>>,
    /// Session-scoped set of disabled MCP server names.
    disabled_mcp_servers: RwLock<std::collections::HashSet<String>>,
    custom_tool_dirs: Vec<PathBuf>,
    mcp_global_config: PathBuf,
    mcp_project_config: PathBuf,
    pub thinking_level: RwLock<ThinkingLevel>,
    pub mode_prompt_suffix: RwLock<Option<String>>,
    /// When true, agent is in Plan mode — write/edit restricted to .ava/plans/*.md.
    pub plan_mode: RwLock<bool>,
    pub todo_state: TodoState,
    /// Shared plan state for the plan tool and TUI display.
    pub plan_state: PlanState,
    /// Bridge for the plan tool to communicate with the TUI.
    plan_bridge: PlanBridge,
    /// Bridge for the question tool to communicate with the TUI.
    question_bridge: QuestionBridge,
    /// Bridge for permission requests that require interactive approval.
    approval_bridge: ApprovalBridge,
    permission_context: Arc<RwLock<InspectionContext>>,
    permission_inspector: Arc<dyn PermissionInspector>,
    /// Shared tool-source map used by the permission middleware.
    /// Updated whenever tools are registered (init, MCP reconnect, custom reload).
    #[allow(dead_code)] // Stored for future dynamic-registration updates
    shared_tool_sources: SharedToolSources,
    /// Sub-agent configuration loaded from agents.toml files.
    agents_config: AgentsConfig,
    /// Compaction threshold as a percentage (50–95). Stored as integer, converted
    /// to fraction (0.50–0.95) when building `CondenserConfig`.
    compaction_threshold_pct: u8,
    /// When false, automatic context compaction is disabled entirely.
    auto_compact: bool,
    /// Parent session ID for linking sub-agent sessions back to their parent.
    /// Set by the TUI before calling `run()` so spawned sub-agents record lineage.
    pub parent_session_id: RwLock<Option<String>>,
    /// Plugin manager for power plugin lifecycle and hook dispatch.
    pub plugin_manager: Arc<tokio::sync::Mutex<PluginManager>>,
    /// JoinHandle for the background codebase indexing task.
    /// Stored so we can detect task panics and surface them to the caller.
    /// Wrapped in `std::sync::Mutex` so the field is `Send` (JoinHandle is Send).
    index_task: std::sync::Mutex<Option<tokio::task::JoinHandle<()>>>,
}

impl AgentStack {
    /// Create a new `AgentStack`.
    ///
    /// Returns the stack and a receiver for question requests. The caller
    /// (typically the TUI) should poll this receiver and present questions to the
    /// user, sending answers back via the embedded oneshot channel.
    pub async fn new(
        config: AgentStackConfig,
    ) -> Result<(
        Self,
        mpsc::UnboundedReceiver<ava_tools::core::question::QuestionRequest>,
        mpsc::UnboundedReceiver<ava_tools::permission_middleware::ApprovalRequest>,
        mpsc::UnboundedReceiver<ava_tools::core::plan::PlanRequest>,
    )> {
        tokio::fs::create_dir_all(&config.data_dir)
            .await
            .map_err(|e| AvaError::IoError(e.to_string()))?;

        let db_path = config.data_dir.join("data.db");
        let config_path = config.data_dir.join("config.yaml");
        let credentials_path = config.data_dir.join("credentials.json");

        let platform: Arc<dyn Platform> = Arc::new(StandardPlatform);

        let config_mgr = ConfigManager::load_from_paths(config_path, credentials_path).await?;
        let cfg = config_mgr.get().await;
        let credentials = config_mgr.credentials().await;
        let router = Arc::new(ModelRouter::new(credentials));

        let session_manager = Arc::new(SessionManager::new(&db_path)?);
        let memory = Arc::new(
            MemorySystem::new(&db_path).map_err(|e| AvaError::DatabaseError(e.to_string()))?,
        );

        let effective_cwd = config
            .working_dir
            .clone()
            .unwrap_or_else(|| std::env::current_dir().unwrap_or_default());

        let workspace_roots = resolve_workspace_roots(&effective_cwd, &cfg.workspace_roots);

        let codebase_index: Arc<RwLock<Option<Arc<CodebaseIndex>>>> = Arc::new(RwLock::new(None));
        let index_clone = codebase_index.clone();
        let project_root = effective_cwd.clone();
        let workspace_roots_for_task = workspace_roots.clone();
        let index_handle = tokio::spawn(async move {
            let index_result = if workspace_roots_for_task.len() > 1 {
                index_workspace(&workspace_roots_for_task).await
            } else {
                index_project(&project_root).await
            };

            match index_result {
                Ok(idx) => {
                    *index_clone.write().await = Some(Arc::new(idx));
                    info!("Codebase indexing complete");
                }
                Err(e) => warn!("Codebase indexing failed: {e}"),
            }
        });
        let routing_locked = config
            .provider
            .as_deref()
            .is_some_and(|provider| provider != cfg.llm.provider)
            || config
                .model
                .as_deref()
                .is_some_and(|model| model != cfg.llm.model);
        let provider_name = config.provider.as_deref().unwrap_or(&cfg.llm.provider);
        if let Some(base_url) = ava_llm::providers::base_url_for_provider(provider_name) {
            match router.pool().get_client(base_url).await {
                Ok(_) => info!(base_url, "Pre-warmed connection pool"),
                Err(e) => warn!(%e, base_url, "Failed to pre-warm connection pool"),
            }
        }

        let mcp_global_config = config.data_dir.join("mcp.json");
        let project_trusted = ava_config::is_project_trusted(&effective_cwd);
        let mcp_project_config = if project_trusted {
            effective_cwd.join(".ava").join("mcp.json")
        } else {
            let candidate = effective_cwd.join(".ava").join("mcp.json");
            if candidate.exists() {
                tracing::warn!(
                    "Skipping project-local MCP servers — project not trusted. \
                     Run with --trust to approve."
                );
            }
            // Use a non-existent path so the loader finds nothing
            config.data_dir.join(".mcp-project-skipped.json")
        };
        let custom_tool_dirs = if project_trusted {
            vec![
                config.data_dir.join("tools"),
                effective_cwd.join(".ava").join("tools"),
            ]
        } else {
            let candidate = effective_cwd.join(".ava").join("tools");
            if candidate.is_dir() {
                tracing::warn!(
                    "Skipping project-local custom tools — project not trusted. \
                     Run with --trust to approve."
                );
            }
            // Only load global custom tools
            vec![config.data_dir.join("tools")]
        };

        let agents_config = if project_trusted {
            AgentsConfig::load(
                &config.data_dir.join("agents.toml"),
                &effective_cwd.join(".ava").join("agents.toml"),
            )
        } else {
            let candidate = effective_cwd.join(".ava").join("agents.toml");
            if candidate.exists() {
                tracing::warn!(
                    "Skipping project-local agents.toml — project not trusted. \
                     Run with --trust to approve."
                );
            }
            // Only load global agents config
            AgentsConfig::load(
                &config.data_dir.join("agents.toml"),
                // Use a non-existent path so load_file returns None for the project config
                &config.data_dir.join(".agents-project-skipped.toml"),
            )
        };

        // Initialize plugin manager and load plugins from default directories.
        let mut plugin_mgr = PluginManager::new();
        let plugin_dirs = vec![
            config.data_dir.join("plugins"),
            effective_cwd.join(".ava").join("plugins"),
        ];
        if let Err(e) = plugin_mgr.load_plugins(&plugin_dirs).await {
            warn!("Failed to load plugins: {e}");
        }
        let plugin_manager = Arc::new(tokio::sync::Mutex::new(plugin_mgr));

        let todo_state = TodoState::new();
        let plan_state = PlanState::new();
        let (plan_bridge, plan_rx) = PlanBridge::new();
        let (question_bridge, question_rx) = QuestionBridge::new();
        let (approval_bridge, approval_rx) = ApprovalBridge::new();
        let permission_context = Arc::new(RwLock::new(InspectionContext {
            workspace_root: effective_cwd.clone(),
            auto_approve: config.yolo,
            session_approved: std::collections::HashSet::new(),
            safety_profiles: core_tool_profiles(),
            persistent_rules: ava_permissions::persistent::PersistentRules::load_merged(
                &effective_cwd,
            ),
            tool_source: None, // Set per-tool-call by middleware
            glob_rules: ava_permissions::glob_rules::GlobRuleset::load_merged(&effective_cwd),
        }));
        let permission_inspector: Arc<dyn PermissionInspector> = Arc::new(DefaultInspector::new(
            PermissionSystem::load(effective_cwd.clone(), vec![]),
            if config.yolo {
                PermissionPolicy::permissive()
            } else {
                PermissionPolicy::standard()
            },
        ));

        let (mut registry, shared_tool_sources) = build_tool_registry(
            platform.clone(),
            Arc::clone(&permission_inspector),
            Arc::clone(&permission_context),
            approval_bridge.clone(),
        );
        register_todo_tools(&mut registry, todo_state.clone());
        register_question_tool(&mut registry, question_bridge.clone());
        register_plan_tool(&mut registry, plan_bridge.clone(), plan_state.clone());
        register_custom_tools(&mut registry, &custom_tool_dirs);

        let mcp_runtime = init_mcp(&mcp_global_config, &mcp_project_config, &mut registry).await;

        // Populate the permission middleware's source map from the fully-built registry
        // so that inspect() receives the correct ToolSource for every tool.
        {
            let mut sources = shared_tool_sources
                .write()
                .unwrap_or_else(|e| e.into_inner());
            for (def, src) in registry.list_tools_with_source() {
                sources.insert(def.name, convert_tool_source(&src));
            }
        }

        let tools = Arc::new(RwLock::new(registry));

        Ok((
            Self {
                router,
                tools,
                session_manager,
                memory,
                config: config_mgr,
                platform,
                codebase_index,
                provider_override: RwLock::new(config.provider),
                model_override: RwLock::new(config.model),
                routing_locked: RwLock::new(routing_locked),
                max_turns: config.max_turns,
                max_budget_usd: config.max_budget_usd,
                yolo: config.yolo,
                injected_provider: config.injected_provider,
                mcp: Arc::new(RwLock::new(mcp_runtime)),
                disabled_mcp_servers: RwLock::new(std::collections::HashSet::new()),
                custom_tool_dirs,
                mcp_global_config,
                mcp_project_config,
                thinking_level: RwLock::new(ThinkingLevel::Off),
                mode_prompt_suffix: RwLock::new(None),
                plan_mode: RwLock::new(false),
                todo_state,
                plan_state,
                plan_bridge,
                question_bridge,
                approval_bridge,
                permission_context,
                permission_inspector,
                shared_tool_sources,
                agents_config,
                compaction_threshold_pct: config.compaction_threshold_pct,
                auto_compact: config.auto_compact,
                parent_session_id: RwLock::new(None),
                plugin_manager,
                index_task: std::sync::Mutex::new(Some(index_handle)),
            },
            question_rx,
            approval_rx,
            plan_rx,
        ))
    }

    // ========================================================================
    // MCP server management
    // ========================================================================

    pub async fn mcp_server_count(&self) -> usize {
        self.mcp.read().await.as_ref().map_or(0, |r| r.server_count)
    }

    pub async fn mcp_tool_count(&self) -> usize {
        self.mcp.read().await.as_ref().map_or(0, |r| r.tool_count)
    }

    pub async fn mcp_server_info(&self) -> Vec<MCPServerInfo> {
        let guard = self.mcp.read().await;
        let Some(runtime) = guard.as_ref() else {
            // Even with no runtime, report servers from config so disabled ones show up.
            return self.mcp_server_info_from_config().await;
        };
        let disabled = self.disabled_mcp_servers.read().await;
        let mut servers: std::collections::HashMap<String, usize> =
            std::collections::HashMap::new();
        for (server_name, _) in &runtime.tools_with_source {
            *servers.entry(server_name.clone()).or_insert(0) += 1;
        }
        let mut result: Vec<MCPServerInfo> = servers
            .into_iter()
            .map(|(name, tool_count)| {
                let scope = runtime
                    .server_scopes
                    .get(&name)
                    .copied()
                    .unwrap_or(McpServerScope::Global);
                let enabled = !disabled.contains(&name);
                MCPServerInfo {
                    name,
                    tool_count,
                    scope,
                    enabled,
                }
            })
            .collect();
        // Also include disabled servers that aren't in the active runtime
        for name in disabled.iter() {
            if !result.iter().any(|s| s.name == *name) {
                let scope = runtime
                    .server_scopes
                    .get(name)
                    .copied()
                    .unwrap_or(McpServerScope::Global);
                result.push(MCPServerInfo {
                    name: name.clone(),
                    tool_count: 0,
                    scope,
                    enabled: false,
                });
            }
        }
        result.sort_by(|a, b| a.name.cmp(&b.name));
        result
    }

    /// Fallback: build server info from config files when no MCP runtime is active.
    async fn mcp_server_info_from_config(&self) -> Vec<MCPServerInfo> {
        let configs =
            load_merged_mcp_config_with_scope(&self.mcp_global_config, &self.mcp_project_config)
                .await
                .unwrap_or_default();
        let disabled = self.disabled_mcp_servers.read().await;
        configs
            .into_iter()
            .map(|(cfg, scope)| MCPServerInfo {
                enabled: cfg.enabled && !disabled.contains(&cfg.name),
                name: cfg.name,
                tool_count: 0,
                scope,
            })
            .collect()
    }

    /// Disable an MCP server by name (session-scoped). Returns true if the server exists.
    pub async fn mcp_disable_server(&self, name: &str) -> bool {
        // Check that the server name is known
        let known = self.mcp_server_exists(name).await;
        if known {
            self.disabled_mcp_servers
                .write()
                .await
                .insert(name.to_string());
            // Remove tools from this server in the registry
            let mut registry = self.tools.write().await;
            registry.remove_by_source(
                |src| matches!(src, ToolSource::MCP { server } if server == name),
            );
        }
        known
    }

    /// Enable a previously disabled MCP server. Returns true if it was disabled.
    /// Triggers a selective reload to re-register tools from this server.
    pub async fn mcp_enable_server(&self, name: &str) -> bool {
        let was_disabled = self.disabled_mcp_servers.write().await.remove(name);
        if was_disabled {
            // Re-register tools from this server by reloading MCP
            let _ = self.reload_mcp().await;
        }
        was_disabled
    }

    /// Check if a server name exists in config or runtime.
    async fn mcp_server_exists(&self, name: &str) -> bool {
        // Check runtime first
        let guard = self.mcp.read().await;
        if let Some(runtime) = guard.as_ref() {
            if runtime.server_scopes.contains_key(name) {
                return true;
            }
        }
        drop(guard);
        // Check config files
        let configs =
            load_merged_mcp_config_with_scope(&self.mcp_global_config, &self.mcp_project_config)
                .await
                .unwrap_or_default();
        configs.iter().any(|(cfg, _)| cfg.name == name)
    }

    // ========================================================================
    // Tool reloading
    // ========================================================================

    pub async fn reload_mcp(&self) -> Result<(usize, usize)> {
        let disabled = self.disabled_mcp_servers.read().await.clone();
        let mut registry = self.tools.write().await;
        registry.remove_by_source(|src| matches!(src, ToolSource::MCP { .. }));
        let runtime = init_mcp_with_disabled(
            &self.mcp_global_config,
            &self.mcp_project_config,
            &mut registry,
            &disabled,
        )
        .await;
        let counts = runtime
            .as_ref()
            .map_or((0, 0), |r| (r.server_count, r.tool_count));
        *self.mcp.write().await = runtime;
        // Refresh shared_tool_sources so the permission middleware sees new MCP tools.
        {
            let mut sources = self
                .shared_tool_sources
                .write()
                .unwrap_or_else(|e| e.into_inner());
            // Remove stale MCP entries, then re-insert from the updated registry.
            sources.retain(|_, src| {
                !matches!(src, ava_permissions::inspector::ToolSource::MCP { .. })
            });
            for (def, src) in registry.list_tools_with_source() {
                if matches!(src, ToolSource::MCP { .. }) {
                    sources.insert(def.name, convert_tool_source(&src));
                }
            }
        }
        Ok(counts)
    }

    pub async fn reload_custom_tools(&self) -> usize {
        let mut registry = self.tools.write().await;
        registry.remove_by_source(|src| matches!(src, ToolSource::Custom { .. }));
        register_custom_tools(&mut registry, &self.custom_tool_dirs);
        registry
            .list_tools_with_source()
            .iter()
            .filter(|(_, src)| matches!(src, ToolSource::Custom { .. }))
            .count()
    }

    pub async fn reload_tools(&self) -> Result<usize> {
        let (mut registry, reload_sources) = build_tool_registry(
            self.platform.clone(),
            Arc::clone(&self.permission_inspector),
            Arc::clone(&self.permission_context),
            self.approval_bridge.clone(),
        );
        register_todo_tools(&mut registry, self.todo_state.clone());
        register_question_tool(&mut registry, self.question_bridge.clone());
        register_plan_tool(
            &mut registry,
            self.plan_bridge.clone(),
            self.plan_state.clone(),
        );
        register_custom_tools(&mut registry, &self.custom_tool_dirs);
        let disabled = self.disabled_mcp_servers.read().await.clone();
        let runtime = init_mcp_with_disabled(
            &self.mcp_global_config,
            &self.mcp_project_config,
            &mut registry,
            &disabled,
        )
        .await;
        // Populate tool sources for the permission middleware.
        {
            let mut sources = reload_sources.write().unwrap_or_else(|e| e.into_inner());
            for (def, src) in registry.list_tools_with_source() {
                sources.insert(def.name, convert_tool_source(&src));
            }
        }
        let count = registry.tool_count();
        *self.tools.write().await = registry;
        *self.mcp.write().await = runtime;
        Ok(count)
    }

    // ========================================================================
    // Codebase index health
    // ========================================================================

    /// Check whether the background codebase indexing task completed without panicking.
    ///
    /// - If the task is still running, returns immediately (no-op).
    /// - If the task finished successfully, clears the stored handle.
    /// - If the task panicked, logs an `error!` and clears the handle so the
    ///   panic is surfaced exactly once (subsequent calls are no-ops).
    pub async fn check_index_status(&self) {
        // Take the handle only if it has already finished, to avoid blocking.
        let handle = {
            let mut guard = self.index_task.lock().unwrap_or_else(|e| e.into_inner());
            match guard.as_ref() {
                Some(h) if h.is_finished() => guard.take(),
                _ => None, // still running or already consumed
            }
        };
        if let Some(handle) = handle {
            match handle.await {
                Ok(()) => {} // task logged its own errors/warnings
                Err(join_err) if join_err.is_panic() => {
                    error!(
                        "Codebase indexing task panicked — \
                         codebase_search will return empty results until the next restart"
                    );
                }
                Err(join_err) => {
                    warn!("Codebase indexing task was cancelled: {join_err}");
                }
            }
        }
    }

    // ========================================================================
    // Model and mode switching
    // ========================================================================

    #[instrument(skip(self))]
    pub async fn switch_model(&self, provider: &str, model: &str) -> Result<()> {
        self.router.route_required(provider, model).await?;
        *self.provider_override.write().await = Some(provider.to_string());
        *self.model_override.write().await = Some(model.to_string());
        *self.routing_locked.write().await = true;
        let project_root = std::env::current_dir().unwrap_or_default();
        let mut state = ava_config::ProjectState::load(&project_root);
        state.last_provider = Some(provider.to_string());
        state.last_model = Some(model.to_string());
        let _ = state.save(&project_root);
        Ok(())
    }

    pub async fn set_mode_prompt_suffix(&self, suffix: Option<String>) -> Result<()> {
        *self.mode_prompt_suffix.write().await = suffix;
        Ok(())
    }

    pub async fn set_plan_mode(&self, enabled: bool) -> Result<()> {
        *self.plan_mode.write().await = enabled;
        Ok(())
    }

    pub async fn set_thinking_level(&self, level: ThinkingLevel) -> Result<()> {
        *self.thinking_level.write().await = level;
        Ok(())
    }

    pub async fn cycle_thinking(&self) -> &'static str {
        let mut guard = self.thinking_level.write().await;
        *guard = guard.cycle();
        guard.label()
    }

    pub async fn get_thinking_level(&self) -> ThinkingLevel {
        *self.thinking_level.read().await
    }

    /// Returns `true` if the agent is in auto-approve (yolo) mode.
    pub async fn is_auto_approve(&self) -> bool {
        self.permission_context.read().await.auto_approve
    }

    /// Set the auto-approve flag on the live permission context.
    pub async fn set_auto_approve(&self, auto_approve: bool) {
        self.permission_context.write().await.auto_approve = auto_approve;
    }

    /// Create a message queue for mid-stream messaging.
    /// Returns the queue (to pass into `run()`) and the sender (for the TUI to send messages).
    pub fn create_message_queue(
        &self,
    ) -> (
        crate::message_queue::MessageQueue,
        mpsc::UnboundedSender<QueuedMessage>,
    ) {
        crate::message_queue::MessageQueue::new()
    }

    pub async fn current_model(&self) -> (String, String) {
        let cfg = self.config.get().await;
        let provider = self
            .provider_override
            .read()
            .await
            .clone()
            .unwrap_or_else(|| cfg.llm.provider.clone());
        let model = self
            .model_override
            .read()
            .await
            .clone()
            .unwrap_or_else(|| cfg.llm.model.clone());
        (provider, model)
    }
}

const _: () = {
    fn _assert_send<T: Send>() {}
    fn _check() {
        _assert_send::<AgentStack>();
    }
};

#[cfg(test)]
mod tests {
    use super::*;
    use stack_run::parse_model_spec;

    #[test]
    fn workspace_root_resolution_keeps_cwd_and_valid_roots() {
        let cwd = std::env::current_dir().expect("cwd");
        let temp = tempfile::tempdir().expect("temp");
        let missing = temp.path().join("missing");

        let configured = vec![
            temp.path().to_string_lossy().to_string(),
            missing.to_string_lossy().to_string(),
        ];
        let roots = resolve_workspace_roots(&cwd, &configured);

        assert!(roots.contains(&cwd));
        assert!(roots.contains(&temp.path().to_path_buf()));
        assert!(!roots.contains(&missing));
    }

    #[test]
    fn parse_model_spec_provider_slash_model() {
        let (provider, model) = parse_model_spec("anthropic/claude-sonnet-4");
        assert_eq!(provider, "anthropic");
        assert_eq!(model, "claude-sonnet-4");
    }

    #[test]
    fn parse_model_spec_openrouter_with_org() {
        // OpenRouter models have org/model format after the provider prefix
        let (provider, model) = parse_model_spec("openrouter/google/gemini-flash-1.5");
        assert_eq!(provider, "openrouter");
        assert_eq!(model, "google/gemini-flash-1.5");
    }

    #[test]
    fn parse_model_spec_bare_model_in_registry() {
        // A bare model name that exists in the registry should resolve to
        // the correct provider and canonical ID.
        let (provider, model) = parse_model_spec("claude-sonnet-4");
        assert_eq!(provider, "anthropic");
        // The registry resolves aliases to canonical IDs
        assert!(model.starts_with("claude-sonnet-4"));
    }

    #[test]
    fn parse_model_spec_bare_model_unknown() {
        // A model name not in the registry falls back to openrouter
        let (provider, model) = parse_model_spec("some-unknown-model-xyz");
        assert_eq!(provider, "openrouter");
        assert_eq!(model, "some-unknown-model-xyz");
    }

    #[test]
    fn parse_model_spec_gemini_provider() {
        let (provider, model) = parse_model_spec("gemini/gemini-2.5-pro");
        assert_eq!(provider, "gemini");
        assert_eq!(model, "gemini-2.5-pro");
    }

    #[test]
    fn parse_model_spec_ollama() {
        let (provider, model) = parse_model_spec("ollama/llama3.3");
        assert_eq!(provider, "ollama");
        assert_eq!(model, "llama3.3");
    }

    #[test]
    fn parse_model_spec_cli_provider() {
        let (provider, model) = parse_model_spec("cli:claude-code/sonnet");
        assert_eq!(provider, "cli:claude-code");
        assert_eq!(model, "sonnet");
    }

    #[test]
    fn parse_model_spec_alias_lookup() {
        // "sonnet" is an alias for claude-sonnet-4 in the registry
        let (provider, model) = parse_model_spec("sonnet");
        assert_eq!(provider, "anthropic");
        // Should resolve to the canonical model ID
        assert!(model.contains("sonnet"));
    }

    #[test]
    fn agents_config_model_override_resolves() {
        // Test that AgentsConfig properly returns model overrides
        let tmp = tempfile::TempDir::new().unwrap();
        let global = tmp.path().join("global.toml");
        std::fs::write(
            &global,
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"

[agents.plan]
model = "anthropic/claude-sonnet-4"

[agents.explore]
model = "openrouter/google/gemini-flash-1.5"

[agents.task]
max_turns = 10
"#,
        )
        .unwrap();
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);

        // Plan agent should have its own model
        let plan = config.get_agent("plan");
        assert_eq!(plan.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        let (prov, mdl) = parse_model_spec(plan.model.as_deref().unwrap());
        assert_eq!(prov, "anthropic");
        assert_eq!(mdl, "claude-sonnet-4");

        // Explore agent should have its own model
        let explore = config.get_agent("explore");
        assert_eq!(
            explore.model.as_deref(),
            Some("openrouter/google/gemini-flash-1.5")
        );
        let (prov, mdl) = parse_model_spec(explore.model.as_deref().unwrap());
        assert_eq!(prov, "openrouter");
        assert_eq!(mdl, "google/gemini-flash-1.5");

        // Task agent should inherit default model
        let task = config.get_agent("task");
        assert_eq!(task.model.as_deref(), Some("anthropic/claude-haiku-4.5"));
        let (prov, mdl) = parse_model_spec(task.model.as_deref().unwrap());
        assert_eq!(prov, "anthropic");
        assert_eq!(mdl, "claude-haiku-4.5");

        // Agent without model override and no defaults.model -> None
        let config_empty = AgentsConfig::default();
        let unknown = config_empty.get_agent("unknown");
        assert!(unknown.model.is_none());
    }
}
