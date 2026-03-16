mod response;
mod tool_execution;

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::pin::Pin;
use std::time::Instant;

use ava_context::ContextManager;
use ava_llm::ThinkingConfig;
use ava_tools::monitor::ToolExecution;
use ava_tools::registry::ToolRegistry;
use ava_types::{
    ImageContent, Message, Role, Session, ThinkingLevel, TokenUsage, ToolCall, ToolResult,
};
use futures::{Stream, StreamExt};
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tracing::{debug, info, instrument, trace, warn};

use crate::llm_trait::LLMProvider;
use crate::message_queue::MessageQueue;
use crate::stuck::{StuckAction, StuckDetector};
use crate::system_prompt::build_system_prompt;

use response::parse_tool_calls;
use tool_execution::has_validation_failure;
pub use tool_execution::READ_ONLY_TOOLS;

fn usage_cost_usd(model: &str, usage: &TokenUsage) -> f64 {
    let (in_rate, out_rate) = ava_llm::providers::common::model_pricing_usd_per_million(model);
    ava_llm::providers::common::estimate_cost_with_cache_usd(usage, in_rate, out_rate)
}

/// Hard cap on total tool calls per agent turn to prevent runaway tool invocations.
const MAX_TOOLS_PER_TURN: usize = 50;

/// Core agent execution loop that orchestrates LLM calls, tool execution, and stuck detection.
///
/// Uses a single unified engine (`run_unified`) for both headless and streaming modes.
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
    /// Optional message queue for mid-stream user messaging (steering, follow-up, post-complete).
    pub message_queue: Option<MessageQueue>,
    /// Images to attach to the first user (goal) message.
    images: Vec<ImageContent>,
}

/// Configuration for a single agent loop run — turn limits, cost caps, and model identity.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentConfig {
    /// Maximum number of turns (0 = unlimited).
    pub max_turns: usize,
    /// Maximum budget in USD (0 = unlimited). CLI-level cost cap.
    #[serde(default)]
    pub max_budget_usd: f64,
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
    /// Optional quantitative cap for providers that support explicit thinking budgets.
    #[serde(default)]
    pub thinking_budget_tokens: Option<u32>,
    /// Optional suffix appended to the system prompt (e.g., mode-specific instructions).
    #[serde(default)]
    pub system_prompt_suffix: Option<String>,
    /// When true, include extended-tier tools in the system prompt alongside
    /// default tools. Extended tools are always *executable* regardless of this flag.
    #[serde(default)]
    pub extended_tools: bool,
    /// When true, restrict write/edit tools to `.ava/plans/*.md` paths only (Plan mode).
    #[serde(default)]
    pub plan_mode: bool,
    /// Optional post-edit validation steps run after successful write/edit tools.
    #[serde(default)]
    pub post_edit_validation: Option<PostEditValidationConfig>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct PostEditValidationConfig {
    /// Run the existing extended-tier `lint` tool after successful edits.
    #[serde(default)]
    pub lint: bool,
    /// Run the existing extended-tier `test_runner` tool after successful edits.
    #[serde(default)]
    pub tests: bool,
    /// Optional custom lint command passed through to the lint tool.
    #[serde(default)]
    pub lint_command: Option<String>,
    /// Optional custom test command passed through to the test_runner tool.
    #[serde(default)]
    pub test_command: Option<String>,
    /// Timeout in seconds for post-edit test execution.
    #[serde(default = "default_post_edit_test_timeout_secs")]
    pub test_timeout_secs: u64,
}

impl PostEditValidationConfig {
    pub fn enabled(&self) -> bool {
        self.lint || self.tests
    }
}

fn default_max_cost() -> f64 {
    1.0
}

fn default_loop_detection() -> bool {
    true
}

