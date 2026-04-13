//! LLM call orchestration, completion detection, and session finalization.
//!
//! Contains `generate_turn_response` (the main LLM calling + response parsing logic),
//! streaming/non-streaming variants, system prompt injection, plugin hooks for
//! messages transforms and chat params, plus completion detection helpers
//! (turn/budget limits, natural completion, attempt_completion).

use std::time::{Duration, Instant};

use ava_llm::ThinkingConfig;
use ava_types::{Message, Role, Session, ThinkingLevel, TokenUsage, ToolCall};
use futures::StreamExt;
use tokio::sync::mpsc;
use tracing::{debug, info, trace, warn};

use super::response;
use super::tool_execution::READ_ONLY_TOOLS;
use super::{AgentEvent, AgentLoop};
use crate::stuck::StuckDetector;
use crate::system_prompt::{
    build_system_prompt_with_override, provider_prompt_suffix_with_provider_and_override,
    SystemPromptParts,
};
use crate::trace::RunEventKind;

use super::response::parse_tool_calls;

/// Tools that modify files — used to decide when to emit `StreamingEditProgress`.
const EDIT_TOOL_NAMES: &[&str] = &["write", "edit", "multiedit", "apply_patch"];

/// Ensure every assistant tool_call has a matching Tool result in the message list.
///
/// After visibility filtering (compaction marks some messages `agent_visible=false`),
/// an assistant message with tool_calls may remain while its corresponding tool result
/// messages were filtered out. OpenAI returns 400 "No tool output found for function
/// call" in this case. This function appends synthetic error results for any orphaned
/// tool_calls, making the history valid for all providers.
pub(super) fn ensure_tool_call_consistency(messages: &mut Vec<Message>) {
    use std::collections::HashSet;

    let answered: HashSet<&str> = messages
        .iter()
        .filter(|m| m.role == Role::Tool)
        .filter_map(|m| m.tool_call_id.as_deref())
        .collect();

    let mut synthetic = Vec::new();
    for msg in messages.iter() {
        if msg.role != Role::Assistant {
            continue;
        }
        for tc in &msg.tool_calls {
            if !answered.contains(tc.id.as_str()) {
                let content = "[Tool result removed during context compaction]".to_string();
                let result = ava_types::ToolResult {
                    call_id: tc.id.clone(),
                    content: content.clone(),
                    is_error: true,
                };
                let tool_msg = Message::new(Role::Tool, content)
                    .with_tool_call_id(&tc.id)
                    .with_tool_results(vec![result]);
                synthetic.push(tool_msg);
            }
        }
    }

    if !synthetic.is_empty() {
        tracing::debug!(
            count = synthetic.len(),
            "injected synthetic tool results for orphaned tool_calls after visibility filtering"
        );
        messages.extend(synthetic);
    }
}

/// Extract a file path from partially-accumulated JSON arguments.
///
/// Looks for `"file_path":"..."` or `"path":"..."` patterns using simple string
/// search (no JSON parser needed since the JSON may be incomplete). This is a
/// best-effort progress hint, not an authoritative parser.
fn extract_file_path_from_partial(json: &str) -> Option<String> {
    for key in &["file_path", "path"] {
        let pattern = format!("\"{}\":\"", key);
        if let Some(start) = json.find(&pattern) {
            let value_start = start + pattern.len();
            if let Some(end) = json[value_start..].find('"') {
                let path = &json[value_start..value_start + end];
                if !path.is_empty() {
                    return Some(path.to_string());
                }
            }
        }
    }
    None
}

fn estimate_tokens(text: &str) -> usize {
    ava_context::count_tokens_default(text)
}

