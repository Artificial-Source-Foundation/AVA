mod response;
mod tool_execution;

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::pin::Pin;
use std::time::Instant;

use ava_context::ContextManager;
use ava_tools::monitor::ToolExecution;
use ava_tools::registry::ToolRegistry;
use ava_types::{Message, Role, Session, ThinkingLevel, ToolCall, ToolResult};
use futures::{Stream, StreamExt};
use serde::{Deserialize, Serialize};
use tracing::{debug, info, instrument, warn};

use crate::llm_trait::LLMProvider;
use crate::stuck::{StuckAction, StuckDetector};
use crate::system_prompt::build_system_prompt;

pub use tool_execution::READ_ONLY_TOOLS;
use response::parse_tool_calls;

/// Core agent execution loop that orchestrates LLM calls, tool execution, and stuck detection.
///
/// Supports both synchronous (`run`) and streaming (`run_streaming`) execution modes.
/// Read-only tools are executed concurrently; write tools run sequentially.
pub struct AgentLoop {
    pub llm: Box<dyn LLMProvider>,
    pub tools: ToolRegistry,
    pub context: ContextManager,
    pub config: AgentConfig,
    pub(crate) last_request_hash: Option<u64>,
    pub(crate) last_request_time: Option<Instant>,
    /// Conversation history from previous turns (injected after system prompt, before goal).
    history: Vec<Message>,
}

/// Configuration for a single agent loop run — turn limits, cost caps, and model identity.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentConfig {
    pub max_turns: usize,
    pub token_limit: usize,
    pub model: String,
    #[serde(default = "default_max_cost")]
    pub max_cost_usd: f64,
    #[serde(default = "default_loop_detection")]
    pub loop_detection: bool,
    /// Optional override for the system prompt. When set, replaces the default system prompt.
    #[serde(default)]
    pub custom_system_prompt: Option<String>,
    /// Thinking/reasoning level for models that support extended thinking.
    #[serde(default)]
    pub thinking_level: ThinkingLevel,
}

fn default_max_cost() -> f64 {
    1.0
}

fn default_loop_detection() -> bool {
    true
}

