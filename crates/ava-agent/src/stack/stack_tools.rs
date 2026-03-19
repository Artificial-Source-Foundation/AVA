//! Tool registry construction, MCP initialization, and workspace resolution.

use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use async_trait::async_trait;
use ava_llm::provider::LLMProvider;
use ava_mcp::config::{load_merged_mcp_config_with_scope, McpServerScope};
use ava_mcp::manager::ExtensionManager;
use ava_permissions::inspector::PermissionInspector;
use ava_platform::Platform;
use ava_tools::mcp_bridge::{MCPBridgeTool, MCPToolCaller};
use ava_tools::permission_middleware::{ApprovalBridge, PermissionMiddleware, SharedToolSources};
use ava_tools::registry::{ToolRegistry, ToolSource};
use ava_types::{Result, ToolResult};
use serde_json::Value;
use tokio::sync::RwLock;
use tracing::{info, warn};

use ava_permissions::inspector::InspectionContext;
use ava_tools::core::{register_default_tools, register_extended_tools};

pub(crate) struct ExtensionManagerCaller {
    pub(crate) manager: ExtensionManager,
}

#[async_trait]
impl MCPToolCaller for ExtensionManagerCaller {
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

#[derive(Debug, Clone)]
pub struct MCPServerInfo {
    pub name: String,
    pub tool_count: usize,
    pub scope: McpServerScope,
    pub enabled: bool,
}

/// Summarizer adapter for the LLM provider (used by context compaction).
pub(crate) struct LlmSummarizer(pub(crate) Arc<dyn LLMProvider>);

#[async_trait]
impl ava_context::Summarizer for LlmSummarizer {
    async fn summarize(&self, text: &str) -> std::result::Result<String, String> {
        use ava_types::{Message, Role};
        let messages = vec![Message::new(Role::User, text.to_string())];
        self.0.generate(&messages).await.map_err(|e| e.to_string())
    }
}

pub(crate) async fn init_mcp(
    global_config: &std::path::Path,
    project_config: &std::path::Path,
    registry: &mut ToolRegistry,
) -> Option<MCPRuntime> {
    init_mcp_with_disabled(global_config, project_config, registry, &HashSet::new()).await
}

pub(crate) async fn init_mcp_with_disabled(
    global_config: &std::path::Path,
    project_config: &std::path::Path,
    registry: &mut ToolRegistry,
    disabled: &HashSet<String>,
) -> Option<MCPRuntime> {
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
                return Some(MCPRuntime {
                    caller: Arc::new(ExtensionManagerCaller {
                        manager: ExtensionManager::new(),
                    }),
                    server_count: 0,
                    tool_count: 0,
                    tools_with_source: Vec::new(),
                    server_scopes,
                });
            }

            let mut manager = ExtensionManager::new();
            if let Err(e) = manager.initialize(configs).await {
                warn!(error = %e, "Failed to initialize MCP servers");
                return None;
            }
            let server_count = manager.server_count();
            let mcp_tools_with_server = manager.list_tools_with_server().to_vec();
            let mcp_tools = manager.list_tools();
            let caller: Arc<dyn MCPToolCaller> = Arc::new(ExtensionManagerCaller { manager });
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
            Some(MCPRuntime {
                caller,
                server_count,
                tool_count,
                tools_with_source,
                server_scopes,
            })
        }
        Ok(_) => None,
        Err(e) => {
            warn!(error = %e, "Failed to load MCP config, continuing without MCP tools");
            None
        }
    }
}

pub(crate) fn resolve_workspace_roots(
    cwd: &std::path::Path,
    configured: &[String],
) -> Vec<std::path::PathBuf> {
    let mut roots = vec![cwd.to_path_buf()];
    for raw in configured {
        if raw.trim().is_empty() {
            continue;
        }
        let candidate = std::path::PathBuf::from(raw);
        let resolved = if candidate.is_absolute() {
            candidate
        } else {
            cwd.join(candidate)
        };
        if resolved.is_dir() && !roots.iter().any(|existing| existing == &resolved) {
            roots.push(resolved);
        }
    }
    roots
}

pub(crate) fn build_tool_registry(
    platform: Arc<dyn Platform>,
    permission_inspector: Arc<dyn PermissionInspector>,
    permission_context: Arc<RwLock<InspectionContext>>,
    approval_bridge: ApprovalBridge,
) -> (ToolRegistry, SharedToolSources) {
    let mut registry = ToolRegistry::new();
    register_default_tools(&mut registry, platform.clone());
    register_extended_tools(&mut registry, platform);
    let middleware = PermissionMiddleware::new(
        permission_inspector,
        permission_context,
        Some(approval_bridge),
    );
    let shared_sources = middleware.tool_sources();
    registry.add_middleware(middleware);
    (registry, shared_sources)
}
