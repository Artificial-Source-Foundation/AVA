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
    ) -> ava_types::Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        let one = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![one])))
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
async fn max_turn_limit_stops_execution() {
    let mut loop_engine = build_loop(vec!["plain response".to_string(); 5], 1_000, 2);
    let session = loop_engine
        .run("keep going")
        .await
        .expect("run should stop at max turns");

    assert_eq!(
        session
            .messages
            .iter()
            .filter(|message| message.role == ava_types::Role::Assistant)
            .count(),
        2
    );
}

#[tokio::test]
async fn context_compaction_triggered_when_threshold_exceeded() {
    let mut loop_engine = build_loop(
        vec![
            "A very long response that consumes many tokens in one shot and should trigger compaction"
                .repeat(20),
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