impl AgentLoop {
    /// Inject the system prompt into the context before the first turn.
    /// Idempotent: calling this multiple times (e.g., follow-up runs) is safe.
    ///
    /// Fires the `chat.system` plugin hook to allow plugins to append text to
    /// the system prompt before it is committed to the context.
    pub(super) async fn inject_system_prompt(&mut self) {
        // Skip if system prompt already present (follow-up / post-complete reuse the context)
        if self
            .context
            .get_messages()
            .iter()
            .any(|m| m.role == Role::System)
        {
            return;
        }
        let (mut parts, is_custom) = if let Some(ref custom) = self.config.custom_system_prompt {
            (
                SystemPromptParts {
                    static_prefix: custom.clone(),
                    dynamic_suffix: String::new(),
                    cache_boundary: custom.len(),
                },
                true,
            )
        } else {
            let native = self.llm.supports_tools();
            let provider_kind = self.llm.provider_kind();
            let tool_defs = self.active_tool_defs_with_hooks().await;
            (
                build_system_prompt_with_override(
                    &tool_defs,
                    native,
                    provider_kind,
                    &self.config.model,
                    self.tool_visibility_profile,
                    self.config.benchmark_prompt_override.as_ref(),
                ),
                false,
            )
        };

        // Append provider-specific instructions to the dynamic suffix.
        let provider_kind = self.llm.provider_kind();
        if let Some(p_suffix) = provider_prompt_suffix_with_provider_and_override(
            provider_kind,
            Some(&self.config.provider),
            &self.config.model,
            self.config.benchmark_prompt_override.as_ref(),
        ) {
            parts.dynamic_suffix.push_str("\n\n");
            parts.dynamic_suffix.push_str(&p_suffix);
        }
        if let Some(ref suffix) = self.config.system_prompt_suffix {
            parts.dynamic_suffix.push_str("\n\n");
            parts.dynamic_suffix.push_str(suffix);
        }
        // chat.system hook: let plugins inject text into the dynamic suffix.
        if let Some(pm) = self.plugin_manager.as_ref() {
            let provider_name = format!("{:?}", provider_kind).to_lowercase();
            let injection = pm
                .lock()
                .await
                .collect_system_injections(&self.config.model, &provider_name)
                .await;
            if let Some(text) = injection {
                parts.dynamic_suffix.push_str("\n\n");
                parts.dynamic_suffix.push_str(&text);
            }
        }
        let system = parts.full_prompt();
        info!(
            model = %self.config.model,
            prompt_chars = system.len(),
            prompt_tokens = estimate_tokens(&system),
            cache_boundary = parts.cache_boundary,
            has_suffix = self.config.system_prompt_suffix.is_some(),
            dynamic_rules = self.enable_dynamic_rules,
            "system prompt prepared"
        );
        let mut msg = Message::new(Role::System, system);
        // F2: Store cache boundary offset in metadata so providers can split
        // the prompt into cacheable (static) and non-cacheable (dynamic) parts.
        if !is_custom && self.config.prompt_caching && parts.cache_boundary > 0 {
            msg.metadata = serde_json::json!({"cache_boundary": parts.cache_boundary});
        }
        self.context.add_message(msg);
    }

    /// Generate LLM response — streaming when `event_tx` is present, non-streaming otherwise.
    ///
    /// Returns (response_text, tool_calls, usage). When streaming, tokens and thinking
    /// events are emitted to `event_tx` as they arrive.
    pub(super) async fn generate_turn_response(
        &mut self,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        let native_tools = self.llm.supports_tools();

        // chat.params hook: allow plugins to modify per-call LLM parameters.
        if let Some(pm) = self.plugin_manager.as_ref() {
            let mut pm = pm.lock().await;
            if pm.has_hook_subscribers(ava_plugin::HookEvent::ChatParams) {
                let params = serde_json::json!({
                    "model": self.config.model,
                    "max_tokens": self.config.token_limit,
                    "thinking_level": format!("{:?}", self.config.thinking_level).to_lowercase(),
                    "thinking_budget_tokens": self.config.thinking_budget_tokens,
                });
                let modified = pm.apply_chat_params_hook(params).await;
                // Apply supported overrides back to config.
                if let Some(v) = modified.get("max_tokens").and_then(|v| v.as_u64()) {
                    self.config.token_limit = v as usize;
                }
                if let Some(v) = modified.get("thinking_budget_tokens") {
                    if v.is_null() {
                        self.config.thinking_budget_tokens = None;
                    } else if let Some(n) = v.as_u64() {
                        self.config.thinking_budget_tokens = Some(n as u32);
                    }
                }
            }
        }

        let prepared = self.prepare_llm_request();
        let dedup_hash = prepared.dedup_hash;
        if let Some(empty) = self.check_dedup_guard(dedup_hash) {
            return Ok(empty);
        }

        let streaming = event_tx.is_some();
        let llm_started_at = Instant::now();
        let prepared_token_count: usize = prepared
            .messages
            .iter()
            .map(|message| ava_context::count_tokens_default(&message.content))
            .sum();
        self.append_run_trace(RunEventKind::LlmRequest {
            model: self.config.model.clone(),
            token_count: prepared_token_count,
        });

        let result = if streaming {
            self.generate_turn_streaming(native_tools, prepared.messages, event_tx)
                .await
        } else {
            self.generate_turn_non_streaming(prepared).await
        };

        // Set dedup hash on success with non-empty response
        if let Ok((text, calls, _)) = &result {
            if !text.trim().is_empty() || !calls.is_empty() {
                self.last_request_hash = Some(dedup_hash);
                self.last_request_time = Some(Instant::now());
            }
        }
        if let Ok((_, _, usage)) = &result {
            let (tokens_in, tokens_out) = usage
                .as_ref()
                .map(|u| (u.input_tokens, u.output_tokens))
                .unwrap_or_default();
            self.append_run_trace(RunEventKind::LlmResponse {
                tokens_in,
                tokens_out,
                duration_ms: llm_started_at.elapsed().as_millis() as u64,
            });
        }

        result
    }

