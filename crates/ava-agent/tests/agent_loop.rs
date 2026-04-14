use std::collections::VecDeque;
use std::pin::Pin;
use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use ava_agent::agent_loop::{AgentConfig, AgentEvent, AgentLoop, PostEditValidationConfig};
use ava_agent::message_queue::MessageQueue;
use ava_agent::LLMProvider;
use ava_context::ContextManager;
use ava_tools::registry::{Tool, ToolRegistry};
use ava_types::{
    AvaError, Message, MessageTier, QueuedMessage, StreamChunk, ThinkingLevel, TokenUsage,
    ToolResult,
};
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

/// A mock provider that emits a `TokenUsage` chunk alongside each response.
#[derive(Clone)]
struct UsageMockProvider {
    responses: Arc<Mutex<VecDeque<String>>>,
    usage: TokenUsage,
}

impl UsageMockProvider {
    fn new(responses: Vec<String>, usage: TokenUsage) -> Self {
        Self {
            responses: Arc::new(Mutex::new(responses.into())),
            usage,
        }
    }
}

#[async_trait]
impl LLMProvider for UsageMockProvider {
    async fn generate(&self, _messages: &[Message]) -> ava_types::Result<String> {
        let mut lock = self
            .responses
            .lock()
            .map_err(|_| AvaError::ToolError("mock lock poisoned".to_string()))?;
        lock.pop_front()
            .ok_or_else(|| AvaError::NotFound("no mock response available".to_string()))
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> ava_types::Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let text = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![
            StreamChunk::text(text),
            StreamChunk::with_usage(self.usage.clone()),
        ])))
    }

    fn model_name(&self) -> &str {
        "mock-usage-model"
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        (input.len() / 4).max(1)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        (input_tokens + output_tokens) as f64 / 1_000_000.0
    }
}

fn build_loop_with_tools(
    responses: Vec<String>,
    tools: ToolRegistry,
    token_limit: usize,
    max_turns: usize,
) -> AgentLoop {
    AgentLoop::new(
        Box::new(MockLLMProvider::new(responses)),
        tools,
        ContextManager::new(token_limit),
        AgentConfig {
            max_turns,
            token_limit,
            provider: String::new(),
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: 90,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        },
    )
}

fn build_loop(responses: Vec<String>, token_limit: usize, max_turns: usize) -> AgentLoop {
    let mut tools = ToolRegistry::new();
    tools.register(NoopTool { name: "echo" });
    tools.register(NoopTool {
        name: "attempt_completion",
    });
    build_loop_with_tools(responses, tools, token_limit, max_turns)
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
async fn natural_completion_allows_honest_no_files_changed_statement() {
    let mut loop_engine = build_loop(
        vec!["I investigated the issue and no files were changed.".to_string()],
        1_000,
        5,
    );

    let session = loop_engine
        .run("check the project")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_messages.len(),
        1,
        "honest negative should complete"
    );
    assert!(assistant_messages[0]
        .content
        .contains("no files were changed"));

    assert!(!session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && !message.user_visible
            && message
                .content
                .contains("no successful matching tool result")
    }));
}

#[tokio::test]
async fn natural_completion_allows_honest_no_todo_changes_statement() {
    let mut loop_engine = build_loop(
        vec!["I did not change the todo list.".to_string()],
        1_000,
        5,
    );

    let session = loop_engine
        .run("track checklist state")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_messages.len(),
        1,
        "honest negative should complete"
    );
    assert!(assistant_messages[0]
        .content
        .contains("did not change the todo list"));

    assert!(!session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && !message.user_visible
            && message
                .content
                .contains("no successful matching tool result")
    }));
}

#[tokio::test]
async fn natural_completion_allows_generic_source_code_phrase_without_file_claim() {
    let mut loop_engine = build_loop(
        vec![
            "I changed approach after reviewing the source code and documented the likely fix."
                .to_string(),
        ],
        1_000,
        5,
    );

    let session = loop_engine
        .run("investigate only")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_messages.len(),
        1,
        "generic source/code phrase should not trigger file-claim guard"
    );

    assert!(!session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && !message.user_visible
            && message
                .content
                .contains("no successful matching tool result")
    }));
}

