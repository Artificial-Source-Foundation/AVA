//! MCP runtime lifecycle management for the shared agent stack.

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::Arc;

use async_trait::async_trait;
use ava_mcp::config::{load_merged_mcp_config_with_scope, McpServerScope};
use ava_mcp::manager::{McpManager, McpServerInitStatus};
use ava_tools::mcp_bridge::{MCPBridgeTool, MCPToolCaller};
use ava_tools::permission_middleware::{convert_tool_source, SharedToolSources};
use ava_tools::registry::{ToolRegistry, ToolSource};
use ava_types::{Result, ToolResult};
use serde_json::Value;
use tokio::sync::{Mutex, Notify, RwLock};
use tracing::{info, warn};

pub(crate) struct McpManagerCaller {
    pub(crate) manager: McpManager,
}

#[async_trait]
impl MCPToolCaller for McpManagerCaller {
    async fn call_tool(&self, name: &str, arguments: Value) -> Result<ToolResult> {
        self.manager.call_tool(name, arguments).await
    }
}

pub(crate) struct MCPRuntime {
    pub(crate) caller: Arc<dyn MCPToolCaller>,
    pub(crate) server_count: usize,
    pub(crate) tool_count: usize,
    pub(crate) tools_with_source: Vec<(String, ava_types::Tool)>,
    /// Scope (global vs local) for each server name.
    pub(crate) server_scopes: HashMap<String, McpServerScope>,
}

/// Connection status of an individual MCP server.
#[derive(Debug, Clone, PartialEq)]
pub enum McpServerStatus {
    /// Tools are available and the server is responding.
    Connected,
    /// Server was explicitly disabled (`enabled: false` in config or via `/mcp disable`).
    Disabled,
    /// The connection or `list_tools` call failed.
    Failed(String),
    /// Connection is in progress (shown while lazy init is running).
    Connecting,
}

#[derive(Debug, Clone)]
pub struct MCPServerInfo {
    pub name: String,
    pub tool_count: usize,
    pub scope: McpServerScope,
    pub enabled: bool,
    /// Whether the current UI toggle action is supported by the backend.
    pub can_toggle: bool,
    /// Current connection status for UI display.
    pub status: McpServerStatus,
}

#[derive(Default)]
pub(crate) struct MCPInitResult {
    pub(crate) runtime: Option<MCPRuntime>,
    pub(crate) statuses: HashMap<String, McpServerStatus>,
}

pub(crate) async fn init_mcp_with_disabled(
    global_config: &Path,
    project_config: &Path,
    registry: &mut ToolRegistry,
    disabled: &HashSet<String>,
) -> MCPInitResult {
    match load_merged_mcp_config_with_scope(global_config, project_config).await {
        Ok(configs_with_scope) if !configs_with_scope.is_empty() => {
            // Build scope map from all configs (before filtering)
            let server_scopes: HashMap<String, McpServerScope> = configs_with_scope
                .iter()
                .map(|(cfg, scope)| (cfg.name.clone(), *scope))
                .collect();

            // Filter out disabled servers
            let configs: Vec<_> = configs_with_scope
                .into_iter()
                .filter(|(cfg, _)| !disabled.contains(&cfg.name))
                .map(|(cfg, _)| cfg)
                .collect();

            if configs.is_empty() {
                // All servers disabled — return runtime with scope info but no tools
                return MCPInitResult {
                    runtime: Some(MCPRuntime {
                        caller: Arc::new(McpManagerCaller {
                            manager: McpManager::new(),
                        }),
                        server_count: 0,
                        tool_count: 0,
                        tools_with_source: Vec::new(),
                        server_scopes,
                    }),
                    statuses: HashMap::new(),
                };
            }

            let mut manager = McpManager::new();
            let statuses = manager
                .initialize_with_report(configs)
                .await
                .into_iter()
                .map(|report| {
                    let status = match report.status {
                        McpServerInitStatus::Connected => McpServerStatus::Connected,
                        McpServerInitStatus::Failed(error) => McpServerStatus::Failed(error),
                    };
                    (report.name, status)
                })
                .collect();
            let server_count = manager.server_count();
            let mcp_tools_with_server = manager.list_tools_with_server().to_vec();
            let mcp_tools = manager.list_tools();
            let caller: Arc<dyn MCPToolCaller> = Arc::new(McpManagerCaller { manager });
            let mut tools_with_source = Vec::new();
            for (server_name, mcp_tool) in &mcp_tools_with_server {
                if let Some(tool_def) = mcp_tools.iter().find(|t| t.name == mcp_tool.name) {
                    tools_with_source.push((server_name.clone(), tool_def.clone()));
                }
            }
            let tool_count = tools_with_source.len();
            for (server_name, tool_def) in &tools_with_source {
                info!(tool = %tool_def.name, server = %server_name, "Registering MCP tool");
                let source = ToolSource::MCP {
                    server: server_name.clone(),
                };
                registry.register_with_source(
                    MCPBridgeTool::new(tool_def.clone(), caller.clone(), server_name),
                    source,
                );
            }
            info!(
                servers = server_count,
                tools = tool_count,
                "MCP initialized"
            );
            MCPInitResult {
                runtime: Some(MCPRuntime {
                    caller,
                    server_count,
                    tool_count,
                    tools_with_source,
                    server_scopes,
                }),
                statuses,
            }
        }
        Ok(_) => MCPInitResult::default(),
        Err(e) => {
            warn!(error = %e, "Failed to load MCP config, continuing without MCP tools");
            MCPInitResult::default()
        }
    }
}

