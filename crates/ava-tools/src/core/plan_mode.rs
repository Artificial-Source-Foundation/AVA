//! Plan mode tools for requiring explicit approval on all tool calls.
//!
//! When plan mode is active, all subsequent tool calls require user approval
//! before execution. This provides a review-before-execute workflow.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::ToolResult;
use serde_json::{json, Value};

use crate::registry::Tool;

/// Shared flag indicating whether plan mode is active.
///
/// When true, the agent loop should require explicit user approval before
/// executing any tool call.
pub type PlanModeFlag = Arc<AtomicBool>;

/// Create a new plan mode flag (defaults to inactive).
pub fn new_plan_mode_flag() -> PlanModeFlag {
    Arc::new(AtomicBool::new(false))
}

/// Tool to enter plan mode — all subsequent tool calls require approval.
pub struct EnterPlanModeTool {
    flag: PlanModeFlag,
}

impl EnterPlanModeTool {
    pub fn new(flag: PlanModeFlag) -> Self {
        Self { flag }
    }
}

#[async_trait]
impl Tool for EnterPlanModeTool {
    fn name(&self) -> &str {
        "enter_plan_mode"
    }

    fn description(&self) -> &str {
        "Enter plan mode where all tool calls require explicit user approval before execution. \
         search_hint: plan mode approval review before execute"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {}
        })
    }

    async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
        self.flag.store(true, Ordering::SeqCst);
        Ok(ToolResult {
            call_id: String::new(),
            content: "Entered plan mode — all tool calls now require approval.".to_string(),
            is_error: false,
        })
    }
}

/// Tool to exit plan mode — restore normal permission handling.
pub struct ExitPlanModeTool {
    flag: PlanModeFlag,
}

impl ExitPlanModeTool {
    pub fn new(flag: PlanModeFlag) -> Self {
        Self { flag }
    }
}

#[async_trait]
impl Tool for ExitPlanModeTool {
    fn name(&self) -> &str {
        "exit_plan_mode"
    }

    fn description(&self) -> &str {
        "Exit plan mode and restore normal tool execution permissions. \
         search_hint: exit plan mode resume normal"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {}
        })
    }

    async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
        self.flag.store(false, Ordering::SeqCst);
        Ok(ToolResult {
            call_id: String::new(),
            content: "Exited plan mode — normal permissions restored.".to_string(),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn enter_plan_mode_metadata() {
        let flag = new_plan_mode_flag();
        let tool = EnterPlanModeTool::new(flag);
        assert_eq!(tool.name(), "enter_plan_mode");
        assert!(tool.description().contains("plan mode"));
        assert!(tool.description().contains("approval"));
    }

    #[test]
    fn exit_plan_mode_metadata() {
        let flag = new_plan_mode_flag();
        let tool = ExitPlanModeTool::new(flag);
        assert_eq!(tool.name(), "exit_plan_mode");
        assert!(tool.description().contains("plan mode"));
        assert!(tool.description().contains("normal"));
    }

    #[tokio::test]
    async fn flag_toggling() {
        let flag = new_plan_mode_flag();
        assert!(!flag.load(Ordering::SeqCst));

        let enter = EnterPlanModeTool::new(flag.clone());
        let result = enter.execute(json!({})).await.unwrap();
        assert!(!result.is_error);
        assert!(flag.load(Ordering::SeqCst));

        let exit = ExitPlanModeTool::new(flag.clone());
        let result = exit.execute(json!({})).await.unwrap();
        assert!(!result.is_error);
        assert!(!flag.load(Ordering::SeqCst));
    }

    #[tokio::test]
    async fn enter_returns_confirmation_message() {
        let flag = new_plan_mode_flag();
        let tool = EnterPlanModeTool::new(flag);
        let result = tool.execute(json!({})).await.unwrap();
        assert!(result.content.contains("Entered plan mode"));
        assert!(result.content.contains("require approval"));
    }

    #[tokio::test]
    async fn exit_returns_confirmation_message() {
        let flag = new_plan_mode_flag();
        let tool = ExitPlanModeTool::new(flag);
        let result = tool.execute(json!({})).await.unwrap();
        assert!(result.content.contains("Exited plan mode"));
        assert!(result.content.contains("normal permissions"));
    }

    #[test]
    fn parameter_schemas_are_empty_objects() {
        let flag = new_plan_mode_flag();
        let enter = EnterPlanModeTool::new(flag.clone());
        let exit = ExitPlanModeTool::new(flag);

        assert_eq!(enter.parameters()["type"], "object");
        assert!(enter.parameters()["properties"]
            .as_object()
            .unwrap()
            .is_empty());
        assert_eq!(exit.parameters()["type"], "object");
        assert!(exit.parameters()["properties"]
            .as_object()
            .unwrap()
            .is_empty());
    }
}