/// Events emitted during streaming agent execution for UI consumption.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum AgentEvent {
    Token(String),
    /// Thinking/reasoning content from the model (displayed separately in UI).
    Thinking(String),
    ToolCall(ToolCall),
    ToolResult(ToolResult),
    Progress(String),
    Complete(Session),
    Error(String),
    ToolStats(ava_tools::monitor::ToolStats),
    TokenUsage {
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
    },
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
            last_request_hash: None,
            last_request_time: None,
            history: Vec::new(),
        }
    }

    /// Set conversation history to inject after the system prompt.
    pub fn with_history(mut self, history: Vec<Message>) -> Self {
        self.history = history;
        self
    }

    /// Inject the system prompt into the context before the first turn.
    fn inject_system_prompt(&mut self) {
        let system = if let Some(ref custom) = self.config.custom_system_prompt {
            custom.clone()
        } else {
            let native = self.llm.supports_tools();
            let tool_defs = self.tools.list_tools();
            build_system_prompt(&tool_defs, native)
        };
        self.context.add_message(Message::new(Role::System, system));
    }

    #[instrument(skip(self), fields(model = %self.config.model))]
    pub async fn run(&mut self, goal: &str) -> ava_types::Result<Session> {
        let mut session = Session::new();
        let mut detector = StuckDetector::new();

        self.inject_system_prompt();

        // Inject conversation history from previous turns
        for msg in std::mem::take(&mut self.history) {
            self.context.add_message(msg.clone());
            session.add_message(msg);
        }

        let goal_message = Message::new(Role::User, goal.to_string());
        self.context.add_message(goal_message.clone());
        session.add_message(goal_message);

        for _ in 0..self.config.max_turns {
            let (response_text, tool_calls, _usage) = self.generate_response_with_thinking().await?;

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

            let completion_requested =
                tool_calls.iter().any(|call| call.name == "attempt_completion");

            self.add_tool_results(&tool_calls, &tool_results, &mut session);

            // Inject a self-correction hint for the first error (avoids flooding)
            if let Some(err_result) = tool_results.iter().find(|r| r.is_error) {
                let first_line = err_result.content.lines().next().unwrap_or("unknown error");
                let hint = format!(
                    "Tool call failed: {first_line}. Try a different approach — don't repeat the same call."
                );
                let hint_msg = Message::new(Role::User, hint);
                self.context.add_message(hint_msg.clone());
                session.add_message(hint_msg);
            }

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

            // Inject conversation history from previous turns
            for msg in std::mem::take(&mut self.history) {
                self.context.add_message(msg.clone());
                session.add_message(msg);
            }

            let goal_message = Message::new(Role::User, goal.clone());
            self.context.add_message(goal_message.clone());
            session.add_message(goal_message);

            for turn in 0..self.config.max_turns {
                yield AgentEvent::Progress(format!("turn {}", turn + 1));

                let native_tools = self.llm.supports_tools();

                // Dedup guard
                let dedup_hash = {
                    let msgs = self.context.get_messages();
                    let mut hasher = DefaultHasher::new();
                    if let Some(last) = msgs.last() {
                        last.content.hash(&mut hasher);
                    }
                    hasher.finish()
                };
                if let (Some(prev_hash), Some(prev_time)) = (self.last_request_hash, self.last_request_time) {
                    if dedup_hash == prev_hash && prev_time.elapsed().as_secs() < 2 {
                        warn!("Skipping duplicate request (same content within 2s)");
                        continue;
                    }
                }

                let (response_text, tool_calls) = {
                    info!(
                        model = %self.config.model,
                        turn = turn + 1,
                        native_tools,
                        thinking = %self.config.thinking_level,
                        messages = self.context.get_messages().len(),
                        "starting LLM stream request"
                    );
                    let stream_result = if native_tools {
                        let tool_defs = self.tools.list_tools();
                        if self.config.thinking_level != ThinkingLevel::Off {
                            self.llm.generate_stream_with_thinking(
                                self.context.get_messages(), &tool_defs, self.config.thinking_level,
                            ).await
                        } else {
                            self.llm.generate_stream_with_tools(
                                self.context.get_messages(), &tool_defs,
                            ).await
                        }
                    } else {
                        self.llm.generate_stream(self.context.get_messages()).await
                    };

                    match stream_result {
                        Ok(mut stream) => {
                            let mut full_text = String::new();
                            let mut accumulated_tool_calls: Vec<response::ToolCallAccumulator> = Vec::new();
                            let mut last_usage: Option<ava_types::TokenUsage> = None;
                            let mut chunk_count: usize = 0;

                            while let Some(chunk) = stream.next().await {
                                chunk_count += 1;
                                debug!(
                                    chunk_count,
                                    has_content = chunk.content.is_some(),
                                    has_thinking = chunk.thinking.is_some(),
                                    has_tool_call = chunk.tool_call.is_some(),
                                    has_usage = chunk.usage.is_some(),
                                    done = chunk.done,
                                    "stream chunk received"
                                );
                                // Emit text tokens as they arrive
                                if let Some(text) = chunk.text_content() {
                                    full_text.push_str(text);
                                    yield AgentEvent::Token(text.to_string());
                                }
                                // Emit thinking
                                if let Some(ref thinking) = chunk.thinking {
                                    if !thinking.is_empty() {
                                        yield AgentEvent::Thinking(thinking.clone());
                                    }
                                }
                                // Accumulate tool call fragments
                                if let Some(ref tc) = chunk.tool_call {
                                    response::accumulate_tool_call(&mut accumulated_tool_calls, tc);
                                }
                                // Capture usage (may arrive in message_start and message_delta)
                                if let Some(ref usage) = chunk.usage {
                                    if let Some(ref mut existing) = last_usage {
                                        // Merge: Anthropic sends input in message_start, output in message_delta
                                        if usage.input_tokens > 0 {
                                            existing.input_tokens = usage.input_tokens;
                                        }
                                        if usage.output_tokens > 0 {
                                            existing.output_tokens = usage.output_tokens;
                                        }
                                    } else {
                                        last_usage = Some(usage.clone());
                                    }
                                }
                            }

                            info!(
                                chunk_count,
                                text_len = full_text.len(),
                                tool_calls = accumulated_tool_calls.len(),
                                has_usage = last_usage.is_some(),
                                "stream completed"
                            );

                            // Emit token usage
                            if let Some(usage) = last_usage {
                                let (in_rate, out_rate) = ava_llm::providers::common::model_pricing_usd_per_million(&self.config.model);
                                let cost = ava_llm::providers::common::estimate_cost_usd(
                                    usage.input_tokens, usage.output_tokens, in_rate, out_rate,
                                );
                                yield AgentEvent::TokenUsage {
                                    input_tokens: usage.input_tokens,
                                    output_tokens: usage.output_tokens,
                                    cost_usd: cost,
                                };
                            }

                            // Convert accumulated tool calls or parse from text
                            let tool_calls = if native_tools && !accumulated_tool_calls.is_empty() {
                                response::finalize_tool_calls(accumulated_tool_calls)
                            } else if !native_tools {
                                parse_tool_calls(&full_text).unwrap_or_default()
                            } else {
                                vec![]
                            };

                            (full_text, tool_calls)
                        }
                        Err(error) => {
                            yield AgentEvent::Error(error.to_string());
                            return;
                        }
                    }
                };

                self.last_request_hash = Some(dedup_hash);
                self.last_request_time = Some(Instant::now());

                // Execute tools: read-only in parallel, write sequentially
                let mut tool_results_collected = Vec::new();
                {
                    let mut read_calls: Vec<(usize, &ToolCall)> = Vec::new();
                    let mut write_calls: Vec<(usize, &ToolCall)> = Vec::new();
                    for (i, tc) in tool_calls.iter().enumerate() {
                        if tc.name == "attempt_completion" { continue; }
                        if tool_execution::READ_ONLY_TOOLS.contains(&tc.name.as_str()) {
                            read_calls.push((i, tc));
                        } else {
                            write_calls.push((i, tc));
                        }
                    }

                    // Emit all ToolCall events first
                    for tc in &tool_calls {
                        if tc.name == "attempt_completion" { continue; }
                        yield AgentEvent::ToolCall(tc.clone());
                    }

                    let mut indexed_results: Vec<(usize, ToolResult, ToolExecution)> = Vec::new();

                    // Read-only tools concurrently
                    if !read_calls.is_empty() {
                        let futs: Vec<_> = read_calls.iter().map(|(_, tc)| self.execute_tool_call_timed(tc)).collect();
                        let results = futures::future::join_all(futs).await;
                        for (pos, (i, _)) in read_calls.iter().enumerate() {
                            let (result, execution) = results[pos].clone();
                            indexed_results.push((*i, result, execution));
                        }
                    }

                    // Write tools sequentially
                    for (i, tc) in &write_calls {
                        let (result, execution) = self.execute_tool_call_timed(tc).await;
                        indexed_results.push((*i, result, execution));
                    }

                    // Sort by original index and emit results
                    indexed_results.sort_by_key(|(i, _, _)| *i);
                    for (_, result, execution) in indexed_results {
                        detector.tool_monitor_mut().record(execution);
                        tool_results_collected.push(result.clone());
                        yield AgentEvent::ToolResult(result);
                    }
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

                // Skip adding empty responses to context — but don't set dedup
                // hash so the next turn will still make a real API call instead
                // of silently burning turns via the dedup guard.
                if response_text.trim().is_empty() && tool_calls.is_empty() {
                    let msg = format!(
                        "Provider returned empty response (model: {}, turn {}). \
                         Possible API format mismatch. Run with RUST_LOG=debug for details.",
                        self.config.model, turn + 1
                    );
                    warn!("{msg}");
                    yield AgentEvent::Error(msg);
                    self.last_request_hash = None;
                    self.last_request_time = None;
                    break;
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

                // Inject a self-correction hint for the first error
                if let Some(err_result) = tool_results_collected.iter().find(|r| r.is_error) {
                    let first_line = err_result.content.lines().next().unwrap_or("unknown error");
                    let hint = format!(
                        "Tool call failed: {first_line}. Try a different approach — don't repeat the same call."
                    );
                    let hint_msg = Message::new(Role::User, hint);
                    self.context.add_message(hint_msg.clone());
                    session.add_message(hint_msg);
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
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};

    use crate::stuck::StuckAction;

    #[test]
    fn stuck_detector_empty_responses() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd: 1.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
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
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
        };
        let llm = crate::tests::mock_llm();

        for i in 0..2 {
            let action = detector.check("same response", &[], &[], &config, llm.as_ref());
            assert!(
                matches!(action, StuckAction::Continue),
                "iteration {i} should continue"
            );
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
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
        };
        let llm = crate::tests::mock_llm();

        let call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };

        for i in 0..2 {
            let action = detector.check(
                &format!("reading {i}"),
                &[call.clone()],
                &[],
                &config,
                llm.as_ref(),
            );
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
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
        };
        let llm = crate::tests::mock_llm();

        let error_result = ToolResult {
            call_id: "1".to_string(),
            content: "file not found".to_string(),
            is_error: true,
        };

        for i in 0..2 {
            let action = detector.check(
                &format!("trying {i}"),
                &[],
                &[error_result.clone()],
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check(
            "trying again",
            &[],
            &[error_result.clone()],
            &config,
            llm.as_ref(),
        );
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
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
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
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
        };
        let llm = crate::tests::mock_llm();

        // Would normally trigger cost stop, but detection is disabled
        let action = detector.check("hello", &[], &[], &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));
    }

    #[test]
    fn read_only_tools_constant_is_populated() {
        assert!(!READ_ONLY_TOOLS.is_empty());
        assert!(READ_ONLY_TOOLS.contains(&"read"));
        assert!(READ_ONLY_TOOLS.contains(&"glob"));
        assert!(READ_ONLY_TOOLS.contains(&"grep"));
        // Write tools should NOT be in the list
        assert!(!READ_ONLY_TOOLS.contains(&"write"));
        assert!(!READ_ONLY_TOOLS.contains(&"bash"));
        assert!(!READ_ONLY_TOOLS.contains(&"edit"));
    }

    #[test]
    fn dedup_guard_skips_rapid_duplicate() {
        // Verify the hash mechanism works deterministically
        let mut hasher1 = DefaultHasher::new();
        "same content".hash(&mut hasher1);
        let h1 = hasher1.finish();

        let mut hasher2 = DefaultHasher::new();
        "same content".hash(&mut hasher2);
        let h2 = hasher2.finish();

        assert_eq!(h1, h2, "same content should produce same hash");

        let mut hasher3 = DefaultHasher::new();
        "different content".hash(&mut hasher3);
        let h3 = hasher3.finish();

        assert_ne!(h1, h3, "different content should produce different hash");
    }

    #[test]
    fn token_usage_event_serializes() {
        let event = AgentEvent::TokenUsage {
            input_tokens: 1000,
            output_tokens: 200,
            cost_usd: 0.015,
        };
        let json = serde_json::to_string(&event).unwrap();
        assert!(json.contains("1000"));
        assert!(json.contains("200"));
        assert!(json.contains("0.015"));
    }
}