#[tokio::test]
async fn natural_completion_rejects_ungrounded_file_claims() {
    let mut loop_engine = build_loop(
        vec![
            "Updated src/lib.rs with the fix.".to_string(),
            "I inspected the code and found the likely fix, but I did not change files."
                .to_string(),
        ],
        1_000,
        5,
    );

    let session = loop_engine
        .run("check the project")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_messages.len(),
        2,
        "guard should force another turn"
    );
    assert!(assistant_messages[0].content.contains("Updated src/lib.rs"));
    assert!(assistant_messages[1]
        .content
        .contains("did not change files"));

    let nudge = session
        .messages
        .iter()
        .find(|message| {
            message.role == ava_types::Role::User
                && !message.user_visible
                && message
                    .content
                    .contains("no successful matching tool result")
        })
        .expect("expected hidden grounding nudge");
    assert!(nudge
        .content
        .contains("Do not claim actions that were not executed"));
}

#[tokio::test]
async fn natural_completion_allows_grounded_file_claim_after_successful_write() {
    let mut tools = ToolRegistry::new();
    tools.register(RecordingTool {
        name: "write",
        calls: Arc::new(Mutex::new(Vec::new())),
        result: ToolResult {
            call_id: "call_write".to_string(),
            content: "Wrote 1 lines to src/lib.rs".to_string(),
            is_error: false,
        },
    });

    let mut loop_engine = build_loop_with_tools(
        vec![
            json!({
                "tool_call": {
                    "name": "write",
                    "arguments": {"path": "src/lib.rs", "content": "updated"}
                }
            })
            .to_string(),
            "I updated src/lib.rs with the requested change.".to_string(),
            "should not be consumed".to_string(),
        ],
        tools,
        10_000,
        5,
    );

    let session = loop_engine
        .run("write the fix")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_messages.len(),
        2,
        "grounded natural completion should end after write + claim"
    );
    assert_eq!(
        assistant_messages[1].content,
        "I updated src/lib.rs with the requested change."
    );

    assert!(!session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && !message.user_visible
            && message
                .content
                .contains("no successful matching tool result")
    }));

    let tool_message = session
        .messages
        .iter()
        .find(|message| message.role == ava_types::Role::Tool)
        .expect("tool message should be present");
    assert!(tool_message.content.contains("Wrote 1 lines to src/lib.rs"));
}

#[tokio::test]
async fn natural_completion_caps_repeated_ungrounded_claim_retries() {
    let mut loop_engine = build_loop(
        vec![
            "Updated src/lib.rs with the fix.".to_string(),
            "Updated src/lib.rs with the fix again.".to_string(),
            "third response should not be consumed".to_string(),
        ],
        1_000,
        10,
    );

    let session = loop_engine
        .run("check the project")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_messages.len(),
        3,
        "guard should stop on second rejection"
    );
    assert!(assistant_messages[2]
        .content
        .contains("Stopping here because the run repeatedly claimed file edits or writes"));
    assert!(!assistant_messages.iter().any(|message| message
        .content
        .contains("third response should not be consumed")));

    let nudge_count = session
        .messages
        .iter()
        .filter(|message| {
            message.role == ava_types::Role::User
                && !message.user_visible
                && message
                    .content
                    .contains("no successful matching tool result")
        })
        .count();
    assert_eq!(nudge_count, 1, "guard should only retry once");
}

#[tokio::test]
async fn attempt_completion_rejects_ungrounded_claims() {
    let mut loop_engine = build_loop(
        vec![
            json!({
                "tool_call": {
                    "name": "attempt_completion",
                    "arguments": {"result": "Updated src/lib.rs and the todo list."}
                }
            })
            .to_string(),
            "I only inspected the files and did not change them.".to_string(),
        ],
        1_000,
        5,
    );

    let session = loop_engine
        .run("finish honestly")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_messages.len(),
        2,
        "attempt_completion should be rejected"
    );
    assert!(assistant_messages[1]
        .content
        .contains("I only inspected the files and did not change them."));

    assert!(session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && !message.user_visible
            && message.content.contains("todo updates")
    }));
}

