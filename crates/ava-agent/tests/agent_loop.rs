use std::collections::VecDeque;
use std::pin::Pin;
use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use ava_agent::agent_loop::{AgentConfig, AgentEvent, AgentLoop};
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
            system_prompt_suffix: None,
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

    let session = loop_engine.run("finish now").await.expect("run should succeed");
    assert!(
        session
            .messages
            .iter()
            .any(|message| message.content.contains("attempt_completion"))
    );
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
    }).to_string();
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
            system_prompt_suffix: None,
        },
    );

    let session = loop_engine.run("do something").await.expect("run should succeed");

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