pub(super) struct AgentMcpRuntime {
    runtime: RwLock<Option<MCPRuntime>>,
    /// Session-scoped set of disabled MCP server names.
    disabled_servers: RwLock<HashSet<String>>,
    /// Last known connection status for configured MCP servers.
    server_statuses: RwLock<HashMap<String, McpServerStatus>>,
    global_config: PathBuf,
    project_config: PathBuf,
    /// Tracks lazy MCP initialization so concurrent callers can await the same in-flight work.
    init_state: Mutex<McpInitState>,
}

enum McpInitState {
    NotStarted,
    Running(Arc<Notify>),
    Done,
}

impl AgentMcpRuntime {
    pub(super) fn new(global_config: PathBuf, project_config: PathBuf) -> Self {
        Self {
            runtime: RwLock::new(None),
            disabled_servers: RwLock::new(HashSet::new()),
            server_statuses: RwLock::new(HashMap::new()),
            global_config,
            project_config,
            init_state: Mutex::new(McpInitState::NotStarted),
        }
    }

    pub(super) fn has_config_files(&self) -> bool {
        self.global_config.exists() || self.project_config.exists()
    }

    pub(super) async fn register_enabled_runtime_tools(&self, registry: &mut ToolRegistry) {
        let disabled = self.disabled_servers.read().await.clone();
        let runtime_guard = self.runtime.read().await;

        if let Some(runtime) = runtime_guard.as_ref() {
            register_runtime_mcp_tools(registry, runtime, &disabled);
        }
    }

    /// Ensure MCP is initialized, awaiting completion before returning.
    ///
    /// This is the lazy-init entry point. A small mutex-protected state machine
    /// ensures only the first concurrent caller actually runs init; subsequent
    /// callers await the same in-flight initialization until it completes.
    ///
    /// The init runs **inline** (not in a background task) so that MCP tools
    /// are guaranteed to be registered in the `ToolRegistry` by the time this
    /// returns — fixing the race condition where `run()` would read an empty
    /// runtime because initialization had not yet completed.
    pub(super) async fn ensure_initialized(
        &self,
        tools: &Arc<RwLock<ToolRegistry>>,
        shared_tool_sources: &SharedToolSources,
    ) {
        loop {
            let notify = {
                let mut init_state = self.init_state.lock().await;
                match &*init_state {
                    McpInitState::Done => return,
                    McpInitState::Running(notify) => Some(notify.clone()),
                    McpInitState::NotStarted => {
                        let notify = Arc::new(Notify::new());
                        *init_state = McpInitState::Running(notify.clone());
                        None
                    }
                }
            };

            if let Some(notify) = notify {
                notify.notified().await;
                continue;
            }

            info!("MCP init: connecting servers…");

            let disabled = self.disabled_snapshot().await;
            let mut registry = tools.write().await;
            let init = self
                .build_init_result_with_disabled(&mut registry, &disabled)
                .await;
            self.refresh_shared_tool_sources(shared_tool_sources, &registry, false);
            // Drop the registry write-lock before acquiring runtime/status write-locks.
            drop(registry);

            let counts = self.store_init_result(init).await;
            let notify = {
                let mut init_state = self.init_state.lock().await;
                match &*init_state {
                    McpInitState::Running(notify) => {
                        let notify = notify.clone();
                        *init_state = McpInitState::Done;
                        notify
                    }
                    McpInitState::NotStarted | McpInitState::Done => unreachable!(
                        "MCP init state should still be running while completing initialization"
                    ),
                }
            };
            notify.notify_waiters();
            info!(
                servers = counts.0,
                tools = counts.1,
                "MCP init complete — tools now available"
            );
            return;
        }
    }