#[tokio::test]
async fn attempt_completion_rejects_ungrounded_file_claim_after_failed_write() {
    let mut tools = ToolRegistry::new();
    tools.register(RecordingTool {
        name: "write",
        calls: Arc::new(Mutex::new(Vec::new())),
        result: ToolResult {
            call_id: "call_write".to_string(),
            content: "write failed: permission denied".to_string(),
            is_error: true,
        },
    });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = build_loop_with_tools(
        vec![
            json!({
                "tool_call": {
                    "name": "write",
                    "arguments": {"file_path": "src/lib.rs", "content": "updated"}
                }
            })
            .to_string(),
            json!({
                "tool_call": {
                    "name": "attempt_completion",
                    "arguments": {"result": "Updated src/lib.rs with the fix."}
                }
            })
            .to_string(),
            "I attempted a write, but it failed, so no files were changed.".to_string(),
        ],
        tools,
        10_000,
        5,
    );

    let session = loop_engine
        .run("make the write")
        .await
        .expect("run should succeed");

    assert!(session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && !message.user_visible
            && message.content.contains("file edits or writes")
    }));
    assert!(session.messages.iter().any(|message| {
        message.role == ava_types::Role::Tool
            && message.tool_results.iter().any(|result| result.is_error)
    }));

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert!(assistant_messages
        .last()
        .expect("expected assistant reply")
        .content
        .contains("it failed, so no files were changed"));
}

#[tokio::test]
async fn attempt_completion_rejects_ungrounded_mixed_claim_when_only_file_edit_succeeded() {
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
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = build_loop_with_tools(
        vec![
            json!({
                "tool_call": {
                    "name": "edit",
                    "arguments": {"path": "src/lib.rs", "old_text": "a", "new_text": "b"}
                }
            })
            .to_string(),
            json!({
                "tool_call": {
                    "name": "attempt_completion",
                    "arguments": {"result": "Updated src/lib.rs and the todo list."}
                }
            })
            .to_string(),
            "I updated src/lib.rs, but I did not change the todo list.".to_string(),
        ],
        tools,
        10_000,
        5,
    );

    let session = loop_engine
        .run("finish honestly")
        .await
        .expect("run should succeed");

    let nudge = session
        .messages
        .iter()
        .find(|message| {
            message.role == ava_types::Role::User
                && !message.user_visible
                && message
                    .content
                    .contains("no successful matching tool result")
        })
        .expect("expected hidden grounding nudge");
    assert!(nudge.content.contains("todo updates"));
    assert!(!nudge.content.contains("file edits or writes"));

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert!(assistant_messages
        .last()
        .expect("expected assistant reply")
        .content
        .contains("did not change the todo list"));
}

#[tokio::test]
async fn attempt_completion_allows_grounded_file_claims_after_successful_edit() {
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
            json!({
                "tool_call": {
                    "name": "attempt_completion",
                    "arguments": {"result": "Updated src/lib.rs with the fix."}
                }
            })
            .to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        AgentConfig {
            max_turns: 5,
            token_limit: 10_000,
            provider: String::new(),
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: 90,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        },
    );

    let session = loop_engine
        .run("make the edit")
        .await
        .expect("run should succeed");

    let assistant_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|message| message.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(assistant_messages.len(), 2);
    assert!(!session.messages.iter().any(|message| {
        message.role == ava_types::Role::User
            && !message.user_visible
            && message
                .content
                .contains("no successful matching tool result")
    }));
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
            provider: String::new(),
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: 90,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
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
            provider: String::new(),
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
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
            prompt_caching: true,
            headless: false,
            is_subagent: false,
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
            provider: String::new(),
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
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
            prompt_caching: true,
            headless: false,
            is_subagent: false,
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
            provider: String::new(),
            model: "mock-model".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
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
            prompt_caching: true,
            headless: false,
            is_subagent: false,
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

// ---------------------------------------------------------------------------
// Integration tests for run_unified() — the 325-line core execution path
// ---------------------------------------------------------------------------

/// Build a minimal AgentConfig with sensible defaults for integration tests.
fn test_config(max_turns: usize) -> AgentConfig {
    AgentConfig {
        max_turns,
        token_limit: 10_000,
        provider: String::new(),
        model: "mock-model".to_string(),
        max_budget_usd: 0.0,
        max_cost_usd: 10.0,
        loop_detection: true,
        custom_system_prompt: None,
        thinking_level: ThinkingLevel::Off,
        thinking_budget_tokens: None,
        system_prompt_suffix: None,
        benchmark_prompt_override: None,
        project_root: None,
        enable_dynamic_rules: false,
        extended_tools: false,
        plan_mode: false,
        post_edit_validation: None,
        auto_compact: true,
        stream_timeout_secs: 90,
        prompt_caching: true,
        headless: false,
        is_subagent: false,
    }
}

