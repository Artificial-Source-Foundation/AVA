use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use ava_tools::registry::{Middleware, Tool, ToolRegistry};
use ava_types::{AvaError, ToolCall, ToolResult};
use serde_json::{json, Value};

struct EchoTool;

#[async_trait]
impl Tool for EchoTool {
    fn name(&self) -> &str {
        "echo"
    }

    fn description(&self) -> &str {
        "Echoes an input value"
    }

    fn parameters(&self) -> Value {
        json!({"type": "object", "properties": {"input": {"type": "string"}}})
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let input = args
            .get("input")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing input".to_string()))?;

        Ok(ToolResult {
            call_id: "call_1".to_string(),
            content: input.to_string(),
            is_error: false,
        })
    }
}

#[derive(Clone)]
struct RecordingMiddleware {
    events: Arc<Mutex<Vec<String>>>,
}

#[async_trait]
impl Middleware for RecordingMiddleware {
    async fn before(&self, tool_call: &ToolCall) -> ava_types::Result<()> {
        let mut events = self
            .events
            .lock()
            .map_err(|_| AvaError::ToolError("middleware mutex poisoned".to_string()))?;
        events.push(format!("before:{}", tool_call.name));
        Ok(())
    }

    async fn after(
        &self,
        tool_call: &ToolCall,
        result: &ToolResult,
    ) -> ava_types::Result<ToolResult> {
        let mut events = self
            .events
            .lock()
            .map_err(|_| AvaError::ToolError("middleware mutex poisoned".to_string()))?;
        events.push(format!("after:{}", tool_call.name));

        Ok(ToolResult {
            call_id: result.call_id.clone(),
            content: format!("{}:wrapped", result.content),
            is_error: result.is_error,
        })
    }
}

#[tokio::test]
async fn register_and_execute_tool() {
    let mut registry = ToolRegistry::new();
    registry.register(EchoTool);

    let call = ToolCall {
        id: "call_1".to_string(),
        name: "echo".to_string(),
        arguments: json!({"input": "hello"}),
    };

    let result = registry.execute(call).await.expect("tool should execute");
    assert_eq!(result.content, "hello");
    assert!(!result.is_error);
}

#[tokio::test]
async fn middleware_runs_before_and_after() {
    let events = Arc::new(Mutex::new(Vec::new()));

    let mut registry = ToolRegistry::new();
    registry.register(EchoTool);
    registry.add_middleware(RecordingMiddleware {
        events: events.clone(),
    });

    let call = ToolCall {
        id: "call_1".to_string(),
        name: "echo".to_string(),
        arguments: json!({"input": "hello"}),
    };

    let result = registry.execute(call).await.expect("tool should execute");
    assert_eq!(result.content, "hello:wrapped");

    let events = events
        .lock()
        .expect("event mutex should be available")
        .clone();
    assert_eq!(events, vec!["before:echo", "after:echo"]);
}

#[tokio::test]
async fn missing_tool_returns_tool_not_found_error() {
    let registry = ToolRegistry::new();
    let call = ToolCall {
        id: "call_1".to_string(),
        name: "does_not_exist".to_string(),
        arguments: json!({}),
    };

    let error = registry
        .execute(call)
        .await
        .expect_err("unknown tool should fail");

    match error {
        AvaError::NotFound(message) => assert!(message.contains("ToolNotFound")),
        other => panic!("expected not found error, got {other:?}"),
    }
}

#[test]
fn list_tools_returns_registered_definitions() {
    let mut registry = ToolRegistry::new();
    registry.register(EchoTool);

    let tools = registry.list_tools();
    assert_eq!(tools.len(), 1);
    assert_eq!(tools[0].name, "echo");
    assert_eq!(tools[0].description, "Echoes an input value");
}