    pub(super) async fn server_count(&self) -> usize {
        self.runtime
            .read()
            .await
            .as_ref()
            .map_or(0, |runtime| runtime.server_count)
    }

    pub(super) async fn tool_count(&self) -> usize {
        self.runtime
            .read()
            .await
            .as_ref()
            .map_or(0, |runtime| runtime.tool_count)
    }

    pub(super) async fn server_info(&self) -> Vec<MCPServerInfo> {
        let config_entries = self.load_config_entries().await;
        let disabled = self.disabled_servers.read().await.clone();
        let server_statuses = self.server_statuses.read().await.clone();
        let runtime = self.runtime.read().await;
        build_mcp_server_info(
            config_entries,
            runtime.as_ref(),
            &server_statuses,
            &disabled,
        )
    }

    /// Disable an MCP server by name (session-scoped). Returns true if the server exists.
    pub(super) async fn disable_server(
        &self,
        name: &str,
        tools: &Arc<RwLock<ToolRegistry>>,
    ) -> bool {
        let known = self.server_exists(name).await;
        if known {
            self.disabled_servers.write().await.insert(name.to_string());
            let mut registry = tools.write().await;
            registry.remove_by_source(
                |src| matches!(src, ToolSource::MCP { server } if server == name),
            );
        }
        known
    }

    /// Enable a previously disabled MCP server. Returns true if it was disabled.
    /// Triggers a selective reload to re-register tools from this server.
    pub(super) async fn enable_server(
        &self,
        name: &str,
        tools: &Arc<RwLock<ToolRegistry>>,
        shared_tool_sources: &SharedToolSources,
    ) -> bool {
        let was_disabled = self.disabled_servers.write().await.remove(name);
        if was_disabled {
            let _ = self.reload(tools, shared_tool_sources).await;
        }
        was_disabled
    }

    pub(super) async fn reload(
        &self,
        tools: &Arc<RwLock<ToolRegistry>>,
        shared_tool_sources: &SharedToolSources,
    ) -> Result<(usize, usize)> {
        let disabled = self.disabled_snapshot().await;
        let mut registry = tools.write().await;
        registry.remove_by_source(|src| matches!(src, ToolSource::MCP { .. }));
        let init = self
            .build_init_result_with_disabled(&mut registry, &disabled)
            .await;
        self.refresh_shared_tool_sources(shared_tool_sources, &registry, true);
        drop(registry);
        Ok(self.store_init_result(init).await)
    }

    pub(super) async fn populate_registry(&self, registry: &mut ToolRegistry) -> (usize, usize) {
        let init = self.build_init_result(registry).await;
        self.store_init_result(init).await
    }

    async fn build_init_result(&self, registry: &mut ToolRegistry) -> MCPInitResult {
        let disabled = self.disabled_snapshot().await;
        self.build_init_result_with_disabled(registry, &disabled)
            .await
    }

    async fn build_init_result_with_disabled(
        &self,
        registry: &mut ToolRegistry,
        disabled: &HashSet<String>,
    ) -> MCPInitResult {
        init_mcp_with_disabled(
            &self.global_config,
            &self.project_config,
            registry,
            disabled,
        )
        .await
    }

    async fn disabled_snapshot(&self) -> HashSet<String> {
        self.disabled_servers.read().await.clone()
    }

