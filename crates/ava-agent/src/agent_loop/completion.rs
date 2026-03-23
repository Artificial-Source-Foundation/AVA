//! LLM call orchestration, completion detection, and session finalization.
//!
//! Contains `generate_turn_response` (the main LLM calling + response parsing logic),
//! streaming/non-streaming variants, system prompt injection, plugin hooks for
//! messages transforms and chat params, plus completion detection helpers
//! (turn/budget limits, natural completion, attempt_completion).

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::time::{Duration, Instant};

use ava_llm::ThinkingConfig;
use ava_types::{Message, Role, Session, ThinkingLevel, TokenUsage, ToolCall};
use futures::StreamExt;
use tokio::sync::mpsc;
use tracing::{debug, info, trace, warn};

use super::response;
use super::{AgentEvent, AgentLoop};
use crate::stuck::StuckDetector;
use crate::system_prompt::{build_system_prompt, provider_prompt_suffix};

use super::response::parse_tool_calls;

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
        let mut system = if let Some(ref custom) = self.config.custom_system_prompt {
            custom.clone()
        } else {
            let native = self.llm.supports_tools();
            let tool_defs = self.active_tool_defs_with_hooks().await;
            build_system_prompt(&tool_defs, native)
        };
        // Append provider-specific instructions (additive — does not replace the base prompt).
        let provider_kind = self.llm.provider_kind();
        if let Some(p_suffix) = provider_prompt_suffix(provider_kind, &self.config.model) {
            system.push_str("\n\n");
            system.push_str(&p_suffix);
        }
        if let Some(ref suffix) = self.config.system_prompt_suffix {
            system.push_str("\n\n");
            system.push_str(suffix);
        }
        // chat.system hook: let plugins inject text into the system prompt.
        if let Some(ref pm) = self.plugin_manager.clone() {
            let provider_name = format!("{:?}", provider_kind).to_lowercase();
            let injection = pm
                .lock()
                .await
                .collect_system_injections(&self.config.model, &provider_name)
                .await;
            if let Some(text) = injection {
                system.push_str("\n\n");
                system.push_str(&text);
            }
        }
        info!(
            model = %self.config.model,
            prompt_chars = system.len(),
            prompt_tokens = estimate_tokens(&system),
            has_suffix = self.config.system_prompt_suffix.is_some(),
            dynamic_rules = self.enable_dynamic_rules,
            "system prompt prepared"
        );
        self.context.add_message(Message::new(Role::System, system));
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

        // chat.params hook: allow plugins to modify per-call LLM parameters.
        if let Some(ref pm) = self.plugin_manager.clone() {
            let params = serde_json::json!({
                "model": self.config.model,
                "max_tokens": self.config.token_limit,
                "thinking_level": format!("{:?}", self.config.thinking_level).to_lowercase(),
                "thinking_budget_tokens": self.config.thinking_budget_tokens,
            });
            let modified = pm.lock().await.apply_chat_params_hook(params).await;
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

        // Repair conversation history before sending to LLM — fix orphaned tool
        // results, empty assistant messages, consecutive user messages, and duplicates
        // that would cause cryptic API errors.
        {
            let mut msgs = self.context.get_messages().to_vec();
            let before = msgs.len();
            ava_types::repair_conversation(&mut msgs);
            if msgs.len() != before {
                debug!(
                    before,
                    after = msgs.len(),
                    removed = before - msgs.len(),
                    "repaired conversation history before LLM call"
                );
                self.context.replace_messages(msgs);
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

    /// Apply the `chat.messages.transform` hook to the current context messages.
    ///
    /// Returns the (possibly modified) message list. If no plugins subscribe or the
    /// hook returns unchanged content, the original context messages are returned as-is.
    async fn apply_messages_transform(&mut self) -> Vec<Message> {
        // Only send agent-visible messages to the LLM. Compacted messages
        // (agent_visible=false) are preserved for UI display but excluded
        // from the context window.
        let messages: Vec<Message> = self
            .context
            .get_messages()
            .iter()
            .filter(|m| m.agent_visible)
            .cloned()
            .collect();
        let Some(ref pm) = self.plugin_manager.clone() else {
            return messages;
        };

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

        // --- chat.messages.transform hook (request/response) ---
        let messages = self.apply_messages_transform().await;

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
        let mut full_text = String::new();
        let mut accumulated_tool_calls: Vec<response::ToolCallAccumulator> = Vec::new();
        let mut last_usage: Option<TokenUsage> = None;
        let mut chunk_count: usize = 0;

        // Per-chunk silence timeout: if no chunk arrives within this window,
        // the stream is cancelled. A value of 0 disables the timeout.
        let timeout_secs = self.config.stream_timeout_secs;
        let use_timeout = timeout_secs > 0;
        let timeout_duration = Duration::from_secs(timeout_secs);

        loop {
            let maybe_chunk = if use_timeout {
                match tokio::time::timeout(timeout_duration, stream.next()).await {
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
            } else {
                stream.next().await
            };

            let Some(chunk) = maybe_chunk else {
                break;
            };

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

        // --- text.complete hook (notification) ---
        if !full_text.is_empty() {
            if let Some(ref pm) = self.plugin_manager {
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