    /// Apply the `chat.messages.transform` hook to the current context messages.
    ///
    /// Returns the (possibly modified) message list. If no plugins subscribe or the
    /// hook returns unchanged content, the original context messages are returned as-is.
    async fn apply_messages_transform(&mut self, messages: Vec<Message>) -> Vec<Message> {
        let Some(pm) = self.plugin_manager.as_ref() else {
            return messages;
        };

        if !pm
            .lock()
            .await
            .has_hook_subscribers(ava_plugin::HookEvent::ChatMessagesTransform)
        {
            return messages;
        }

        // Only send agent-visible messages to the LLM. Compacted messages
        // (agent_visible=false) are preserved for UI display but excluded
        // from the context window.
        let messages: Vec<Message> = messages;

        // Serialize messages to JSON for the hook.
        let json_messages: Vec<serde_json::Value> = messages
            .iter()
            .map(|m| {
                let role_str = match m.role {
                    Role::System => "system",
                    Role::User => "user",
                    Role::Assistant => "assistant",
                    Role::Tool => "tool",
                };
                serde_json::json!({
                    "role": role_str,
                    "content": m.content,
                })
            })
            .collect();

        let transformed = pm
            .lock()
            .await
            .apply_messages_transform_hook(json_messages)
            .await;

        // Map transformed JSON back to Message objects.
        // For messages that still match by index, preserve the full original (with images,
        // tool calls, etc.) but update `content` if changed. For new/extra messages, create
        // minimal Message objects from the JSON.
        let mut result: Vec<Message> = Vec::with_capacity(transformed.len());
        for (i, json_msg) in transformed.iter().enumerate() {
            let content = json_msg
                .get("content")
                .and_then(|v| v.as_str())
                .unwrap_or("")
                .to_string();
            let role_str = json_msg
                .get("role")
                .and_then(|v| v.as_str())
                .unwrap_or("user");
            if let Some(orig) = messages.get(i) {
                // Preserve original message structure; only update content if changed.
                if orig.content != content {
                    let mut updated = orig.clone();
                    updated.content = content;
                    result.push(updated);
                } else {
                    result.push(orig.clone());
                }
            } else {
                // New message injected by a plugin.
                let role = match role_str {
                    "assistant" => Role::Assistant,
                    "system" => Role::System,
                    "tool" => Role::Tool,
                    _ => Role::User,
                };
                result.push(Message::new(role, content));
            }
        }
        ensure_tool_call_consistency(&mut result);
        result
    }

    /// Streaming LLM call: emits Token, Thinking, and TokenUsage events.
    ///
    /// F1 — Streaming tool execution: read-only tools whose arguments are fully
    /// accumulated are dispatched to the registry immediately (while the stream
    /// continues). Pre-dispatched results are returned alongside finalized tool
    /// calls so the main loop can skip re-executing them.
    async fn generate_turn_streaming(
        &mut self,
        native_tools: bool,
        prepared_messages: Vec<Message>,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        info!(
            model = %self.config.model,
            native_tools,
            thinking = %self.config.thinking_level,
            messages = self.context.get_messages().len(),
            "starting LLM stream request"
        );

        // --- chat.messages.transform hook (request/response) ---
        let messages = self.apply_messages_transform(prepared_messages).await;
        let provider_request_start = Instant::now();

        let stream_result = if native_tools {
            let tool_defs = self.active_tool_defs_with_hooks().await;
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
                    .generate_stream_with_thinking_config(&messages, &tool_defs, thinking)
                    .await
            } else {
                self.llm
                    .generate_stream_with_tools(&messages, &tool_defs)
                    .await
            }
        } else {
            self.llm.generate_stream(&messages).await
        };