    async fn store_init_result(&self, init: MCPInitResult) -> (usize, usize) {
        let counts = init
            .runtime
            .as_ref()
            .map_or((0, 0), |runtime| (runtime.server_count, runtime.tool_count));
        *self.runtime.write().await = init.runtime;
        *self.server_statuses.write().await = init.statuses;
        counts
    }

    fn refresh_shared_tool_sources(
        &self,
        shared_tool_sources: &SharedToolSources,
        registry: &ToolRegistry,
        replace_existing: bool,
    ) {
        // infallible: RwLock poisoning is recovered by taking the inner value
        let mut sources = shared_tool_sources
            .write()
            .unwrap_or_else(|error| error.into_inner());
        if replace_existing {
            sources.retain(|_, src| {
                !matches!(src, ava_permissions::inspector::ToolSource::MCP { .. })
            });
        }
        for (def, src) in registry.list_tools_with_source() {
            if matches!(src, ToolSource::MCP { .. }) {
                sources.insert(def.name, convert_tool_source(&src));
            }
        }
    }

    async fn server_exists(&self, name: &str) -> bool {
        let runtime = self.runtime.read().await;
        if let Some(runtime) = runtime.as_ref() {
            if runtime.server_scopes.contains_key(name) {
                return true;
            }
        }
        drop(runtime);

        self.load_config_entries()
            .await
            .iter()
            .any(|(cfg, _)| cfg.name == name)
    }

    async fn load_config_entries(&self) -> Vec<(ava_mcp::config::MCPServerConfig, McpServerScope)> {
        load_merged_mcp_config_with_scope(&self.global_config, &self.project_config)
            .await
            .unwrap_or_default()
    }

    #[cfg(test)]
    pub(super) async fn set_runtime_for_tests(&self, runtime: Option<MCPRuntime>) {
        *self.runtime.write().await = runtime;
    }

    #[cfg(test)]
    pub(super) async fn init_done_for_tests(&self) -> bool {
        let guard = self.init_state.lock().await;
        matches!(&*guard, McpInitState::Done)
    }

    #[cfg(test)]
    pub(super) async fn init_running_for_tests(&self) -> bool {
        let guard = self.init_state.lock().await;
        matches!(&*guard, McpInitState::Running(_))
    }

    #[cfg(test)]
    pub(super) async fn runtime_is_none_for_tests(&self) -> bool {
        self.runtime.read().await.is_none()
    }

    #[cfg(test)]
    pub(super) async fn server_statuses_for_tests(&self) -> HashMap<String, McpServerStatus> {
        self.server_statuses.read().await.clone()
    }
}

fn build_mcp_server_info(
    config_entries: Vec<(ava_mcp::config::MCPServerConfig, McpServerScope)>,
    runtime: Option<&MCPRuntime>,
    server_statuses: &HashMap<String, McpServerStatus>,
    disabled: &HashSet<String>,
) -> Vec<MCPServerInfo> {
    let tool_counts: HashMap<String, usize> = runtime
        .map(|runtime| {
            runtime
                .tools_with_source
                .iter()
                .fold(HashMap::new(), |mut acc, (server_name, _)| {
                    *acc.entry(server_name.clone()).or_insert(0) += 1;
                    acc
                })
        })
        .unwrap_or_default();

    let mut result = Vec::with_capacity(config_entries.len());
    let mut seen = HashSet::new();

    for (cfg, scope) in config_entries {
        let name = cfg.name;
        let session_disabled = disabled.contains(&name);
        let enabled = cfg.enabled && !session_disabled;
        let status = if enabled {
            server_statuses
                .get(&name)
                .cloned()
                .unwrap_or(McpServerStatus::Connecting)
        } else {
            McpServerStatus::Disabled
        };

        result.push(MCPServerInfo {
            tool_count: tool_counts.get(&name).copied().unwrap_or(0),
            scope,
            enabled,
            can_toggle: cfg.enabled,
            status,
            name: name.clone(),
        });
        seen.insert(name);
    }

    if let Some(runtime) = runtime {
        for name in disabled {
            if seen.contains(name) {
                continue;
            }
            result.push(MCPServerInfo {
                name: name.clone(),
                tool_count: tool_counts.get(name).copied().unwrap_or(0),
                scope: runtime
                    .server_scopes
                    .get(name)
                    .copied()
                    .unwrap_or(McpServerScope::Global),
                enabled: false,
                can_toggle: true,
                status: McpServerStatus::Disabled,
            });
            seen.insert(name.clone());
        }

        for (name, status) in server_statuses {
            if seen.contains(name) {
                continue;
            }
            result.push(MCPServerInfo {
                name: name.clone(),
                tool_count: tool_counts.get(name).copied().unwrap_or(0),
                scope: runtime
                    .server_scopes
                    .get(name)
                    .copied()
                    .unwrap_or(McpServerScope::Global),
                enabled: !disabled.contains(name),
                can_toggle: true,
                status: if disabled.contains(name) {
                    McpServerStatus::Disabled
                } else {
                    status.clone()
                },
            });
        }
    }

    result.sort_by(|a, b| a.name.cmp(&b.name));
    result
}

