use std::collections::VecDeque;
use std::pin::Pin;
use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use ava_agent::agent_loop::{AgentConfig, AgentEvent, AgentLoop, PostEditValidationConfig};
use ava_agent::LLMProvider;
use ava_context::ContextManager;
use ava_tools::registry::{Tool, ToolRegistry};
use ava_types::{AvaError, Message, ToolResult};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

#[derive(Clone)]
struct MockLLMProvider {
    responses: Arc<Mutex<VecDeque<String>>>,
}

impl MockLLMProvider {
    fn new(responses: Vec<String>) -> Self {
        Self {
            responses: Arc::new(Mutex::new(responses.into())),
        }
    }
}

#[async_trait]
impl LLMProvider for MockLLMProvider {
    async fn generate(&self, _messages: &[Message]) -> ava_types::Result<String> {
        let mut lock = self
            .responses
            .lock()
            .map_err(|_| AvaError::ToolError("mock response lock poisoned".to_string()))?;

        lock.pop_front()
            .ok_or_else(|| AvaError::NotFound("no mock response available".to_string()))
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> ava_types::Result<Pin<Box<dyn Stream<Item = ava_types::StreamChunk> + Send>>> {
        let one = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![
            ava_types::StreamChunk::text(one),
        ])))
    }

    fn model_name(&self) -> &str {
        "mock-model"
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        (input.len() / 4).max(1)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        (input_tokens + output_tokens) as f64 / 1_000_000.0
    }
}

struct NoopTool {
    name: &'static str,
}

#[async_trait]
impl Tool for NoopTool {
    fn name(&self) -> &str {
        self.name
    }

    fn description(&self) -> &str {
        "No-op tool"
    }

    fn parameters(&self) -> Value {
        json!({"type": "object"})
    }

    async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
        Ok(ToolResult {
            call_id: "call_1".to_string(),
            content: format!("{} done", self.name),
            is_error: false,
        })
    }
}

struct FailingTool;

#[async_trait]
impl Tool for FailingTool {
    fn name(&self) -> &str {
        "fail_tool"
    }

    fn description(&self) -> &str {
        "Always fails"
    }

    fn parameters(&self) -> Value {
        json!({"type": "object"})
    }

    async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
        Ok(ToolResult {
            call_id: "call_fail".to_string(),
            content: "file not found: /nonexistent".to_string(),
            is_error: true,
        })
    }
}

#[derive(Clone)]
struct RecordingTool {
    name: &'static str,
    calls: Arc<Mutex<Vec<Value>>>,
    result: ToolResult,
}

#[async_trait]
impl Tool for RecordingTool {
    fn name(&self) -> &str {
        self.name
    }

    fn description(&self) -> &str {
        "Recording tool"
    }

    fn parameters(&self) -> Value {
        json!({"type": "object"})
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        self.calls
            .lock()
            .expect("recording tool lock poisoned")
            .push(args);
        Ok(self.result.clone())
    }
}

fn build_loop(responses: Vec<String>, token_limit: usize, max_turns: usize) -> AgentLoop {
    let mut tools = ToolRegistry::new();
    tools.register(NoopTool { name: "echo" });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    AgentLoop::new(
        Box::new(MockLLMProvider::new(responses)),
        tools,
        ContextManager::new(token_limit),
        AgentConfig {
            max_turns,
            token_limit,
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: 90,
        },
    )
}

#[tokio::test]
async fn completion_detection_stops_loop() {
    let mut loop_engine = build_loop(
        vec![
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
            "should not be consumed".to_string(),
        ],
        1_000,
        5,
    );

    let session = loop_engine
        .run("finish now")
        .await
        .expect("run should succeed");
    assert!(session
        .messages
        .iter()
        .any(|message| message.content.contains("attempt_completion")));
}

#[tokio::test]
async fn natural_completion_on_text_only_response() {
    let mut loop_engine = build_loop(vec!["plain response".to_string(); 5], 1_000, 5);
    let session = loop_engine
        .run("keep going")
        .await
        .expect("run should succeed");

    // Natural completion: first non-empty text-only response ends the loop immediately
    assert_eq!(
        session
            .messages
            .iter()
            .filter(|message| message.role == ava_types::Role::Assistant)
            .count(),
        1
    );
}