        let mut stream = stream_result?;
        let provider_stream_ready_ms = provider_request_start.elapsed().as_millis() as u64;
        let mut full_text = String::new();
        let mut accumulated_tool_calls: Vec<response::ToolCallAccumulator> = Vec::new();
        let mut last_usage: Option<TokenUsage> = None;
        let mut chunk_count: usize = 0;
        let mut first_chunk_ms: Option<u64> = None;
        let mut first_text_ms: Option<u64> = None;
        let mut first_tool_ms: Option<u64> = None;

        // F8 — Stream Idle Watchdog: two-stage silence detection.
        // Stage 1 (warning): after half the timeout, emit a warning event.
        // Stage 2 (kill): after the full timeout, cancel the stream.
        // Both timers reset on each received chunk.
        let timeout_secs = self.config.stream_timeout_secs;
        let use_timeout = timeout_secs > 0;
        let kill_duration = Duration::from_secs(timeout_secs);
        let warn_duration = Duration::from_secs(timeout_secs / 2);
        let mut warning_emitted = false;

        // F1 — Pre-dispatch tracking: indices of tool calls already dispatched
        // while the stream was still active. Maps accumulator index → JoinHandle.
        let mut pre_dispatched: std::collections::HashMap<
            usize,
            tokio::task::JoinHandle<ava_types::ToolResult>,
        > = std::collections::HashMap::new();

        loop {
            let maybe_chunk = if use_timeout {
                // Two-stage watchdog: first check warning, then kill.
                if !warning_emitted {
                    // Stage 1: wait for chunk OR warning timeout
                    match tokio::time::timeout(warn_duration, stream.next()).await {
                        Ok(chunk) => chunk,
                        Err(_elapsed) => {
                            // Warning stage: emit event, then wait for remaining time
                            #[allow(unused_assignments)]
                            {
                                warning_emitted = true;
                            }
                            let warn_secs = timeout_secs / 2;
                            warn!(
                                elapsed_secs = warn_secs,
                                "stream silence warning: no chunks received for {warn_secs}s"
                            );
                            Self::emit(
                                event_tx,
                                AgentEvent::StreamSilenceWarning {
                                    elapsed_secs: warn_secs,
                                },
                            );
                            // Stage 2: wait remaining time for kill
                            let remaining = kill_duration.saturating_sub(warn_duration);
                            match tokio::time::timeout(remaining, stream.next()).await {
                                Ok(chunk) => chunk,
                                Err(_elapsed) => {
                                    let msg = format!(
                                        "LLM stream timed out after {timeout_secs} seconds of silence. \
                                         The provider may be overloaded."
                                    );
                                    warn!("{msg}");
                                    return Err(ava_types::AvaError::ProviderError {
                                        provider: self.config.model.clone(),
                                        message: msg,
                                    });
                                }
                            }
                        }
                    }
                } else {
                    // Warning already emitted this silence window; wait for kill timeout only
                    match tokio::time::timeout(kill_duration, stream.next()).await {
                        Ok(chunk) => chunk,
                        Err(_elapsed) => {
                            let msg = format!(
                                "LLM stream timed out after {timeout_secs} seconds of silence. \
                                 The provider may be overloaded."
                            );
                            warn!("{msg}");
                            return Err(ava_types::AvaError::ProviderError {
                                provider: self.config.model.clone(),
                                message: msg,
                            });
                        }
                    }
                }
            } else {
                stream.next().await
            };

            let Some(chunk) = maybe_chunk else {
                break;
            };

            if first_chunk_ms.is_none() {
                first_chunk_ms = Some(provider_request_start.elapsed().as_millis() as u64);
            }
            chunk_count += 1;
            // F8: Reset watchdog warning state on each received chunk.
            warning_emitted = false;
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
                if first_text_ms.is_none() {
                    first_text_ms = Some(provider_request_start.elapsed().as_millis() as u64);
                }
                full_text.push_str(text);
                if !self.suppress_next_tokens {
                    Self::emit(event_tx, AgentEvent::Token(text.to_string()));
                }
            }
            // Emit thinking
            if let Some(ref thinking) = chunk.thinking {
                if !thinking.is_empty() {
                    Self::emit(event_tx, AgentEvent::Thinking(thinking.clone()));
                }
            }
            // Accumulate tool call fragments
            if let Some(ref tc) = chunk.tool_call {
                if first_tool_ms.is_none() {
                    first_tool_ms = Some(provider_request_start.elapsed().as_millis() as u64);
                }
                let delta_len = tc.arguments_delta.as_ref().map_or(0, |d| d.len());
                response::accumulate_tool_call(&mut accumulated_tool_calls, tc);

                // Emit StreamingEditProgress for file-editing tools
                if let Some(acc) = accumulated_tool_calls.iter().find(|a| a.index == tc.index) {
                    if EDIT_TOOL_NAMES.contains(&acc.name.as_str()) {
                        // First progress: as soon as we know it's an edit tool (even before file path)
                        // Subsequent progress: every ~500 bytes of accumulated arguments
                        let should_emit = acc.arguments_json.len() <= delta_len // first delta
                            || (acc.arguments_json.len() > 100
                                && acc.arguments_json.len() % 500 < delta_len.max(1));
                        if should_emit {
                            let file_path = extract_file_path_from_partial(&acc.arguments_json);
                            Self::emit(
                                event_tx,
                                AgentEvent::StreamingEditProgress {
                                    call_id: acc.id.clone(),
                                    tool_name: acc.name.clone(),
                                    file_path,
                                    bytes_received: acc.arguments_json.len(),
                                },
                            );
                        }
                    }
                }
            }
            // F1 — Pre-dispatch: if a tool call just became complete and it's
            // read-only, spawn its execution now (overlaps with continued streaming).
            if let Some(ref tc) = chunk.tool_call {
                if let Some(acc) = accumulated_tool_calls.iter().find(|a| a.index == tc.index) {
                    if !pre_dispatched.contains_key(&acc.index)
                        && acc.is_complete()
                        && READ_ONLY_TOOLS.contains(&acc.name.as_str())
                    {
                        if let Some(tool_call) = acc.to_tool_call() {
                            debug!(
                                tool = %tool_call.name,
                                index = acc.index,
                                "F1: pre-dispatching read-only tool during stream"
                            );
                            let tools = self.tools.clone();
                            let join = tokio::spawn(async move {
                                match tools.execute(tool_call).await {
                                    Ok(result) => result,
                                    Err(err) => ava_types::ToolResult {
                                        call_id: String::new(),
                                        content: err.to_string(),
                                        is_error: true,
                                    },
                                }
                            });
                            pre_dispatched.insert(acc.index, join);
                        }
                    }
                }
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
            provider_stream_ready_ms,
            first_chunk_ms,
            first_text_ms,
            first_tool_ms,
            "stream completed"
        );