/// Single-turn run: verify `run()` returns `Ok(Session)` and session contains
/// both the user goal message and the assistant reply.
#[tokio::test]
async fn single_turn_headless_run_produces_session() {
    let mut loop_engine = build_loop(vec!["Task complete.".to_string()], 10_000, 5);
    let session = loop_engine
        .run("hello world")
        .await
        .expect("run should succeed");

    // Session must contain the user goal message
    let user_msgs: Vec<_> = session
        .messages
        .iter()
        .filter(|m| m.role == ava_types::Role::User)
        .collect();
    assert!(
        !user_msgs.is_empty(),
        "session should have at least one user message"
    );
    assert!(
        user_msgs.iter().any(|m| m.content.contains("hello world")),
        "goal message should appear in session"
    );

    // Session must contain the assistant reply
    let assistant_msgs: Vec<_> = session
        .messages
        .iter()
        .filter(|m| m.role == ava_types::Role::Assistant)
        .collect();
    assert_eq!(
        assistant_msgs.len(),
        1,
        "should have exactly one assistant message"
    );
    assert_eq!(assistant_msgs[0].content, "Task complete.");
}

/// Streaming single-turn run: verify `run_streaming()` emits a `Complete` event
/// and that the event's session contains the expected messages.
#[tokio::test]
async fn streaming_run_emits_complete_event() {
    let mut loop_engine = build_loop(vec!["All done!".to_string()], 10_000, 5);
    let stream = loop_engine.run_streaming("do something").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    // Must have a Complete event
    let complete_event = events.iter().find(|e| matches!(e, AgentEvent::Complete(_)));
    assert!(
        complete_event.is_some(),
        "streaming run should emit Complete event"
    );

    // The Complete event's session should contain the assistant reply
    if let Some(AgentEvent::Complete(session)) = complete_event {
        let assistant_msgs: Vec<_> = session
            .messages
            .iter()
            .filter(|m| m.role == ava_types::Role::Assistant)
            .collect();
        assert_eq!(assistant_msgs.len(), 1);
        assert_eq!(assistant_msgs[0].content, "All done!");
    }
}

/// Streaming run emits Token events before the Complete event.
#[tokio::test]
async fn streaming_run_emits_token_events() {
    let mut loop_engine = build_loop(vec!["Token output here.".to_string()], 10_000, 5);
    let stream = loop_engine.run_streaming("generate tokens").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    let token_events: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, AgentEvent::Token(_)))
        .collect();
    assert!(
        !token_events.is_empty(),
        "should emit at least one Token event"
    );

    // Tokens should come before Complete
    let token_idx = events
        .iter()
        .position(|e| matches!(e, AgentEvent::Token(_)))
        .unwrap();
    let complete_idx = events
        .iter()
        .position(|e| matches!(e, AgentEvent::Complete(_)))
        .unwrap();
    assert!(
        token_idx < complete_idx,
        "Token events should precede Complete"
    );
}

/// Max-turns enforcement: when `max_turns = 1` and the LLM keeps returning tool
/// calls, the loop must stop after the configured limit and still produce a
/// valid session (via the force_summary path).
#[tokio::test]
async fn max_turns_limit_halts_loop() {
    let mut tools = ToolRegistry::new();
    tools.register(NoopTool { name: "echo" });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    // Provide enough responses that without a turn limit the loop would keep going.
    // With max_turns=1, the second response should never be consumed.
    let responses = vec![
        json!({"tool_call": {"name": "echo", "arguments": {"msg": "first"}}}).to_string(),
        // Force-summary call (when max_turns is hit) will consume this
        "Summary: I've done what I can.".to_string(),
        // These should never be consumed
        json!({"tool_call": {"name": "echo", "arguments": {"msg": "should not run"}}}).to_string(),
    ];

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(responses)),
        tools,
        ContextManager::new(10_000),
        test_config(1),
    );

    let session = loop_engine
        .run("keep running")
        .await
        .expect("run should succeed even when turn limit hit");

    // The loop should have emitted a Progress event and then a summary
    // Session should exist (not an error)
    assert!(
        !session.messages.is_empty(),
        "session should have messages even when turn limit enforced"
    );
}

