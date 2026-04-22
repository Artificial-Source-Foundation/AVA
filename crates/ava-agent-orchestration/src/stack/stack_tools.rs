//! Tool registry construction, workspace resolution, and runtime helpers.

use std::sync::Arc;

use async_trait::async_trait;
use ava_llm::provider::LLMProvider;
use ava_permissions::inspector::PermissionInspector;
use ava_platform::Platform;
use ava_tools::permission_middleware::{ApprovalBridge, PermissionMiddleware, SharedToolSources};
use ava_tools::registry::ToolRegistry;
use tokio::sync::RwLock;

use ava_permissions::inspector::InspectionContext;
use ava_plugin::PluginManager;
use ava_tools::core::register_default_tools_with_plugins;

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

pub(crate) fn build_tool_registry_with_plugins(
    platform: Arc<dyn Platform>,
    permission_inspector: Arc<dyn PermissionInspector>,
    permission_context: Arc<RwLock<InspectionContext>>,
    approval_bridge: ApprovalBridge,
    plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
) -> (
    ToolRegistry,
    SharedToolSources,
    ava_tools::core::file_backup::FileBackupSession,
) {
    let mut registry = ToolRegistry::new();
    let backup_session =
        register_default_tools_with_plugins(&mut registry, platform, plugin_manager.clone());
    let middleware = PermissionMiddleware::new(
        permission_inspector,
        permission_context,
        Some(approval_bridge),
    );
    // Wire the permission.ask hook: attach the plugin manager so plugins can
    // approve/deny tool calls before the interactive TUI bridge is invoked.
    let middleware = if let Some(pm) = plugin_manager {
        middleware.with_plugin_manager(pm)
    } else {
        middleware
    };
    let shared_sources = middleware.tool_sources();
    registry.add_middleware(middleware);
    (registry, shared_sources, backup_session)
}
