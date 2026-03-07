use std::sync::Arc;

use async_trait::async_trait;
use ava_permissions::inspector::{InspectionContext, PermissionInspector};
use ava_types::{AvaError, Result, ToolCall, ToolResult};
use tokio::sync::RwLock;

use crate::registry::Middleware;

pub struct PermissionMiddleware {
    inspector: Arc<dyn PermissionInspector>,
    context: Arc<RwLock<InspectionContext>>,
}

impl PermissionMiddleware {
    pub fn new(
        inspector: Arc<dyn PermissionInspector>,
        context: Arc<RwLock<InspectionContext>>,
    ) -> Self {
        Self { inspector, context }
    }
}

#[async_trait]
impl Middleware for PermissionMiddleware {
    async fn before(&self, tool_call: &ToolCall) -> Result<()> {
        let context = self.context.read().await;
        let result =
            self.inspector
                .inspect(&tool_call.name, &tool_call.arguments, &context);

        match result.action {
            ava_permissions::Action::Allow => Ok(()),
            ava_permissions::Action::Deny => {
                Err(AvaError::PermissionDenied(result.reason))
            }
            ava_permissions::Action::Ask => Err(AvaError::PermissionDenied(format!(
                "Tool '{}' requires approval: {} (risk: {:?})",
                tool_call.name, result.reason, result.risk_level
            ))),
        }
    }

    async fn after(&self, _tool_call: &ToolCall, result: &ToolResult) -> Result<ToolResult> {
        Ok(result.clone())
    }
}
