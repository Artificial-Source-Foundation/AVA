use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_permissions::inspector::{InspectionContext, InspectionResult, PermissionInspector};
use ava_types::{AvaError, Result, ToolCall, ToolResult};
use tokio::sync::{mpsc, oneshot, RwLock};

use crate::registry::Middleware;

#[derive(Debug, Clone)]
pub enum ToolApproval {
    Allowed,
    AllowedForSession,
    /// Persist the approval to `.ava/permissions.toml` so it survives across sessions.
    AllowAlways,
    Rejected(Option<String>),
}

#[derive(Debug)]
pub struct ApprovalRequest {
    pub call: ToolCall,
    pub inspection: InspectionResult,
    pub reply: oneshot::Sender<ToolApproval>,
}

#[derive(Clone)]
pub struct ApprovalBridge {
    tx: mpsc::UnboundedSender<ApprovalRequest>,
}

impl ApprovalBridge {
    pub fn new() -> (Self, mpsc::UnboundedReceiver<ApprovalRequest>) {
        let (tx, rx) = mpsc::unbounded_channel();
        (Self { tx }, rx)
    }

    async fn request_approval(
        &self,
        call: ToolCall,
        inspection: InspectionResult,
    ) -> Result<ToolApproval> {
        let (reply_tx, reply_rx) = oneshot::channel();
        self.tx
            .send(ApprovalRequest {
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
        }
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
                        // Persist to ~/.ava/permissions.toml (user-global, not repo-local)
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
}