        // --- text.complete hook (notification) ---
        if !full_text.is_empty() {
            if let Some(ref pm) = self.plugin_manager {
                let has_hook = pm
                    .lock()
                    .await
                    .has_hook_subscribers(ava_plugin::HookEvent::TextComplete);
                if has_hook {
                    let token_count = last_usage.as_ref().map(|u| u.output_tokens).unwrap_or(0);
                    let mut pm = pm.lock().await;
                    pm.trigger_hook(
                        ava_plugin::HookEvent::TextComplete,
                        serde_json::json!({
                            "session_id": "",
                            "content": full_text,
                            "token_count": token_count,
                        }),
                    )
                    .await;
                }
            }
        }

        // F1 — Collect pre-dispatched results before finalization.
        // Map accumulator index → tool result for tools that ran during the stream.
        if !pre_dispatched.is_empty() {
            debug!(
                count = pre_dispatched.len(),
                "F1: collecting pre-dispatched tool results"
            );
        }
        // Build index→id mapping before finalization consumes the accumulators.
        let acc_index_to_id: std::collections::HashMap<usize, String> = accumulated_tool_calls
            .iter()
            .map(|a| {
                (
                    a.index,
                    if a.id.is_empty() {
                        String::new()
                    } else {
                        a.id.clone()
                    },
                )
            })
            .collect();

        // Convert accumulated tool calls or parse from text
        let tool_calls = if native_tools && !accumulated_tool_calls.is_empty() {
            response::finalize_tool_calls(accumulated_tool_calls)
        } else if !native_tools {
            parse_tool_calls(&full_text)?
        } else {
            vec![]
        };

        // F1: Await and store pre-dispatched results keyed by tool call ID.
        self.pre_dispatched_results.clear();
        for (acc_idx, handle) in pre_dispatched {
            if let Ok(mut result) = handle.await {
                // Map accumulator index to the finalized tool call ID.
                if let Some(id) = acc_index_to_id.get(&acc_idx) {
                    if !id.is_empty() {
                        result.call_id = id.clone();
                        self.pre_dispatched_results.insert(id.clone(), result);
                    }
                }
            }
        }
        if !self.pre_dispatched_results.is_empty() {
            info!(
                count = self.pre_dispatched_results.len(),
                "F1: pre-dispatched tool results ready"
            );
        }

