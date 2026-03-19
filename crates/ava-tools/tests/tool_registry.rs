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
        AvaError::ToolNotFound { tool, .. } => assert_eq!(tool, "does_not_exist"),
        other => panic!("expected ToolNotFound error, got {other:?}"),
    }
}

/// A tool named "read" that fails with a transient error a configurable number
/// of times before succeeding.
struct FlakeyReadTool {
    fail_count: Arc<Mutex<usize>>,
    max_failures: usize,
    error_message: String,
}

impl FlakeyReadTool {
    fn new(max_failures: usize, error_message: &str) -> Self {
        Self {
            fail_count: Arc::new(Mutex::new(0)),
            max_failures,
            error_message: error_message.to_string(),
        }
    }
}

#[async_trait]
impl Tool for FlakeyReadTool {
    fn name(&self) -> &str {
        "read"
    }

    fn description(&self) -> &str {
        "Flakey read tool for testing retry"
    }

    fn parameters(&self) -> Value {
        json!({"type": "object", "properties": {"path": {"type": "string"}}})
    }

    async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
        let mut count = self.fail_count.lock().unwrap();
        *count += 1;
        if *count <= self.max_failures {
            Err(AvaError::ToolError(self.error_message.clone()))
        } else {
            Ok(ToolResult {
                call_id: "call_1".to_string(),
                content: "file contents".to_string(),
                is_error: false,
            })
        }
    }
}

#[tokio::test]
async fn retry_succeeds_on_transient_error() {
    let tool = FlakeyReadTool::new(1, "permission denied");
    let fail_count = tool.fail_count.clone();
    let mut registry = ToolRegistry::new();
    registry.register(tool);

    let call = ToolCall {
        id: "call_1".to_string(),
        name: "read".to_string(),
        arguments: json!({"path": "/tmp/test"}),
    };

    let result = registry
        .execute(call)
        .await
        .expect("should succeed after retry");
    assert_eq!(result.content, "file contents");
    assert!(!result.is_error);
    // Should have attempted twice: 1 failure + 1 success
    assert_eq!(*fail_count.lock().unwrap(), 2);
}

#[tokio::test]
async fn retry_gives_up_after_max_retries() {
    let tool = FlakeyReadTool::new(5, "connection refused");
    let fail_count = tool.fail_count.clone();
    let mut registry = ToolRegistry::new();
    registry.register(tool);

    let call = ToolCall {
        id: "call_1".to_string(),
        name: "read".to_string(),
        arguments: json!({"path": "/tmp/test"}),
    };

    let err = registry
        .execute(call)
        .await
        .expect_err("should fail after max retries");
    assert!(err.to_string().contains("connection refused"));
    // Should have attempted 3 times: 1 initial + 2 retries
    assert_eq!(*fail_count.lock().unwrap(), 3);
}

#[tokio::test]
async fn no_retry_on_permanent_error() {
    let tool = FlakeyReadTool::new(5, "file not found: /tmp/missing.txt");
    let fail_count = tool.fail_count.clone();
    let mut registry = ToolRegistry::new();
    registry.register(tool);

    let call = ToolCall {
        id: "call_1".to_string(),
        name: "read".to_string(),
        arguments: json!({"path": "/tmp/missing.txt"}),
    };

    let err = registry
        .execute(call)
        .await
        .expect_err("should fail immediately");
    assert!(err.to_string().contains("file not found"));
    // Should have attempted only once — no retries for permanent errors
    assert_eq!(*fail_count.lock().unwrap(), 1);
}

/// A tool named "write" that always fails. Should never be retried.
struct FlakeyWriteTool;

#[async_trait]
impl Tool for FlakeyWriteTool {
    fn name(&self) -> &str {
        "write"
    }

    fn description(&self) -> &str {
        "Flakey write tool for testing no-retry"
    }

    fn parameters(&self) -> Value {
        json!({"type": "object"})
    }

    async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
        Err(AvaError::ToolError("permission denied".to_string()))
    }
}

#[tokio::test]
async fn no_retry_on_non_read_only_tools() {
    let mut registry = ToolRegistry::new();
    registry.register(FlakeyWriteTool);

    let call = ToolCall {
        id: "call_1".to_string(),
        name: "write".to_string(),
        arguments: json!({}),
    };

    let err = registry
        .execute(call)
        .await
        .expect_err("write should fail without retry");
    assert!(err.to_string().contains("permission denied"));
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