#[tokio::test]
async fn context_compaction_triggered_when_threshold_exceeded() {
    // First response uses a tool call (so the loop continues), with a long body to fill context.
    // Second response completes via attempt_completion.
    let long_tool_response = json!({
        "tool_call": {"name": "echo", "arguments": {"data": "x".repeat(800)}},
    })
    .to_string();
    let mut loop_engine = build_loop(
        vec![
            long_tool_response,
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ],
        40,
        4,
    );

    let stream = loop_engine.run_streaming("compact context please").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    assert!(events.iter().any(|event| {
        matches!(event, AgentEvent::Progress(message) if message == "context compacted")
    }));
}

#[tokio::test]
async fn error_hint_injected_after_tool_failure() {
    let mut tools = ToolRegistry::new();
    tools.register(FailingTool);
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(vec![
            // First turn: call the failing tool
            json!({"tool_call": {"name": "fail_tool", "arguments": {}}}).to_string(),
            // Second turn: complete
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        AgentConfig {
            max_turns: 5,
            token_limit: 10_000,
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: 90,
        },
    );

    let session = loop_engine
        .run("do something")
        .await
        .expect("run should succeed");

    // Should find a hint message about the tool failure
    let has_hint = session.messages.iter().any(|m| {
        m.role == ava_types::Role::User
            && m.content.contains("Tool call failed")
            && m.content.contains("different approach")
    });
    assert!(has_hint, "expected error hint message in session");
}

#[tokio::test]
async fn empty_response_does_not_crash() {
    // MockProvider returns empty string — agent should handle gracefully
    let mut loop_engine = build_loop(vec!["".to_string()], 1_000, 5);
    let result = loop_engine.run("do something").await;
    // Should complete without panic — either Ok or a controlled error
    assert!(result.is_ok() || result.is_err());
}

#[tokio::test]
async fn post_edit_validation_runs_opt_in_lint_after_edit() {
    let lint_calls = Arc::new(Mutex::new(Vec::new()));

    let mut tools = ToolRegistry::new();
    tools.register(RecordingTool {
        name: "edit",
        calls: Arc::new(Mutex::new(Vec::new())),
        result: ToolResult {
            call_id: "call_edit".to_string(),
            content: "Applied exact_match; changed 1 lines".to_string(),
            is_error: false,
        },
    });
    tools.register(RecordingTool {
        name: "lint",
        calls: lint_calls.clone(),
        result: ToolResult {
            call_id: "call_lint".to_string(),
            content: json!({
                "warnings": 1,
                "errors": 0,
                "output": "warning: demo",
                "fixed": false,
            })
            .to_string(),
            is_error: false,
        },
    });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(vec![
            json!({
                "tool_call": {
                    "name": "edit",
                    "arguments": {"path": "src/lib.rs", "old_text": "a", "new_text": "b"}
                }
            })
            .to_string(),
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        AgentConfig {
            max_turns: 5,
            token_limit: 10_000,
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: Some(PostEditValidationConfig {
                lint: true,
                tests: false,
                lint_command: None,
                test_command: None,
                test_timeout_secs: 60,
            }),
            auto_compact: true,
            stream_timeout_secs: 90,
        },
    );

    let session = loop_engine
        .run("edit and validate")
        .await
        .expect("run should succeed");

    let calls = lint_calls.lock().expect("lint calls lock poisoned");
    assert_eq!(calls.len(), 1);
    assert_eq!(
        calls[0].get("path").and_then(Value::as_str),
        Some("src/lib.rs")
    );

    let tool_message = session
        .messages
        .iter()
        .find(|message| {
            message.role == ava_types::Role::Tool && message.content.contains("Applied exact_match")
        })
        .expect("tool message should be present");
    assert!(tool_message.content.contains("[post-edit validation]"));
    assert!(tool_message
        .content
        .contains("- lint: passed (0 errors, 1 warnings)"));
}

#[tokio::test]
async fn post_edit_validation_keeps_tool_success_distinct_when_tests_fail() {
    let test_calls = Arc::new(Mutex::new(Vec::new()));
    let mut tools = ToolRegistry::new();
    tools.register(RecordingTool {
        name: "edit",
        calls: Arc::new(Mutex::new(Vec::new())),
        result: ToolResult {
            call_id: "call_edit".to_string(),
            content: "Applied exact_match; changed 1 lines".to_string(),
            is_error: false,
        },
    });
    tools.register(RecordingTool {
        name: "test_runner",
        calls: test_calls.clone(),
        result: ToolResult {
            call_id: "call_test".to_string(),
            content: json!({
                "passed": false,
                "exit_code": 101,
                "output": "tests failed",
            })
            .to_string(),
            is_error: true,
        },
    });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(vec![
            json!({
                "tool_call": {
                    "name": "edit",
                    "arguments": {"path": "src/lib.rs", "old_text": "a", "new_text": "b"}
                }
            })
            .to_string(),
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        AgentConfig {
            max_turns: 5,
            token_limit: 10_000,
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: Some(PostEditValidationConfig {
                lint: false,
                tests: true,
                lint_command: None,
                test_command: None,
                test_timeout_secs: 60,
            }),
            auto_compact: true,
            stream_timeout_secs: 90,
        },
    );

    let session = loop_engine
        .run("edit and validate")
        .await
        .expect("run should succeed");

    let tool_message = session
        .messages
        .iter()
        .find(|message| {
            message.role == ava_types::Role::Tool && message.content.contains("Applied exact_match")
        })
        .expect("tool message should be present");
    assert!(tool_message
        .content
        .contains("- tests: failed (exit code 101)"));
    assert!(!tool_message.content.contains("Tool call failed:"));

    let calls = test_calls.lock().expect("test calls lock poisoned");
    assert_eq!(calls.len(), 1);

    let has_hint = session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && message.content.contains("Post-edit validation failed")
            && message.content.contains("- tests: failed (exit code 101)")
    });
    assert!(
        has_hint,
        "validation failures should trigger the existing self-correction hint"
    );
}

#[tokio::test]
async fn post_edit_validation_scopes_lint_for_apply_patch_paths() {
    let lint_calls = Arc::new(Mutex::new(Vec::new()));

    let mut tools = ToolRegistry::new();
    tools.register(RecordingTool {
        name: "apply_patch",
        calls: Arc::new(Mutex::new(Vec::new())),
        result: ToolResult {
            call_id: "call_patch".to_string(),
            content: "Applied 1 hunks to 1 files.".to_string(),
            is_error: false,
        },
    });
    tools.register(RecordingTool {
        name: "lint",
        calls: lint_calls.clone(),
        result: ToolResult {
            call_id: "call_lint".to_string(),
            content: json!({
                "warnings": 0,
                "errors": 0,
                "output": "",
                "fixed": false,
            })
            .to_string(),
            is_error: false,
        },
    });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(vec![
            json!({
                "tool_call": {
                    "name": "apply_patch",
                    "arguments": {
                        "patch": "--- a/src/lib.rs\n+++ b/src/lib.rs\n@@ -1 +1 @@\n-old\n+new\n"
                    }
                }
            })
            .to_string(),
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        AgentConfig {
            max_turns: 5,
            token_limit: 10_000,
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: Some(PostEditValidationConfig {
                lint: true,
                tests: false,
                lint_command: None,
                test_command: None,
                test_timeout_secs: 60,
            }),
            auto_compact: true,
            stream_timeout_secs: 90,
        },
    );

    loop_engine
        .run("patch and validate")
        .await
        .expect("run should succeed");

    let calls = lint_calls.lock().expect("lint calls lock poisoned");
    assert_eq!(calls.len(), 1);
    assert_eq!(
        calls[0].get("path").and_then(Value::as_str),
        Some("src/lib.rs")
    );
}