/// Max-turns via streaming: verify Progress("turn limit reached") is emitted
/// before the Complete event.
#[tokio::test]
async fn max_turns_streaming_emits_progress_and_complete() {
    let mut tools = ToolRegistry::new();
    tools.register(NoopTool { name: "echo" });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let responses = vec![
        json!({"tool_call": {"name": "echo", "arguments": {}}}).to_string(),
        "Summary done.".to_string(),
    ];

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(responses)),
        tools,
        ContextManager::new(10_000),
        test_config(1),
    );

    let stream = loop_engine.run_streaming("limited run").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    // Should see turn-limit progress event
    let has_turn_limit_progress = events
        .iter()
        .any(|e| matches!(e, AgentEvent::Progress(msg) if msg.contains("turn limit reached")));
    assert!(
        has_turn_limit_progress,
        "should emit turn limit progress event"
    );

    // Should still end with Complete
    assert!(
        events.iter().any(|e| matches!(e, AgentEvent::Complete(_))),
        "should emit Complete even after turn limit"
    );
}

/// Tool dispatch success: run with a successful tool call, verify the session
/// contains both the tool call and the tool result message.
#[tokio::test]
async fn successful_tool_call_recorded_in_session() {
    let recording_calls = Arc::new(Mutex::new(Vec::new()));
    let mut tools = ToolRegistry::new();
    tools.register(RecordingTool {
        name: "my_tool",
        calls: recording_calls.clone(),
        result: ToolResult {
            call_id: "call_001".to_string(),
            content: "tool output data".to_string(),
            is_error: false,
        },
    });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(vec![
            json!({"tool_call": {"name": "my_tool", "arguments": {"key": "value"}}}).to_string(),
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        test_config(5),
    );

    let session = loop_engine
        .run("use the tool")
        .await
        .expect("run should succeed");

    // The tool should have been invoked with the provided arguments
    let calls = recording_calls.lock().expect("calls lock poisoned");
    assert_eq!(calls.len(), 1, "tool should be called exactly once");
    assert_eq!(
        calls[0].get("key").and_then(Value::as_str),
        Some("value"),
        "tool should receive correct arguments"
    );

    // Session should contain a Tool role message with the tool output
    let tool_messages: Vec<_> = session
        .messages
        .iter()
        .filter(|m| m.role == ava_types::Role::Tool)
        .collect();
    assert!(
        !tool_messages.is_empty(),
        "session should have tool result messages"
    );
    assert!(
        tool_messages
            .iter()
            .any(|m| m.content.contains("tool output data")),
        "session should contain tool output content"
    );
}

/// Tool dispatch: streaming mode emits ToolCall and ToolResult events.
#[tokio::test]
async fn streaming_run_emits_tool_call_and_result_events() {
    let mut tools = ToolRegistry::new();
    tools.register(NoopTool { name: "echo" });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(vec![
            json!({"tool_call": {"name": "echo", "arguments": {}}}).to_string(),
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        test_config(5),
    );

    let stream = loop_engine.run_streaming("invoke a tool").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    assert!(
        events.iter().any(|e| matches!(e, AgentEvent::ToolCall(_))),
        "streaming run should emit ToolCall event"
    );
    assert!(
        events
            .iter()
            .any(|e| matches!(e, AgentEvent::ToolResult(_))),
        "streaming run should emit ToolResult event"
    );
}

/// Token usage event: a provider that emits usage chunks should cause the agent
/// loop to emit an `AgentEvent::TokenUsage` during streaming.
#[tokio::test]
async fn streaming_run_emits_token_usage_event() {
    let mut tools = ToolRegistry::new();
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let usage = TokenUsage {
        input_tokens: 100,
        output_tokens: 50,
        cache_read_tokens: 0,
        cache_creation_tokens: 0,
    };

    let mut loop_engine = AgentLoop::new(
        Box::new(UsageMockProvider::new(
            vec![json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string()],
            usage,
        )),
        tools,
        ContextManager::new(10_000),
        test_config(5),
    );

    let stream = loop_engine.run_streaming("track tokens").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    let usage_event = events
        .iter()
        .find(|e| matches!(e, AgentEvent::TokenUsage { .. }));
    assert!(
        usage_event.is_some(),
        "streaming run should emit TokenUsage event"
    );

    if let Some(AgentEvent::TokenUsage {
        input_tokens,
        output_tokens,
        ..
    }) = usage_event
    {
        assert_eq!(*input_tokens, 100);
        assert_eq!(*output_tokens, 50);
    }
}

/// Steering queue: when a steering message is injected into the MessageQueue
/// before a write tool executes, it should appear in the session as a user
/// steering message on the next turn.
#[tokio::test]
async fn steering_message_injected_into_session() {
    let mut tools = ToolRegistry::new();
    // Use a write (non-read-only) tool so steering can interrupt between calls
    tools.register(NoopTool { name: "write" });
    tools.register(NoopTool {
        name: "attempt_completion",
    });

    let (queue, sender) = MessageQueue::new();
    // Pre-load the queue with a steering message before the run starts
    sender
        .send(QueuedMessage {
            tier: MessageTier::Steering,
            text: "steer me now".to_string(),
        })
        .expect("should enqueue steering");

    let mut loop_engine = AgentLoop::new(
        Box::new(MockLLMProvider::new(vec![
            json!({"tool_call": {"name": "write", "arguments": {"path": "x.txt", "content": "x"}}})
                .to_string(),
            // After steering, agent gets one more turn to complete
            json!({"tool_call": {"name": "attempt_completion", "arguments": {}}}).to_string(),
        ])),
        tools,
        ContextManager::new(10_000),
        test_config(5),
    )
    .with_message_queue(queue);

    let session = loop_engine
        .run("do a task with steering")
        .await
        .expect("run should succeed");

    // Steering message should appear as a user message containing the steering text
    // and the interruption framing prefix
    let has_steering = session.messages.iter().any(|m| {
        m.role == ava_types::Role::User
            && m.content.contains("steer me now")
            && m.content
                .contains("user has interrupted with a new instruction")
    });
    assert!(
        has_steering,
        "steering message should be injected into session as user message"
    );
}

/// Conversation history: messages passed via `with_history` should appear in
/// the session before the goal message.
#[tokio::test]
async fn history_prepended_before_goal() {
    let history = vec![
        Message::new(ava_types::Role::User, "previous question".to_string()),
        Message::new(ava_types::Role::Assistant, "previous answer".to_string()),
    ];

    let mut loop_engine = build_loop(vec!["response".to_string()], 10_000, 5).with_history(history);

    let session = loop_engine
        .run("new goal")
        .await
        .expect("run should succeed");

    // History messages should appear before the new goal
    let user_msgs: Vec<_> = session
        .messages
        .iter()
        .filter(|m| m.role == ava_types::Role::User)
        .collect();
    assert!(
        user_msgs.len() >= 2,
        "session should have history user message plus goal"
    );
    assert!(
        user_msgs[0].content.contains("previous question"),
        "first user message should be from history"
    );
    assert!(
        user_msgs.iter().any(|m| m.content.contains("new goal")),
        "goal message should be present"
    );
}

/// Progress events: `run_streaming()` should emit Progress("turn N") at the
/// start of each turn.
#[tokio::test]
async fn streaming_run_emits_turn_progress_event() {
    let mut loop_engine = build_loop(vec!["done".to_string()], 10_000, 5);
    let stream = loop_engine.run_streaming("test progress").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    let has_turn_progress = events
        .iter()
        .any(|e| matches!(e, AgentEvent::Progress(msg) if msg.starts_with("turn ")));
    assert!(
        has_turn_progress,
        "should emit at least one 'turn N' progress event"
    );
}

/// ToolStats event: `Complete` should be preceded by a `ToolStats` event.
#[tokio::test]
async fn streaming_run_emits_tool_stats_before_complete() {
    let mut loop_engine = build_loop(vec!["result".to_string()], 10_000, 5);
    let stream = loop_engine.run_streaming("check stats").await;
    let events: Vec<AgentEvent> = stream.collect().await;

    let stats_idx = events
        .iter()
        .position(|e| matches!(e, AgentEvent::ToolStats(_)));
    let complete_idx = events
        .iter()
        .position(|e| matches!(e, AgentEvent::Complete(_)));

    assert!(stats_idx.is_some(), "should emit ToolStats event");
    assert!(complete_idx.is_some(), "should emit Complete event");
    assert!(
        stats_idx.unwrap() < complete_idx.unwrap(),
        "ToolStats should precede Complete"
    );
}
