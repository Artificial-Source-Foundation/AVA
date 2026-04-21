use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_permissions::inspector::{InspectionContext, InspectionResult, PermissionInspector};
use ava_plugin::{PluginManager, PluginPermissionDecision};
use ava_types::{AvaError, Result, ToolCall, ToolResult};
use tokio::sync::{mpsc, oneshot, RwLock};

use crate::registry::Middleware;

#[derive(Debug, Clone, PartialEq)]
pub enum ToolApproval {
    Allowed,
    AllowedForSession,
    /// Persist the approval to `.ava/permissions.toml` so it survives across sessions.
    AllowAlways,
    Rejected(Option<String>),
}

#[derive(Debug)]
pub struct ApprovalRequest {
    pub run_id: Option<String>,
    pub call: ToolCall,
    pub inspection: InspectionResult,
    pub reply: oneshot::Sender<ToolApproval>,
}

#[derive(Clone)]
pub struct ApprovalBridge {
    tx: mpsc::UnboundedSender<ApprovalRequest>,
    run_id: Option<String>,
}

impl ApprovalBridge {
    pub fn new() -> (Self, mpsc::UnboundedReceiver<ApprovalRequest>) {
        let (tx, rx) = mpsc::unbounded_channel();
        (Self { tx, run_id: None }, rx)
    }

    pub fn with_run_id(&self, run_id: Option<String>) -> Self {
        Self {
            tx: self.tx.clone(),
            run_id,
        }
    }

    async fn request_approval(
        &self,
        call: ToolCall,
        inspection: InspectionResult,
    ) -> Result<ToolApproval> {
        let (reply_tx, reply_rx) = oneshot::channel();
        self.tx
            .send(ApprovalRequest {
                run_id: self.run_id.clone(),
                call,
                inspection,
                reply: reply_tx,
            })
            .map_err(|_| {
                AvaError::ToolError(
                    "Failed to send tool approval request to UI — the TUI may not be running"
                        .to_string(),
                )
            })?;

        reply_rx.await.map_err(|_| {
            AvaError::ToolError(
                "Tool approval request was not answered — the UI channel was closed".to_string(),
            )
        })
    }
}

/// Shared map of tool name → permission-level [`ava_permissions::inspector::ToolSource`].
///
/// Populated by the stack after tool registration; the middleware reads it per-call
/// to set `InspectionContext.tool_source` before the inspector runs.
pub type SharedToolSources =
    Arc<std::sync::RwLock<HashMap<String, ava_permissions::inspector::ToolSource>>>;

/// Convert a registry-level [`crate::registry::ToolSource`] into the
/// permission-level [`ava_permissions::inspector::ToolSource`].
pub fn convert_tool_source(
    src: &crate::registry::ToolSource,
) -> ava_permissions::inspector::ToolSource {
    match src {
        crate::registry::ToolSource::BuiltIn => ava_permissions::inspector::ToolSource::BuiltIn,
        crate::registry::ToolSource::MCP { server } => {
            ava_permissions::inspector::ToolSource::MCP {
                server: server.clone(),
            }
        }
        crate::registry::ToolSource::Custom { path } => {
            ava_permissions::inspector::ToolSource::Custom { path: path.clone() }
        }
    }
}

pub struct PermissionMiddleware {
    inspector: Arc<dyn PermissionInspector>,
    context: Arc<RwLock<InspectionContext>>,
    approval_bridge: Option<ApprovalBridge>,
    tool_sources: SharedToolSources,
    /// Optional plugin manager for the `permission.ask` hook.
    /// When set, plugins are consulted before the interactive bridge is invoked.
    plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
}

impl PermissionMiddleware {
    pub fn new(
        inspector: Arc<dyn PermissionInspector>,
        context: Arc<RwLock<InspectionContext>>,
        approval_bridge: Option<ApprovalBridge>,
    ) -> Self {
        Self {
            inspector,
            context,
            approval_bridge,
            tool_sources: Arc::new(std::sync::RwLock::new(HashMap::new())),
            plugin_manager: None,
        }
    }

    /// Attach a plugin manager to enable the `permission.ask` hook.
    pub fn with_plugin_manager(mut self, pm: Arc<tokio::sync::Mutex<PluginManager>>) -> Self {
        self.plugin_manager = Some(pm);
        self
    }

    /// Return a shared handle to the tool-source map.
    ///
    /// The caller (typically `build_tool_registry` in `ava-agent`) populates this
    /// map after registering tools so the middleware can look up each tool's
    /// provenance at call time.
    pub fn tool_sources(&self) -> SharedToolSources {
        Arc::clone(&self.tool_sources)
    }
}

