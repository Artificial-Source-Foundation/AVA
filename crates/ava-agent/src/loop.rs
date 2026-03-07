use std::pin::Pin;
use std::time::Instant;

use ava_context::ContextManager;
use ava_tools::monitor::{hash_arguments, ToolExecution, ToolStats};
use ava_tools::registry::ToolRegistry;
use ava_types::{AvaError, Message, Result, Role, Session, ToolCall, ToolResult};
use futures::{Stream, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use uuid::Uuid;

use tracing::instrument;

use crate::llm_trait::LLMProvider;
use crate::stuck::{StuckAction, StuckDetector};
use crate::system_prompt::build_system_prompt;

const MAX_TOOL_RESULT_BYTES: usize = 50_000;

fn truncate_tool_result(result: &mut ToolResult) {
    if result.content.len() > MAX_TOOL_RESULT_BYTES {
        let original_len = result.content.len();
        let mut truncate_at = MAX_TOOL_RESULT_BYTES;
        while truncate_at > 0 && !result.content.is_char_boundary(truncate_at) {
            truncate_at -= 1;
        }
        result.content.truncate(truncate_at);
        result.content.push_str(&format!(
            "\n\n[truncated — showing first {} bytes of {} total]",
            truncate_at, original_len
        ));
    }
}

pub struct AgentLoop {
    pub llm: Box<dyn LLMProvider>,
    pub tools: ToolRegistry,
    pub context: ContextManager,
    pub config: AgentConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentConfig {
    pub max_turns: usize,
    pub token_limit: usize,
    pub model: String,
    #[serde(default = "default_max_cost")]
    pub max_cost_usd: f64,
    #[serde(default = "default_loop_detection")]
    pub loop_detection: bool,
}

fn default_max_cost() -> f64 {
    1.0
}

fn default_loop_detection() -> bool {
    true
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum AgentEvent {
    Token(String),
    ToolCall(ToolCall),
    ToolResult(ToolResult),
    Progress(String),
    Complete(Session),
    Error(String),
    ToolStats(ToolStats),
}

impl AgentLoop {
    pub fn new(
        llm: Box<dyn LLMProvider>,
        tools: ToolRegistry,
        context: ContextManager,
        config: AgentConfig,
    ) -> Self {
        Self {
            llm,
            tools,
            context,
            config,
        }
    }

    /// Inject the system prompt into the context before the first turn.
    fn inject_system_prompt(&mut self) {
        let native = self.llm.supports_tools();
        let tool_defs = self.tools.list_tools();
        let system = build_system_prompt(&tool_defs, native);
        self.context.add_message(Message::new(Role::System, system));
    }

    /// Generate a response, using native tool calling when the provider supports it.
    /// Returns (response_text, tool_calls).
    async fn generate_response(&self) -> Result<(String, Vec<ToolCall>)> {
        if self.llm.supports_tools() {
            let tool_defs = self.tools.list_tools();
            let response = self
                .llm
                .generate_with_tools(self.context.get_messages(), &tool_defs)
                .await?;
            Ok((response.content, response.tool_calls))
        } else {
            let response = self.llm.generate(self.context.get_messages()).await?;
            let tool_calls = parse_tool_calls(&response)?;
            Ok((response, tool_calls))
        }
    }

    #[instrument(skip(self), fields(model = %self.config.model))]
    pub async fn run(&mut self, goal: &str) -> Result<Session> {
        let mut session = Session::new();
        let mut detector = StuckDetector::new();

        self.inject_system_prompt();

        let goal_message = Message::new(Role::User, goal.to_string());
        self.context.add_message(goal_message.clone());
        session.add_message(goal_message);

        for _ in 0..self.config.max_turns {
            let (response_text, tool_calls) = self.generate_response().await?;

            let assistant_message = Message::new(Role::Assistant, response_text.clone())
                .with_tool_calls(tool_calls.clone());

            let tool_results = self.execute_tool_calls_tracked(&tool_calls, &mut detector).await;

            match detector.check(
                &response_text,
                &tool_calls,
                &tool_results,
                &self.config,
                self.llm.as_ref(),
            ) {
                StuckAction::Continue => {}
                StuckAction::InjectMessage(msg) => {
                    self.context.add_message(assistant_message.clone());
                    session.add_message(assistant_message);
                    self.add_tool_results(&tool_calls, &tool_results, &mut session);
                    let nudge = Message::new(Role::User, msg);
                    self.context.add_message(nudge.clone());
                    session.add_message(nudge);
                    continue;
                }
                StuckAction::Stop(reason) => {
                    session.add_message(Message::new(Role::System, reason));
                    break;
                }
            }

            // Skip adding empty responses to context
            if response_text.trim().is_empty() && tool_calls.is_empty() {
                continue;
            }

            self.context.add_message(assistant_message.clone());
            session.add_message(assistant_message);

            // Natural completion: non-empty text with no tool calls = final answer
            if tool_calls.is_empty() {
                return Ok(session);
            }

            let completion_requested = tool_calls.iter().any(|call| call.name == "attempt_completion");

            self.add_tool_results(&tool_calls, &tool_results, &mut session);

            if self.context.should_compact() {
                self.context.compact_async().await?;
            }

            if completion_requested {
                return Ok(session);
            }
        }

        Ok(session)
    }

    #[instrument(skip(self), fields(model = %self.config.model))]
    pub async fn run_streaming(
        &mut self,
        goal: &str,
    ) -> Pin<Box<dyn Stream<Item = AgentEvent> + Send + '_>> {
        let goal = goal.to_string();
        Box::pin(async_stream::stream! {
            let mut session = Session::new();
            let mut detector = StuckDetector::new();

            self.inject_system_prompt();

            let goal_message = Message::new(Role::User, goal.clone());
            self.context.add_message(goal_message.clone());
            session.add_message(goal_message);

            for turn in 0..self.config.max_turns {
                yield AgentEvent::Progress(format!("turn {}", turn + 1));

                let native_tools = self.llm.supports_tools();

                let (response_text, tool_calls) = if native_tools {
                    let tool_defs = self.tools.list_tools();
                    match self.llm.generate_with_tools(self.context.get_messages(), &tool_defs).await {
                        Ok(response) => {
                            if !response.content.is_empty() {
                                yield AgentEvent::Token(response.content.clone());
                            }
                            (response.content, response.tool_calls)
                        }
                        Err(error) => {
                            yield AgentEvent::Error(error.to_string());
                            return;
                        }
                    }
                } else {
                    let mut full_response = String::new();
                    let stream_result = self.llm.generate_stream(self.context.get_messages()).await;
                    match stream_result {
                        Ok(mut stream) => {
                            while let Some(chunk) = stream.next().await {
                                full_response.push_str(&chunk);
                                yield AgentEvent::Token(chunk);
                            }
                        }
                        Err(error) => {
                            yield AgentEvent::Error(error.to_string());
                            return;
                        }
                    }
                    let tool_calls = match parse_tool_calls(&full_response) {
                        Ok(calls) => calls,
                        Err(error) => {
                            yield AgentEvent::Error(error.to_string());
                            return;
                        }
                    };
                    (full_response, tool_calls)
                };

                // Execute tools, track in monitor, and collect results
                let mut tool_results_collected = Vec::new();
                for tool_call in &tool_calls {
                    if tool_call.name == "attempt_completion" {
                        continue;
                    }
                    yield AgentEvent::ToolCall(tool_call.clone());
                    let (result, execution) = self.execute_tool_call_timed(tool_call).await;
                    detector.tool_monitor_mut().record(execution);
                    tool_results_collected.push(result.clone());
                    yield AgentEvent::ToolResult(result);
                }

                // Stuck detection
                match detector.check(
                    &response_text,
                    &tool_calls,
                    &tool_results_collected,
                    &self.config,
                    self.llm.as_ref(),
                ) {
                    StuckAction::Continue => {}
                    StuckAction::InjectMessage(msg) => {
                        let assistant_message = Message::new(Role::Assistant, response_text.clone())
                            .with_tool_calls(tool_calls.clone());
                        self.context.add_message(assistant_message.clone());
                        session.add_message(assistant_message);
                        for (i, tool_call) in tool_calls.iter().enumerate() {
                            if tool_call.name == "attempt_completion" { continue; }
                            if let Some(result) = tool_results_collected.get(i) {
                                let tool_message = Message::new(Role::Tool, result.content.clone())
                                    .with_tool_call_id(&tool_call.id)
                                    .with_tool_results(vec![result.clone()]);
                                self.context.add_message(tool_message.clone());
                                session.add_message(tool_message);
                            }
                        }
                        yield AgentEvent::Progress(msg.clone());
                        let nudge = Message::new(Role::User, msg);
                        self.context.add_message(nudge.clone());
                        session.add_message(nudge);
                        continue;
                    }
                    StuckAction::Stop(reason) => {
                        yield AgentEvent::Progress(reason);
                        break;
                    }
                }

                // Skip adding empty responses to context
                if response_text.trim().is_empty() && tool_calls.is_empty() {
                    continue;
                }

                let assistant_message = Message::new(Role::Assistant, response_text.clone())
                    .with_tool_calls(tool_calls.clone());
                self.context.add_message(assistant_message.clone());
                session.add_message(assistant_message);

                // Natural completion: non-empty text with no tool calls = final answer
                if tool_calls.is_empty() {
                    yield AgentEvent::ToolStats(detector.tool_monitor().stats());
                    yield AgentEvent::Complete(session.clone());
                    return;
                }

                let completion_requested = tool_calls.iter().any(|call| call.name == "attempt_completion");

                // Add tool results to context
                for (i, tool_call) in tool_calls.iter().enumerate() {
                    if tool_call.name == "attempt_completion" { continue; }
                    if let Some(result) = tool_results_collected.get(i) {
                        let tool_message = Message::new(Role::Tool, result.content.clone())
                            .with_tool_call_id(&tool_call.id)
                            .with_tool_results(vec![result.clone()]);
                        self.context.add_message(tool_message.clone());
                        session.add_message(tool_message);
                    }
                }

                if self.context.should_compact() {
                    if let Err(error) = self.context.compact_async().await {
                        yield AgentEvent::Error(error.to_string());
                        return;
                    }
                    yield AgentEvent::Progress("context compacted".to_string());
                }

                if completion_requested {
                    yield AgentEvent::ToolStats(detector.tool_monitor().stats());
                    yield AgentEvent::Complete(session.clone());
                    return;
                }
            }

            yield AgentEvent::ToolStats(detector.tool_monitor().stats());
            yield AgentEvent::Progress("max turns reached".to_string());
            yield AgentEvent::Complete(session);
        })
    }

    /// Execute tool calls, record in detector's monitor, and collect results.
    async fn execute_tool_calls_tracked(
        &self,
        tool_calls: &[ToolCall],
        detector: &mut StuckDetector,
    ) -> Vec<ToolResult> {
        let mut results = Vec::new();
        for tool_call in tool_calls {
            if tool_call.name == "attempt_completion" {
                continue;
            }
            let (result, execution) = self.execute_tool_call_timed(tool_call).await;
            detector.tool_monitor_mut().record(execution);
            results.push(result);
        }
        results
    }

    /// Add tool results to context and session.
    fn add_tool_results(
        &mut self,
        tool_calls: &[ToolCall],
        results: &[ToolResult],
        session: &mut Session,
    ) {
        let mut ri = 0;
        for tool_call in tool_calls {
            if tool_call.name == "attempt_completion" {
                continue;
            }
            if let Some(result) = results.get(ri) {
                let tool_message = Message::new(Role::Tool, result.content.clone())
                    .with_tool_call_id(&tool_call.id)
                    .with_tool_results(vec![result.clone()]);
                self.context.add_message(tool_message.clone());
                session.add_message(tool_message);
            }
            ri += 1;
        }
    }

    /// Execute a tool call and return both the result and a timed execution record.
    async fn execute_tool_call_timed(&self, tool_call: &ToolCall) -> (ToolResult, ToolExecution) {
        let start = Instant::now();
        let mut result = match self.tools.execute(tool_call.clone()).await {
            Ok(result) => result,
            Err(error) => ToolResult {
                call_id: tool_call.id.clone(),
                content: error.to_string(),
                is_error: true,
            },
        };
        let duration = start.elapsed();
        let execution = ToolExecution {
            tool_name: tool_call.name.clone(),
            arguments_hash: hash_arguments(&tool_call.arguments),
            success: !result.is_error,
            duration,
            timestamp: start,
        };
        truncate_tool_result(&mut result);
        (result, execution)
    }
}

#[derive(Debug, Deserialize)]
struct ToolCallEnvelope {
    name: String,
    #[serde(default)]
    arguments: Value,
    #[serde(default)]
    id: Option<String>,
}

fn parse_tool_calls(content: &str) -> Result<Vec<ToolCall>> {
    let value = match serde_json::from_str::<Value>(content) {
        Ok(value) => value,
        Err(_) => return Ok(Vec::new()),
    };

    let calls = if let Some(raw_calls) = value.get("tool_calls") {
        serde_json::from_value::<Vec<ToolCallEnvelope>>(raw_calls.clone())
            .map_err(|error| AvaError::SerializationError(error.to_string()))?
    } else if let Some(raw_call) = value.get("tool_call") {
        vec![serde_json::from_value::<ToolCallEnvelope>(raw_call.clone())
            .map_err(|error| AvaError::SerializationError(error.to_string()))?]
    } else {
        Vec::new()
    };

    Ok(calls
        .into_iter()
        .map(|call| ToolCall {
            id: call.id.unwrap_or_else(|| Uuid::new_v4().to_string()),
            name: call.name,
            arguments: call.arguments,
        })
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stuck_detector_empty_responses() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd: 1.0,
            loop_detection: true,
        };
        let llm = crate::tests::mock_llm();

        // First empty: continue
        let action = detector.check("", &[], &[], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));

        // Second empty: stop
        let action = detector.check("", &[], &[], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_identical_responses() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd: 10.0,
            loop_detection: true,
        };
        let llm = crate::tests::mock_llm();

        for i in 0..2 {
            let action = detector.check("same response", &[], &[], &config, llm.as_ref());
            assert!(matches!(action, StuckAction::Continue), "iteration {i} should continue");
        }

        let action = detector.check("same response", &[], &[], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_tool_call_loop() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd: 10.0,
            loop_detection: true,
        };
        let llm = crate::tests::mock_llm();

        let call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };

        for i in 0..2 {
            let action = detector.check(&format!("reading {i}"), &[call.clone()], &[], &config, llm.as_ref());
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check("reading again", &[call.clone()], &[], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    #[test]
    fn stuck_detector_error_loop() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd: 10.0,
            loop_detection: true,
        };
        let llm = crate::tests::mock_llm();

        let error_result = ToolResult {
            call_id: "1".to_string(),
            content: "file not found".to_string(),
            is_error: true,
        };

        for i in 0..2 {
            let action = detector.check(&format!("trying {i}"), &[], &[error_result.clone()], &config, llm.as_ref());
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check("trying again", &[], &[error_result.clone()], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    #[test]
    fn stuck_detector_cost_threshold() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd: 0.0, // Zero threshold = immediate stop
            loop_detection: true,
        };
        let llm = crate::tests::mock_llm();

        let action = detector.check("hello", &[], &[], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_disabled() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd: 0.0,
            loop_detection: false,
        };
        let llm = crate::tests::mock_llm();

        // Would normally trigger cost stop, but detection is disabled
        let action = detector.check("hello", &[], &[], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));
    }
}
