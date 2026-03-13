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

pub struct PermissionMiddleware {
    inspector: Arc<dyn PermissionInspector>,
    context: Arc<RwLock<InspectionContext>>,
    approval_bridge: Option<ApprovalBridge>,
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
        }
    }
}

#[async_trait]
impl Middleware for PermissionMiddleware {
    async fn before(&self, tool_call: &ToolCall) -> Result<()> {
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
            safety_profiles: std::collections::HashMap::new(),
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