#[async_trait]
impl Middleware for PermissionMiddleware {
    async fn before(&self, tool_call: &ToolCall) -> Result<()> {
        // Set the tool source on the context so the inspector can make
        // source-aware decisions (e.g. only auto-approve truly built-in tools).
        {
            let source = self
                .tool_sources
                .read()
                .unwrap_or_else(|e| e.into_inner())
                .get(&tool_call.name)
                .cloned();
            self.context.write().await.tool_source = source;
        }

        let context = self.context.read().await;
        let result = self
            .inspector
            .inspect(&tool_call.name, &tool_call.arguments, &context);
        drop(context);

        match result.action {
            ava_permissions::Action::Allow => Ok(()),
            ava_permissions::Action::Deny => Err(AvaError::PermissionDenied(result.reason)),
            ava_permissions::Action::Ask => {
                // permission.ask hook: consult plugins before interrupting the user.
                if let Some(ref pm) = self.plugin_manager {
                    let risk_str = format!("{:?}", result.risk_level).to_lowercase();
                    let plugin_decision = pm
                        .lock()
                        .await
                        .ask_permission(
                            &tool_call.name,
                            &tool_call.arguments,
                            &risk_str,
                            &result.reason,
                        )
                        .await;
                    match plugin_decision {
                        Some(PluginPermissionDecision::Allow) => return Ok(()),
                        Some(PluginPermissionDecision::Deny { reason }) => {
                            return Err(AvaError::PermissionDenied(reason));
                        }
                        None => {} // No plugin decision — fall through to normal flow
                    }
                }

                let Some(approval_bridge) = &self.approval_bridge else {
                    return Err(AvaError::PermissionDenied(format!(
                        "Tool '{}' requires approval: {} (risk: {:?})",
                        tool_call.name, result.reason, result.risk_level
                    )));
                };

                match approval_bridge
                    .request_approval(tool_call.clone(), result.clone())
                    .await?
                {
                    ToolApproval::Allowed => Ok(()),
                    ToolApproval::AllowedForSession => {
                        self.context
                            .write()
                            .await
                            .session_approved
                            .insert(tool_call.name.clone());
                        Ok(())
                    }
                    ToolApproval::AllowAlways => {
                        let mut ctx = self.context.write().await;
                        // Add to session approved for immediate effect
                        ctx.session_approved.insert(tool_call.name.clone());
                        // Persist to $XDG_CONFIG_HOME/ava/permissions.toml (user-global, not repo-local)
                        ctx.persistent_rules.allow_tool(&tool_call.name);
                        if let Err(e) = ctx.persistent_rules.save() {
                            tracing::warn!("Failed to save persistent permission rules: {e}");
                        }
                        Ok(())
                    }
                    ToolApproval::Rejected(reason) => {
                        Err(AvaError::PermissionDenied(reason.unwrap_or_else(|| {
                            format!("Tool '{}' was rejected by the user", tool_call.name)
                        })))
                    }
                }
            }
        }
    }