fn default_post_edit_test_timeout_secs() -> u64 {
    60
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
    BudgetWarning {
        threshold_percent: u8,
        current_cost_usd: f64,
        max_budget_usd: f64,
    },
    /// A sub-agent has completed its run. Contains the full conversation for
    /// display/storage by the TUI.
    SubAgentComplete {
        /// The tool call ID that triggered this sub-agent.
        call_id: String,
        /// The sub-agent's session ID (persisted in the session store).
        session_id: String,
        /// The sub-agent's full conversation messages.
        messages: Vec<Message>,
        /// The task description/prompt given to the sub-agent.
        description: String,
        /// Total input tokens consumed by the sub-agent.
        input_tokens: usize,
        /// Total output tokens consumed by the sub-agent.
        output_tokens: usize,
        /// Estimated cost in USD for the sub-agent's LLM calls.
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
            message_queue: None,
            images: Vec::new(),
        }
    }

    /// Set conversation history to inject after the system prompt.
    pub fn with_history(mut self, history: Vec<Message>) -> Self {
        self.history = history;
        self
    }

    /// Attach a message queue for mid-stream user messaging.
    pub fn with_message_queue(mut self, queue: MessageQueue) -> Self {
        self.message_queue = Some(queue);
        self
    }

    /// Attach images to the first user (goal) message for multimodal input.
    pub fn with_images(mut self, images: Vec<ImageContent>) -> Self {
        self.images = images;
        self
    }

    /// Merge token usage from a single turn into a running total.
    fn merge_usage(total: &mut TokenUsage, usage: &Option<TokenUsage>) {
        if let Some(u) = usage {
            total.input_tokens += u.input_tokens;
            total.output_tokens += u.output_tokens;
            total.cache_read_tokens += u.cache_read_tokens;
            total.cache_creation_tokens += u.cache_creation_tokens;
        }
    }

    /// Send an event to the optional event channel. No-op when headless.
    fn emit(event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>, event: AgentEvent) {
        if let Some(tx) = event_tx {
            let _ = tx.send(event);
        }
    }

    /// Inject the system prompt into the context before the first turn.
    /// Idempotent: calling this multiple times (e.g., follow-up runs) is safe.
    fn inject_system_prompt(&mut self) {
        // Skip if system prompt already present (follow-up / post-complete reuse the context)
        if self
            .context
            .get_messages()
            .iter()
            .any(|m| m.role == Role::System)
        {
            return;
        }
        let mut system = if let Some(ref custom) = self.config.custom_system_prompt {
            custom.clone()
        } else {
            let native = self.llm.supports_tools();
            let tool_defs = self.active_tool_defs();
            build_system_prompt(&tool_defs, native)
        };
        if let Some(ref suffix) = self.config.system_prompt_suffix {
            system.push_str("\n\n");
            system.push_str(suffix);
        }
        self.context.add_message(Message::new(Role::System, system));
    }

    /// Inject a summary prompt and do one final LLM call so the agent can wrap up.
    async fn force_summary(
        &mut self,
        session: &mut Session,
        prompt: &str,
        total_usage: &mut TokenUsage,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) {
        let summary_msg = Message::new(Role::User, prompt.to_string());
        self.context.add_message(summary_msg.clone());
        session.add_message(summary_msg);
        Self::emit(
            event_tx,
            AgentEvent::Progress(if prompt.contains("budget") {
                "budget limit reached — requesting summary".to_string()
            } else {
                "turn limit reached — requesting summary".to_string()
            }),
        );
        if let Ok((text, _, usage)) = self.generate_response_with_thinking().await {
            Self::merge_usage(total_usage, &usage);
            if !text.trim().is_empty() {
                Self::emit(event_tx, AgentEvent::Token(text.clone()));
                let msg = Message::new(Role::Assistant, text);
                self.context.add_message(msg.clone());
                session.add_message(msg);
            }
        }
    }

    /// Generate LLM response — streaming when `event_tx` is present, non-streaming otherwise.
    ///
    /// Returns (response_text, tool_calls, usage). When streaming, tokens and thinking
    /// events are emitted to `event_tx` as they arrive.
    async fn generate_turn_response(
        &mut self,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        let native_tools = self.llm.supports_tools();

        // Dedup guard — hash last message content + message count so context growth
        // (new tool results) breaks the dedup even if the last message content is
        // identical (e.g. same compile error).
        let dedup_hash = {
            let msgs = self.context.get_messages();
            let mut hasher = DefaultHasher::new();
            msgs.len().hash(&mut hasher);
            if let Some(last) = msgs.last() {
                last.content.hash(&mut hasher);
            }
            hasher.finish()
        };
        if let (Some(prev_hash), Some(prev_time)) = (self.last_request_hash, self.last_request_time)
        {
            if dedup_hash == prev_hash && prev_time.elapsed().as_secs() < 2 {
                warn!("Skipping duplicate request (same content within 2s)");
                return Ok((String::new(), vec![], None));
            }
        }

        let streaming = event_tx.is_some();

        let result = if streaming {
            self.generate_turn_streaming(native_tools, event_tx).await
        } else {
            self.generate_turn_non_streaming().await
        };

        // Set dedup hash on success with non-empty response
        match &result {
            Ok((text, calls, _)) => {
                if !text.trim().is_empty() || !calls.is_empty() {
                    self.last_request_hash = Some(dedup_hash);
                    self.last_request_time = Some(Instant::now());
                }
            }
            Err(_) => {
                self.last_request_hash = Some(dedup_hash);
                self.last_request_time = Some(Instant::now());
            }
        }

        result
    }

    /// Streaming LLM call: emits Token, Thinking, and TokenUsage events.
    async fn generate_turn_streaming(
        &mut self,
        native_tools: bool,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        info!(
            model = %self.config.model,
            native_tools,
            thinking = %self.config.thinking_level,
            messages = self.context.get_messages().len(),
            "starting LLM stream request"
        );

        let stream_result = if native_tools {
            let tool_defs = self.active_tool_defs();
            if self.config.thinking_level != ThinkingLevel::Off {
                let thinking = ThinkingConfig::new(
                    self.config.thinking_level,
                    self.config.thinking_budget_tokens,
                );
                let resolved = self.llm.resolve_thinking_config(thinking);
                if let Some(fallback) = resolved.fallback {
                    warn!(
                        requested_budget = thinking.budget_tokens,
                        applied_budget = resolved.applied.budget_tokens,
                        ?fallback,
                        model = %self.config.model,
                        "provider could not fully honor requested thinking budget"
                    );
                }
                self.llm
                    .generate_stream_with_thinking_config(
                        self.context.get_messages(),
                        &tool_defs,
                        thinking,
                    )
                    .await
            } else {
                self.llm
                    .generate_stream_with_tools(self.context.get_messages(), &tool_defs)
                    .await
            }
        } else {
            self.llm.generate_stream(self.context.get_messages()).await
        };

        let mut stream = stream_result?;
        let mut full_text = String::new();
        let mut accumulated_tool_calls: Vec<response::ToolCallAccumulator> = Vec::new();
        let mut last_usage: Option<TokenUsage> = None;
        let mut chunk_count: usize = 0;

        while let Some(chunk) = stream.next().await {
            chunk_count += 1;
            trace!(
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
                Self::emit(event_tx, AgentEvent::Token(text.to_string()));
            }
            // Emit thinking
            if let Some(ref thinking) = chunk.thinking {
                if !thinking.is_empty() {
                    Self::emit(event_tx, AgentEvent::Thinking(thinking.clone()));
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
                    if usage.cache_read_tokens > 0 {
                        existing.cache_read_tokens = usage.cache_read_tokens;
                    }
                    if usage.cache_creation_tokens > 0 {
                        existing.cache_creation_tokens = usage.cache_creation_tokens;
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

        // Convert accumulated tool calls or parse from text
        let tool_calls = if native_tools && !accumulated_tool_calls.is_empty() {
            response::finalize_tool_calls(accumulated_tool_calls)
        } else if !native_tools {
            parse_tool_calls(&full_text).unwrap_or_default()
        } else {
            vec![]
        };

        Ok((full_text, tool_calls, last_usage))
    }

    /// Non-streaming LLM call (used by headless mode).
    async fn generate_turn_non_streaming(
        &mut self,
    ) -> ava_types::Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        self.generate_response_with_thinking().await
    }

    /// Execute tool calls with steering support and event emission.
    ///
    /// Returns (tool_results, steering_triggered).
    async fn execute_tools_unified(
        &mut self,
        tool_calls: &[ToolCall],
        detector: &mut StuckDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> (Vec<ToolResult>, bool) {
        let mut steering_triggered = false;

        // Guard against runaway tool invocations from a single LLM response
        if tool_calls.len() > MAX_TOOLS_PER_TURN {
            warn!(
                requested = tool_calls.len(),
                limit = MAX_TOOLS_PER_TURN,
                "Tool call limit reached, truncating to {MAX_TOOLS_PER_TURN}"
            );
        }
        let capped_calls = &tool_calls[..tool_calls.len().min(MAX_TOOLS_PER_TURN)];

        // Separate into read-only and write groups
        let mut read_calls: Vec<(usize, &ToolCall)> = Vec::new();
        let mut write_calls: Vec<(usize, &ToolCall)> = Vec::new();
        for (i, tc) in capped_calls.iter().enumerate() {
            if tc.name == "attempt_completion" {
                continue;
            }
            if tool_execution::READ_ONLY_TOOLS.contains(&tc.name.as_str()) {
                read_calls.push((i, tc));
            } else {
                write_calls.push((i, tc));
            }
        }

        // Emit all ToolCall events first (streaming mode)
        for tc in capped_calls {
            if tc.name == "attempt_completion" {
                continue;
            }
            Self::emit(event_tx, AgentEvent::ToolCall(tc.clone()));
        }

        let mut indexed_results: Vec<(usize, ToolResult, ToolExecution)> = Vec::new();

        // Read-only tools concurrently
        if !read_calls.is_empty() {
            let futs: Vec<_> = read_calls
                .iter()
                .map(|(_, tc)| self.execute_tool_call_timed(tc))
                .collect();
            let results = futures::future::join_all(futs).await;
            for (pos, (i, _)) in read_calls.iter().enumerate() {
                let (result, execution) = results[pos].clone();
                indexed_results.push((*i, result, execution));
            }
        }

        // Poll for steering after read-only batch
        if let Some(ref mut queue) = self.message_queue {
            queue.poll();
            if queue.has_steering() {
                steering_triggered = true;
            }
        }

        // Write tools sequentially — check steering between each
        if !steering_triggered {
            for (i, tc) in &write_calls {
                let (result, execution) = self.execute_tool_call_timed(tc).await;
                indexed_results.push((*i, result, execution));

                // Poll for steering after each write tool
                if let Some(ref mut queue) = self.message_queue {
                    queue.poll();
                    if queue.has_steering() {
                        steering_triggered = true;
                        break;
                    }
                }
            }
        }

        // If steering was triggered, add skip results for remaining write tools
        if steering_triggered {
            let executed_indices: std::collections::HashSet<usize> =
                indexed_results.iter().map(|(i, _, _)| *i).collect();
            for (i, tc) in &write_calls {
                if !executed_indices.contains(i) {
                    let skip_result = ToolResult {
                        call_id: tc.id.clone(),
                        content: "Skipped due to steering message.".to_string(),
                        is_error: true,
                    };
                    let execution = ToolExecution {
                        tool_name: tc.name.clone(),
                        arguments_hash: ava_tools::monitor::hash_arguments(&tc.arguments),
                        success: false,
                        duration: std::time::Duration::ZERO,
                        timestamp: Instant::now(),
                    };
                    indexed_results.push((*i, skip_result, execution));
                }
            }
        }

        // Sort by original index and collect results
        indexed_results.sort_by_key(|(i, _, _)| *i);
        let mut tool_results = Vec::new();
        for (_, result, execution) in indexed_results {
            detector.tool_monitor_mut().record(execution);
            tool_results.push(result.clone());
            Self::emit(event_tx, AgentEvent::ToolResult(result));
        }

        (tool_results, steering_triggered)
    }

    /// Unified agent execution engine. Both `run()` (headless) and `run_streaming()`
    /// delegate to this method. When `event_tx` is `Some`, streaming events (Token,
    /// Thinking, ToolCall, ToolResult, Progress, Complete, etc.) are emitted. When
    /// `None`, execution is silent.
    async fn run_unified(
        &mut self,
        goal: &str,
        event_tx: Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<Session> {
        let mut session = Session::new();
        let mut detector = StuckDetector::new();
        let mut total_usage = TokenUsage::default();
        let mut total_cost_usd = 0.0;

        // --- Setup ---
        self.inject_system_prompt();

        // Inject conversation history from previous turns
        for msg in std::mem::take(&mut self.history) {
            self.context.add_message(msg.clone());
            session.add_message(msg);
        }

        let goal_images = std::mem::take(&mut self.images);
        let goal_message = if goal_images.is_empty() {
            Message::new(Role::User, goal.to_string())
        } else {
            Message::new(Role::User, goal.to_string()).with_images(goal_images)
        };
        self.context.add_message(goal_message.clone());
        session.add_message(goal_message);

        // max_turns == 0 means unlimited; use a very large sentinel
        let effective_max = if self.config.max_turns == 0 {
            usize::MAX
        } else {
            self.config.max_turns
        };
        let max_budget = self.config.max_budget_usd;
        let mut turn: usize = 0;

        // --- Main loop ---
        loop {
            // Check turn limit
            if self.config.max_turns > 0 && turn >= effective_max {
                self.force_summary(
                    &mut session,
                    &format!(
                        "You have reached the maximum number of turns ({}). Please summarize what you've accomplished and list any remaining work.",
                        self.config.max_turns,
                    ),
                    &mut total_usage,
                    &event_tx,
                ).await;
                break;
            }

            // Check budget limit
            if max_budget > 0.0 && total_cost_usd >= max_budget {
                self.force_summary(
                    &mut session,
                    &format!(
                        "You have reached the budget limit (${:.2}). Please summarize what you've accomplished and list any remaining work.",
                        max_budget,
                    ),
                    &mut total_usage,
                    &event_tx,
                ).await;
                break;
            }

            turn += 1;
            Self::emit(&event_tx, AgentEvent::Progress(format!("turn {turn}")));

            // --- Generate LLM response ---
            let (response_text, tool_calls, usage) =
                match self.generate_turn_response(&event_tx).await {
                    Ok(result) => result,
                    Err(error) => {
                        Self::emit(&event_tx, AgentEvent::Error(error.to_string()));
                        return Err(error);
                    }
                };

            Self::merge_usage(&mut total_usage, &usage);
            if let Some(ref usage) = usage {
                let cost = usage_cost_usd(&self.config.model, usage);
                total_cost_usd += cost;
                Self::emit(
                    &event_tx,
                    AgentEvent::TokenUsage {
                        input_tokens: usage.input_tokens,
                        output_tokens: usage.output_tokens,
                        cost_usd: cost,
                    },
                );
            }

            // --- Execute tools ---
            let (tool_results, steering_triggered) = self
                .execute_tools_unified(&tool_calls, &mut detector, &event_tx)
                .await;

            // --- Stuck detection ---
            debug!(
                text_len = response_text.len(),
                tool_calls = tool_calls.len(),
                tool_results = tool_results.len(),
                "running stuck detection"
            );
            match detector.check(
                &response_text,
                &tool_calls,
                &tool_results,
                usage.as_ref(),
                &self.config,
                self.llm.as_ref(),
            ) {
                StuckAction::Continue => {}
                StuckAction::InjectMessage(msg) => {
                    let assistant_message = Message::new(Role::Assistant, response_text.clone())
                        .with_tool_calls(tool_calls.clone());
                    self.context.add_message(assistant_message.clone());
                    session.add_message(assistant_message);
                    self.add_tool_results(&tool_calls, &tool_results, &mut session);
                    Self::emit(&event_tx, AgentEvent::Progress(msg.clone()));
                    let nudge = Message::new(Role::User, msg);
                    self.context.add_message(nudge.clone());
                    session.add_message(nudge);
                    continue;
                }
                StuckAction::Stop(reason) => {
                    Self::emit(&event_tx, AgentEvent::Progress(reason.clone()));
                    session.add_message(Message::new(Role::System, reason));
                    break;
                }
            }

            // --- Empty response handling ---
            if response_text.trim().is_empty() && tool_calls.is_empty() {
                let msg = format!(
                    "Provider returned empty response (model: {}, turn {}). \
                     Possible API format mismatch. Run with RUST_LOG=debug for details.",
                    self.config.model, turn
                );
                warn!("{msg}");
                Self::emit(&event_tx, AgentEvent::Error(msg));
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
                info!(
                    text_len = response_text.len(),
                    "natural completion — no tool calls"
                );
                session.token_usage = total_usage;
                Self::emit(
                    &event_tx,
                    AgentEvent::ToolStats(detector.tool_monitor().stats()),
                );
                Self::emit(&event_tx, AgentEvent::Complete(session.clone()));
                return Ok(session);
            }

            let completion_requested = tool_calls
                .iter()
                .any(|call| call.name == "attempt_completion");

            // Add tool results to context
            self.add_tool_results(&tool_calls, &tool_results, &mut session);

            // Inject a self-correction hint for the first error (skip if steering overrides)
            if !steering_triggered {
                if let Some(err_result) = tool_results
                    .iter()
                    .find(|result| result.is_error || has_validation_failure(result))
                {
                    let (prefix, first_line) = correction_hint_parts(err_result);
                    let hint = format!(
                        "{prefix}: {first_line}. Try a different approach — don't repeat the same call."
                    );
                    let hint_msg = Message::new(Role::User, hint);
                    self.context.add_message(hint_msg.clone());
                    session.add_message(hint_msg);
                }
            }

            // Steering injection: if steering was triggered, inject all steering
            // messages as user turns and skip to the next LLM call.
            if steering_triggered {
                if let Some(ref mut queue) = self.message_queue {
                    let steering_msgs = queue.drain_steering();
                    for text in steering_msgs {
                        let prefixed = format!("[User steering] {text}");
                        Self::emit(&event_tx, AgentEvent::Progress(format!("steering: {text}")));
                        let msg = Message::new(Role::User, prefixed);
                        self.context.add_message(msg.clone());
                        session.add_message(msg);
                    }
                }
                // Continue to next turn — the LLM will see the steering message
                continue;
            }

            if self.context.should_compact() {
                if let Err(error) = self.context.compact_async().await {
                    Self::emit(&event_tx, AgentEvent::Error(error.to_string()));
                    return Err(error.into());
                }
                Self::emit(
                    &event_tx,
                    AgentEvent::Progress("context compacted".to_string()),
                );
            }

            if completion_requested {
                session.token_usage = total_usage;
                Self::emit(
                    &event_tx,
                    AgentEvent::ToolStats(detector.tool_monitor().stats()),
                );
                Self::emit(&event_tx, AgentEvent::Complete(session.clone()));
                return Ok(session);
            }
        }

        // --- Cleanup ---
        info!("agent loop ended");
        session.token_usage = total_usage;
        Self::emit(
            &event_tx,
            AgentEvent::ToolStats(detector.tool_monitor().stats()),
        );
        Self::emit(&event_tx, AgentEvent::Complete(session.clone()));
        Ok(session)
    }

    /// Headless (non-streaming) execution. Delegates to the unified engine with no event sink.
    #[instrument(skip(self), fields(model = %self.config.model))]
    pub async fn run(&mut self, goal: &str) -> ava_types::Result<Session> {
        self.run_unified(goal, None).await
    }

    /// Streaming execution. Delegates to the unified engine with an event channel,
    /// returning the receiver wrapped as a stream for backward compatibility.
    #[instrument(skip(self), fields(model = %self.config.model))]
    pub async fn run_streaming(
        &mut self,
        goal: &str,
    ) -> Pin<Box<dyn Stream<Item = AgentEvent> + Send + '_>> {
        let (tx, rx) = mpsc::unbounded_channel();
        let goal = goal.to_string();

        // Run the unified engine inline (not spawned) so we can borrow `self`.
        // We use async_stream to drive the unified engine and yield events from
        // the receiver in lock-step.
        Box::pin(async_stream::stream! {
            // Start the unified engine — it will send events to `tx`.
            // We run it as a future and poll the receiver for events.
            let mut engine_fut = std::pin::pin!(self.run_unified(&goal, Some(tx)));
            let mut rx = rx;
            let mut engine_done = false;

            loop {
                if engine_done {
                    // Drain remaining events from the channel after engine completes
                    while let Ok(event) = rx.try_recv() {
                        yield event;
                    }
                    break;
                }

                tokio::select! {
                    biased;
                    // Prefer draining events before polling the engine again
                    event = rx.recv() => {
                        match event {
                            Some(event) => yield event,
                            None => break, // channel closed
                        }
                    }
                    result = &mut engine_fut => {
                        engine_done = true;
                        // If engine errored and didn't emit Error event, emit one now
                        if let Err(error) = result {
                            yield AgentEvent::Error(error.to_string());
                        }
                        // Continue to drain remaining events
                    }
                }
            }
        })
    }
}

fn correction_hint_parts(result: &ToolResult) -> (&'static str, &str) {
    if result.is_error {
        (
            "Tool call failed",
            result.content.lines().next().unwrap_or("unknown error"),
        )
    } else {
        (
            "Post-edit validation failed",
            result
                .content
                .lines()
                .find(|line| line.starts_with("- "))
                .unwrap_or("validation failed"),
        )
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
            max_budget_usd: 0.0,
            max_cost_usd: 1.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
        };
        let llm = crate::tests::mock_llm();

        // First empty: continue
        let action = detector.check("", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));

        // Second empty: stop
        let action = detector.check("", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_identical_responses() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
        };
        let llm = crate::tests::mock_llm();

        for i in 0..2 {
            let action = detector.check("same response", &[], &[], None, &config, llm.as_ref());
            assert!(
                matches!(action, StuckAction::Continue),
                "iteration {i} should continue"
            );
        }

        let action = detector.check("same response", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_tool_call_loop() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
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
                std::slice::from_ref(&call),
                &[],
                None,
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check(
            "reading again",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    #[test]
    fn stuck_detector_error_loop() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 10.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
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
                std::slice::from_ref(&error_result),
                None,
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check(
            "trying again",
            &[],
            std::slice::from_ref(&error_result),
            None,
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
            max_budget_usd: 0.0,
            max_cost_usd: 0.0, // Zero threshold = immediate stop
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
        };
        let llm = crate::tests::mock_llm();

        let action = detector.check("hello", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn stuck_detector_disabled() {
        let mut detector = StuckDetector::new();
        let config = AgentConfig {
            max_turns: 10,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_budget_usd: 0.0,
            max_cost_usd: 0.0,
            loop_detection: false,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
        };
        let llm = crate::tests::mock_llm();

        // Would normally trigger cost stop, but detection is disabled
        let action = detector.check("hello", &[], &[], None, &config, llm.as_ref());
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

    // --- Plan mode tests ---

    #[test]
    fn plan_mode_allows_write_to_plan_dir() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": ".ava/plans/my-plan.md", "content": "# Plan"}),
        };
        assert!(check_plan_mode_tool(&tc).is_none());
    }

    #[test]
    fn plan_mode_allows_write_to_nested_plan_dir() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": "/home/user/project/.ava/plans/refactor.md", "content": "# Plan"}),
        };
        assert!(check_plan_mode_tool(&tc).is_none());
    }

    #[test]
    fn plan_mode_blocks_write_to_source_files() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": "src/main.rs", "content": "fn main() {}"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
        assert!(result.unwrap().contains("Plan mode"));
    }

    #[test]
    fn plan_mode_blocks_write_to_non_md_in_plan_dir() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"path": ".ava/plans/script.sh", "content": "#!/bin/bash"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
        assert!(result.unwrap().contains("Plan mode"));
    }

    #[test]
    fn plan_mode_blocks_bash() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "bash".to_string(),
            arguments: serde_json::json!({"command": "rm -rf /"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
        assert!(result.unwrap().contains("bash is not available"));
    }

    #[test]
    fn plan_mode_allows_read_tools() {
        use tool_execution::check_plan_mode_tool;
        for tool_name in &["read", "glob", "grep", "codebase_search", "todo_read"] {
            let tc = ToolCall {
                id: "1".to_string(),
                name: tool_name.to_string(),
                arguments: serde_json::json!({"path": "src/main.rs"}),
            };
            assert!(
                check_plan_mode_tool(&tc).is_none(),
                "{tool_name} should be allowed in plan mode"
            );
        }
    }

    #[test]
    fn plan_mode_blocks_edit_to_source() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "edit".to_string(),
            arguments: serde_json::json!({"path": "src/lib.rs", "old_string": "a", "new_string": "b"}),
        };
        let result = check_plan_mode_tool(&tc);
        assert!(result.is_some());
    }

    #[test]
    fn plan_mode_allows_attempt_completion() {
        use tool_execution::check_plan_mode_tool;
        let tc = ToolCall {
            id: "1".to_string(),
            name: "attempt_completion".to_string(),
            arguments: serde_json::json!({"result": "Plan complete."}),
        };
        assert!(check_plan_mode_tool(&tc).is_none());
    }

    #[test]
    fn is_plan_path_validates_correctly() {
        use tool_execution::is_plan_path;
        assert!(is_plan_path(".ava/plans/my-plan.md"));
        assert!(is_plan_path("/home/user/.ava/plans/refactor.md"));
        assert!(!is_plan_path(".ava/plans/script.sh"));
        assert!(!is_plan_path("src/main.rs"));
        assert!(!is_plan_path(".ava/config.toml"));
        assert!(!is_plan_path(".ava/plans/")); // no filename
    }
}