        Ok((full_text, tool_calls, last_usage))
    }

    /// Non-streaming LLM call (used by headless mode).
    async fn generate_turn_non_streaming(
        &mut self,
        prepared: super::response::PreparedRequest,
    ) -> ava_types::Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        self.generate_response_with_thinking_prepared(prepared)
            .await
    }

    /// Check if the turn limit has been reached. If so, force a summary and return `true`.
    pub(super) async fn check_turn_limit(
        &mut self,
        turn: usize,
        session: &mut Session,
        total_usage: &mut TokenUsage,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        if self.config.max_turns > 0 && turn >= self.effective_max_turns() {
            self.force_summary(
                session,
                &format!(
                    "You have reached the maximum number of turns ({}). Please summarize what you've accomplished and list any remaining work.",
                    self.config.max_turns,
                ),
                total_usage,
                event_tx,
            ).await;
            true
        } else {
            false
        }
    }

    /// Check if the budget limit has been reached. If so, force a summary and return `true`.
    pub(super) async fn check_budget_limit(
        &mut self,
        total_cost_usd: f64,
        session: &mut Session,
        total_usage: &mut TokenUsage,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        let max_budget = self.config.max_budget_usd;
        if max_budget > 0.0 && total_cost_usd >= max_budget {
            self.force_summary(
                session,
                &format!(
                    "You have reached the budget limit (${:.2}). Please summarize what you've accomplished and list any remaining work.",
                    max_budget,
                ),
                total_usage,
                event_tx,
            ).await;
            true
        } else {
            false
        }
    }

    /// Effective max turns (0 means unlimited, represented as `usize::MAX`).
    pub(super) fn effective_max_turns(&self) -> usize {
        if self.config.max_turns == 0 {
            usize::MAX
        } else {
            self.config.max_turns
        }
    }

    /// Handle natural completion (non-empty text, no tool calls).
    ///
    /// Checks for pending steering messages first. If steering is pending,
    /// returns `false` (do not complete, continue looping). Otherwise, emits
    /// completion events and returns `true` (complete).
    pub(super) async fn handle_natural_completion(
        &mut self,
        response_text: &str,
        session: &mut Session,
        total_usage: TokenUsage,
        detector: &StuckDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        // Check for steering messages that arrived during LLM call
        if self.check_steering_before_complete(session, event_tx) {
            return false; // Continue looping
        }

        info!(
            text_len = response_text.len(),
            "natural completion — no tool calls"
        );
        session.token_usage = total_usage;
        Self::emit(
            event_tx,
            AgentEvent::ToolStats(detector.tool_monitor().stats()),
        );
        let complete_event = AgentEvent::Complete(session.clone());
        Self::emit(event_tx, complete_event.clone());
        self.broadcast_event_to_plugins(&complete_event).await;
        true // Complete
    }

    /// Emit final completion events when the loop ends (turn/budget limit, stuck, etc.).
    pub(super) async fn emit_final_completion(
        &self,
        session: &mut Session,
        total_usage: TokenUsage,
        detector: &StuckDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) {
        info!("agent loop ended");
        session.token_usage = total_usage;
        Self::emit(
            event_tx,
            AgentEvent::ToolStats(detector.tool_monitor().stats()),
        );
        let complete_event = AgentEvent::Complete(session.clone());
        Self::emit(event_tx, complete_event.clone());
        self.broadcast_event_to_plugins(&complete_event).await;
    }

    /// Emit completion events for attempt_completion tool call.
    pub(super) async fn handle_attempt_completion(
        &self,
        tool_calls: &[ToolCall],
        session: &mut Session,
        total_usage: TokenUsage,
        detector: &StuckDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        let completion_requested = tool_calls
            .iter()
            .any(|call| call.name == "attempt_completion");
        if !completion_requested {
            return false;
        }
        session.token_usage = total_usage;
        Self::emit(
            event_tx,
            AgentEvent::ToolStats(detector.tool_monitor().stats()),
        );
        let complete_event = AgentEvent::Complete(session.clone());
        Self::emit(event_tx, complete_event.clone());
        self.broadcast_event_to_plugins(&complete_event).await;
        true
    }
}