    async fn after(&self, _tool_call: &ToolCall, result: &ToolResult) -> Result<ToolResult> {
        Ok(result.clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_permissions::tags::RiskLevel;
    use ava_permissions::Action;
    use serde_json::json;

    struct AskInspector;

    impl PermissionInspector for AskInspector {
        fn inspect(
            &self,
            tool_name: &str,
            _arguments: &serde_json::Value,
            context: &InspectionContext,
        ) -> InspectionResult {
            if context.session_approved.contains(tool_name) {
                InspectionResult {
                    action: Action::Allow,
                    reason: "session approved".to_string(),
                    risk_level: RiskLevel::Medium,
                    tags: Vec::new(),
                    warnings: Vec::new(),
                }
            } else {
                InspectionResult {
                    action: Action::Ask,
                    reason: "needs approval".to_string(),
                    risk_level: RiskLevel::Medium,
                    tags: Vec::new(),
                    warnings: vec!["careful".to_string()],
                }
            }
        }
    }

    fn test_call() -> ToolCall {
        ToolCall {
            id: "call_1".to_string(),
            name: "bash".to_string(),
            arguments: json!({"command": "ls"}),
        }
    }

    fn test_context() -> Arc<RwLock<InspectionContext>> {
        Arc::new(RwLock::new(InspectionContext {
            workspace_root: "/workspace".into(),
            auto_approve: false,
            session_approved: std::collections::HashSet::new(),
            persistent_rules: ava_permissions::persistent::PersistentRules::default(),
            safety_profiles: std::collections::HashMap::new(),
            tool_source: None,
            glob_rules: ava_permissions::glob_rules::GlobRuleset::empty(),
        }))
    }

    #[tokio::test]
    async fn approval_bridge_allows_tool_execution() {
        let (bridge, mut rx) = ApprovalBridge::new();
        let middleware =
            PermissionMiddleware::new(Arc::new(AskInspector), test_context(), Some(bridge));

        tokio::spawn(async move {
            let req = rx.recv().await.expect("approval request");
            req.reply
                .send(ToolApproval::Allowed)
                .expect("send approval");
        });

        middleware
            .before(&test_call())
            .await
            .expect("approval should allow execution");
    }

    #[tokio::test]
    async fn session_approval_persists_in_context() {
        let (bridge, mut rx) = ApprovalBridge::new();
        let context = test_context();
        let middleware =
            PermissionMiddleware::new(Arc::new(AskInspector), context.clone(), Some(bridge));

        tokio::spawn(async move {
            let req = rx.recv().await.expect("approval request");
            req.reply
                .send(ToolApproval::AllowedForSession)
                .expect("send session approval");
        });

        middleware
            .before(&test_call())
            .await
            .expect("first call should be approved");
        assert!(context.read().await.session_approved.contains("bash"));
        middleware
            .before(&test_call())
            .await
            .expect("second call should skip approval");
    }

    struct DenyInspector;

    impl PermissionInspector for DenyInspector {
        fn inspect(
            &self,
            _tool_name: &str,
            _arguments: &serde_json::Value,
            _context: &InspectionContext,
        ) -> InspectionResult {
            InspectionResult {
                action: Action::Deny,
                reason: "explicitly denied".to_string(),
                risk_level: RiskLevel::Critical,
                tags: Vec::new(),
                warnings: Vec::new(),
            }
        }
    }

    struct AllowInspector;

    impl PermissionInspector for AllowInspector {
        fn inspect(
            &self,
            _tool_name: &str,
            _arguments: &serde_json::Value,
            _context: &InspectionContext,
        ) -> InspectionResult {
            InspectionResult {
                action: Action::Allow,
                reason: "allowed".to_string(),
                risk_level: RiskLevel::Low,
                tags: Vec::new(),
                warnings: Vec::new(),
            }
        }
    }

    /// A deny result from the inspector propagates as `AvaError::PermissionDenied`.
    #[tokio::test]
    async fn deny_propagates_as_permission_denied() {
        let middleware = PermissionMiddleware::new(Arc::new(DenyInspector), test_context(), None);

        let err = middleware
            .before(&test_call())
            .await
            .expect_err("deny should return error");

        assert!(
            matches!(err, ava_types::AvaError::PermissionDenied(_)),
            "expected PermissionDenied, got: {err:?}"
        );
    }

    /// When there is no approval bridge, an Ask result is treated as a Deny.
    #[tokio::test]
    async fn ask_without_bridge_propagates_as_permission_denied() {
        let middleware = PermissionMiddleware::new(Arc::new(AskInspector), test_context(), None);

        let err = middleware
            .before(&test_call())
            .await
            .expect_err("Ask without bridge should return error");

        assert!(
            matches!(err, ava_types::AvaError::PermissionDenied(_)),
            "expected PermissionDenied, got: {err:?}"
        );
    }

    /// A user rejection via the approval bridge propagates as `AvaError::PermissionDenied`.
    #[tokio::test]
    async fn rejection_propagates_as_permission_denied() {
        let (bridge, mut rx) = ApprovalBridge::new();
        let middleware =
            PermissionMiddleware::new(Arc::new(AskInspector), test_context(), Some(bridge));

        tokio::spawn(async move {
            let req = rx.recv().await.expect("approval request");
            req.reply
                .send(ToolApproval::Rejected(Some("user said no".to_string())))
                .expect("send rejection");
        });

        let err = middleware
            .before(&test_call())
            .await
            .expect_err("rejection should return error");

        assert!(
            matches!(&err, ava_types::AvaError::PermissionDenied(msg) if msg.contains("user said no")),
            "expected PermissionDenied with reason, got: {err:?}"
        );
    }

    /// When auto_approve is set in the context and the inspector returns Allow,
    /// the call proceeds without needing the approval bridge.
    #[tokio::test]
    async fn auto_approve_context_bypasses_bridge_for_allowed_tools() {
        // Build a context with auto_approve = true
        let context = Arc::new(RwLock::new(InspectionContext {
            workspace_root: "/workspace".into(),
            auto_approve: true,
            session_approved: std::collections::HashSet::new(),
            persistent_rules: ava_permissions::persistent::PersistentRules::default(),
            safety_profiles: std::collections::HashMap::new(),
            tool_source: None,
            glob_rules: ava_permissions::glob_rules::GlobRuleset::empty(),
        }));

        // AllowInspector always allows — with auto_approve the bridge is never needed.
        // We intentionally pass None bridge to prove no bridge required.
        let middleware = PermissionMiddleware::new(Arc::new(AllowInspector), context, None);

        middleware
            .before(&test_call())
            .await
            .expect("allow + auto_approve should not require bridge");
    }

    /// `after` is a passthrough — it should return the result unchanged.
    #[tokio::test]
    async fn after_passthrough() {
        let middleware = PermissionMiddleware::new(Arc::new(AllowInspector), test_context(), None);

        let result = ava_types::ToolResult {
            call_id: "call_1".to_string(),
            content: "ok".to_string(),
            is_error: false,
        };

        let out = middleware.after(&test_call(), &result).await.unwrap();
        assert_eq!(out.content, "ok");
        assert!(!out.is_error);
    }
}