fn register_runtime_mcp_tools(
    registry: &mut ToolRegistry,
    runtime: &MCPRuntime,
    disabled: &HashSet<String>,
) {
    for (server_name, tool_def) in &runtime.tools_with_source {
        if disabled.contains(server_name) {
            continue;
        }

        let source = ToolSource::MCP {
            server: server_name.clone(),
        };
        registry.register_with_source(
            MCPBridgeTool::new(tool_def.clone(), runtime.caller.clone(), server_name),
            source,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use ava_mcp::config::{MCPServerConfig, TransportType};
    use serde_json::json;
    use tokio::time::{timeout, Duration};

    fn stdio_config(name: &str, enabled: bool) -> MCPServerConfig {
        MCPServerConfig {
            name: name.to_string(),
            transport: TransportType::Stdio {
                command: "true".to_string(),
                args: Vec::new(),
                env: HashMap::new(),
            },
            enabled,
        }
    }

    fn empty_runtime(name: &str, scope: McpServerScope) -> MCPRuntime {
        MCPRuntime {
            caller: Arc::new(McpManagerCaller {
                manager: McpManager::new(),
            }),
            server_count: 1,
            tool_count: 0,
            tools_with_source: Vec::new(),
            server_scopes: HashMap::from([(name.to_string(), scope)]),
        }
    }

    fn test_tool(name: &str) -> ava_types::Tool {
        ava_types::Tool {
            name: name.to_string(),
            description: format!("{name} description"),
            parameters: json!({}),
        }
    }

    fn namespaced_mcp_tool_name(server_name: &str, tool_name: &str) -> String {
        format!("mcp_{server_name}_{tool_name}")
    }

    fn empty_shared_tool_sources() -> SharedToolSources {
        Arc::new(std::sync::RwLock::new(HashMap::new()))
    }

    #[test]
    fn build_mcp_server_info_keeps_connected_zero_tool_servers_visible() {
        let config_entries = vec![(stdio_config("zero-tools", true), McpServerScope::Local)];
        let runtime = empty_runtime("zero-tools", McpServerScope::Local);
        let statuses = HashMap::from([("zero-tools".to_string(), McpServerStatus::Connected)]);

        let info =
            build_mcp_server_info(config_entries, Some(&runtime), &statuses, &HashSet::new());

        assert_eq!(info.len(), 1);
        assert_eq!(info[0].name, "zero-tools");
        assert_eq!(info[0].tool_count, 0);
        assert!(info[0].enabled);
        assert_eq!(info[0].status, McpServerStatus::Connected);
    }

    #[test]
    fn build_mcp_server_info_preserves_failed_status_without_runtime_tools() {
        let config_entries = vec![(stdio_config("broken-server", true), McpServerScope::Global)];
        let statuses = HashMap::from([(
            "broken-server".to_string(),
            McpServerStatus::Failed("spawn failed: missing binary".to_string()),
        )]);

        let info = build_mcp_server_info(config_entries, None, &statuses, &HashSet::new());

        assert_eq!(info.len(), 1);
        assert_eq!(info[0].name, "broken-server");
        assert!(matches!(
            &info[0].status,
            McpServerStatus::Failed(error) if error == "spawn failed: missing binary"
        ));
    }

    #[test]
    fn register_runtime_mcp_tools_skips_session_disabled_servers() {
        let mut registry = ToolRegistry::new();
        let runtime = MCPRuntime {
            caller: Arc::new(McpManagerCaller {
                manager: McpManager::new(),
            }),
            server_count: 2,
            tool_count: 2,
            tools_with_source: vec![
                ("enabled-server".to_string(), test_tool("enabled_tool")),
                ("disabled-server".to_string(), test_tool("disabled_tool")),
            ],
            server_scopes: HashMap::from([
                ("enabled-server".to_string(), McpServerScope::Global),
                ("disabled-server".to_string(), McpServerScope::Global),
            ]),
        };
        let disabled = HashSet::from(["disabled-server".to_string()]);

        register_runtime_mcp_tools(&mut registry, &runtime, &disabled);

        let names = registry
            .list_tools_with_source()
            .into_iter()
            .map(|(tool, _)| tool.name)
            .collect::<Vec<_>>();
        let enabled_name = namespaced_mcp_tool_name("enabled-server", "enabled_tool");
        let disabled_name = namespaced_mcp_tool_name("disabled-server", "disabled_tool");
        assert!(names.iter().any(|name| name == &enabled_name));
        assert!(!names.iter().any(|name| name == &disabled_name));
    }

    #[tokio::test]
    async fn ensure_initialized_waits_for_in_flight_initialization() {
        let temp = tempfile::tempdir().expect("temp");
        let runtime = Arc::new(AgentMcpRuntime::new(
            temp.path().join("global-missing.json"),
            temp.path().join("project-missing.json"),
        ));
        let tools = Arc::new(RwLock::new(ToolRegistry::new()));
        let shared_tool_sources = empty_shared_tool_sources();

        let registry_guard = tools.write().await;

        let first_runtime = Arc::clone(&runtime);
        let first_tools = Arc::clone(&tools);
        let first_sources = shared_tool_sources.clone();
        let first = tokio::spawn(async move {
            first_runtime
                .ensure_initialized(&first_tools, &first_sources)
                .await;
        });

        timeout(Duration::from_millis(200), async {
            while !runtime.init_running_for_tests().await {
                tokio::task::yield_now().await;
            }
        })
        .await
        .expect("first caller should mark MCP init as running");

        let second_runtime = Arc::clone(&runtime);
        let second_tools = Arc::clone(&tools);
        let second_sources = shared_tool_sources.clone();
        let second = tokio::spawn(async move {
            second_runtime
                .ensure_initialized(&second_tools, &second_sources)
                .await;
        });

        tokio::time::sleep(Duration::from_millis(25)).await;
        assert!(
            !first.is_finished(),
            "first caller should still be waiting on the registry lock"
        );
        assert!(
            !second.is_finished(),
            "second caller should wait for the same in-flight initialization"
        );

        drop(registry_guard);

        first.await.expect("first init task should complete");
        second.await.expect("second init task should complete");
        assert!(runtime.init_done_for_tests().await);
    }

    #[tokio::test]
    async fn reload_snapshots_disabled_servers_before_waiting_on_registry_lock() {
        let temp = tempfile::tempdir().expect("temp");
        let runtime = Arc::new(AgentMcpRuntime::new(
            temp.path().join("global-missing.json"),
            temp.path().join("project-missing.json"),
        ));
        let tools = Arc::new(RwLock::new(ToolRegistry::new()));
        let shared_tool_sources = empty_shared_tool_sources();

        let disabled_guard = runtime.disabled_servers.write().await;

        let reload_runtime = Arc::clone(&runtime);
        let reload_tools = Arc::clone(&tools);
        let reload_sources = shared_tool_sources.clone();
        let reload =
            tokio::spawn(
                async move { reload_runtime.reload(&reload_tools, &reload_sources).await },
            );

        let probe_tools = Arc::clone(&tools);
        timeout(Duration::from_millis(200), async move {
            let _guard = probe_tools.write().await;
        })
        .await
        .expect(
            "reload should not hold the registry lock while blocked on disabled-server snapshot",
        );

        drop(disabled_guard);

        reload
            .await
            .expect("reload task should join")
            .expect("reload should succeed once the disabled snapshot is available");
    }
}
