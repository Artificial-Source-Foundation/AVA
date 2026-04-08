use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::time::Duration;

use ava_llm::ThinkingConfig;
use ava_types::{AvaError, Message, Result, StreamToolCall, ThinkingLevel, TokenUsage, ToolCall};
use serde::Deserialize;
use serde_json::Value;
use tracing::warn;
use uuid::Uuid;

use ava_tools::registry::ToolTier;

use super::AgentLoop;

#[derive(Debug, Deserialize)]
pub(super) struct ToolCallEnvelope {
    name: String,
    #[serde(default)]
    arguments: Value,
    #[serde(default)]
    id: Option<String>,
}

/// Accumulates streaming tool call fragments into complete tool calls.
pub(super) struct ToolCallAccumulator {
    pub index: usize,
    pub id: String,
    pub name: String,
    pub arguments_json: String,
}

impl ToolCallAccumulator {
    /// F1 — Check if the accumulated arguments form valid JSON.
    ///
    /// Used during streaming to detect when a tool call's arguments are complete
    /// so read-only tools can be pre-dispatched before the stream finishes.
    pub fn is_complete(&self) -> bool {
        if self.name.is_empty() || self.arguments_json.is_empty() {
            return false;
        }
        serde_json::from_str::<serde_json::Value>(&self.arguments_json).is_ok()
    }

    /// Convert to a `ToolCall` if complete.
    pub fn to_tool_call(&self) -> Option<ToolCall> {
        if !self.is_complete() {
            return None;
        }
        let arguments = serde_json::from_str(&self.arguments_json).ok()?;
        Some(ToolCall {
            id: if self.id.is_empty() {
                Uuid::new_v4().to_string()
            } else {
                self.id.clone()
            },
            name: self.name.clone(),
            arguments,
        })
    }
}

pub(super) fn accumulate_tool_call(
    accumulators: &mut Vec<ToolCallAccumulator>,
    tc: &StreamToolCall,
) {
    let acc = if let Some(acc) = accumulators.iter_mut().find(|a| a.index == tc.index) {
        acc
    } else {
        accumulators.push(ToolCallAccumulator {
            index: tc.index,
            id: String::new(),
            name: String::new(),
            arguments_json: String::new(),
        });
        accumulators.last_mut().expect("accumulator just pushed")
    };
    if let Some(ref id) = tc.id {
        acc.id.clone_from(id);
    }
    if let Some(ref name) = tc.name {
        acc.name.clone_from(name);
    }
    if let Some(ref args) = tc.arguments_delta {
        merge_tool_arguments(&mut acc.arguments_json, args);
    }
}

/// Merge streaming tool argument fragments.
///
/// **F5 — Raw Stream Processing**: Instead of re-parsing JSON on every incoming
/// delta (O(n²) over the lifetime of a tool call), we simply accumulate raw
/// string fragments. All JSON validation is deferred to `finalize_tool_calls`,
/// which parses once at the end.
///
/// We keep three cheap guards:
/// 1. Skip empty deltas.
/// 2. Skip exact duplicates (some providers re-send the complete payload).
/// 3. Re-open a trailing `}` when a provider flushes a complete JSON object
///    then sends a `,<field>:...}` continuation fragment.
fn merge_tool_arguments(existing: &mut String, incoming: &str) {
    if incoming.is_empty() {
        return;
    }

    if existing.is_empty() {
        existing.push_str(incoming);
        return;
    }

    // Cheap string-level dedup: some providers re-send the full payload.
    if existing == incoming {
        return;
    }

    // Some providers stream tool arguments as full or progressively growing JSON
    // snapshots rather than true deltas. Replacing the buffer in those cases
    // avoids corrupting valid JSON by concatenating successive snapshots.
    let existing_trimmed = existing.trim();
    let incoming_trimmed = incoming.trim();
    let incoming_is_jsonish =
        incoming_trimmed.starts_with('{') || incoming_trimmed.starts_with('[');
    if incoming_is_jsonish {
        let incoming_is_complete =
            serde_json::from_str::<serde_json::Value>(incoming_trimmed).is_ok();
        let existing_is_complete =
            serde_json::from_str::<serde_json::Value>(existing_trimmed).is_ok();

        if incoming_is_complete
            || incoming_trimmed.starts_with(existing_trimmed)
            || (!existing_is_complete && incoming_trimmed.len() >= existing_trimmed.len())
        {
            existing.clear();
            existing.push_str(incoming);
            return;
        }
    }

    // Re-open heuristic: if we already have a complete JSON object and the
    // next fragment starts with `,`, strip the trailing `}` so the fragment
    // can be stitched in. This handles providers that flush `{"a":1}` then
    // send `,"b":2}` as a continuation.
    if incoming.trim_start().starts_with(',') && existing.trim_end().ends_with('}') {
        while existing.ends_with(char::is_whitespace) {
            existing.pop();
        }
        if existing.ends_with('}') {
            existing.pop();
        }
    }

    existing.push_str(incoming);
}

pub(super) fn finalize_tool_calls(accumulators: Vec<ToolCallAccumulator>) -> Vec<ToolCall> {
    let mut accumulators = accumulators;
    accumulators.sort_by_key(|acc| acc.index);

    accumulators
        .into_iter()
        // Skip accumulators with no name — these are incomplete fragments from
        // streaming (e.g. a Responses API function_call whose output_item.added
        // event never carried a `name` field). Sending a ToolCall with an empty
        // name back to the Responses API triggers a 400 "empty_string" error.
        .filter(|acc| !acc.name.is_empty())
        .map(|acc| {
            let arguments = match serde_json::from_str(&acc.arguments_json) {
                Ok(arguments) => arguments,
                Err(error) => {
                    warn!(
                        tool_index = acc.index,
                        tool_name = %acc.name,
                        error = %error,
                        payload_len = acc.arguments_json.len(),
                        "failed to parse streamed tool arguments; falling back to empty object"
                    );
                    serde_json::json!({})
                }
            };
            ToolCall {
                id: if acc.id.is_empty() {
                    Uuid::new_v4().to_string()
                } else {
                    acc.id
                },
                name: acc.name,
                arguments,
            }
        })
        .collect()
}

pub(super) fn parse_tool_calls(content: &str) -> Result<Vec<ToolCall>> {
    let Ok(value) = serde_json::from_str::<Value>(content) else {
        if looks_like_tool_response(content) {
            return Err(AvaError::SerializationError(format!(
                "malformed tool response envelope: {}",
                truncate_tool_response_preview(content)
            )));
        }
        return Ok(Vec::new());
    };

    let calls = if let Some(raw_calls) = value.get("tool_calls") {
        match serde_json::from_value::<Vec<ToolCallEnvelope>>(raw_calls.clone()) {
            Ok(calls) => calls,
            Err(error) => {
                warn!(
                    error = %error,
                    "tool response had invalid tool_calls schema, treating it as plain text"
                );
                Vec::new()
            }
        }
    } else if let Some(raw_call) = value.get("tool_call") {
        match serde_json::from_value::<ToolCallEnvelope>(raw_call.clone()) {
            Ok(call) => vec![call],
            Err(error) => {
                warn!(
                    error = %error,
                    "tool response had invalid tool_call schema, treating it as plain text"
                );
                Vec::new()
            }
        }
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

fn looks_like_tool_response(content: &str) -> bool {
    let trimmed = content.trim();
    (trimmed.starts_with('{') || trimmed.starts_with('['))
        && (trimmed.contains("\"tool_calls\"") || trimmed.contains("\"tool_call\""))
}

fn truncate_tool_response_preview(content: &str) -> String {
    let trimmed = content.trim();
    if trimmed.chars().count() <= 160 {
        trimmed.to_string()
    } else {
        let preview: String = trimmed.chars().take(160).collect();
        format!("{preview}...")
    }
}

pub(super) fn request_dedup_hash(messages: &[Message]) -> u64 {
    let mut hasher = DefaultHasher::new();
    // This is intentionally lightweight: we only need a short-lived guard
    // against immediate duplicate turns, not a globally collision-resistant key.
    messages.len().hash(&mut hasher);
    if let Some(last) = messages.last() {
        std::mem::discriminant(&last.role).hash(&mut hasher);
        last.content.hash(&mut hasher);
    }
    hasher.finish()
}

pub(super) struct PreparedRequest {
    pub messages: Vec<Message>,
    pub dedup_hash: u64,
}

/// Tools allowed in Plan mode. The LLM only sees these — write/edit are hidden.
/// Bash is included but restricted at execution time to read-only commands.
const PLAN_MODE_ALLOWED_TOOLS: &[&str] = &[
    "read",
    "glob",
    "grep",
    "web_fetch",
    "web_search",
    "git",
    "plan",
    "todo_read",
    "todo_write",
    "question",
    "codebase_search",
    "memory_read",
    "bash", // allowed but restricted to read-only commands at execution time
];

impl AgentLoop {
    pub(super) fn prepare_llm_request(&mut self) -> PreparedRequest {
        if self.context.needs_repair() {
            let mut msgs = self.context.get_messages().to_vec();
            let before = msgs.len();
            ava_types::repair_conversation(&mut msgs);
            if msgs.len() != before {
                tracing::debug!(
                    before,
                    after = msgs.len(),
                    removed = before - msgs.len(),
                    "repaired conversation history before LLM call"
                );
                self.context.replace_messages(msgs);
            } else {
                self.context.clear_needs_repair();
            }
        }

        let mut messages: Vec<Message> = self
            .context
            .get_messages()
            .iter()
            .filter(|m| m.agent_visible)
            .cloned()
            .collect();
        super::completion::ensure_tool_call_consistency(&mut messages);

        PreparedRequest {
            dedup_hash: request_dedup_hash(&messages),
            messages,
        }
    }

    pub(super) fn check_dedup_guard(
        &self,
        dedup_hash: u64,
    ) -> Option<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        if let (Some(prev_hash), Some(prev_time)) = (self.last_request_hash, self.last_request_time)
        {
            if dedup_hash == prev_hash && prev_time.elapsed().as_secs() < 2 {
                warn!("Skipping duplicate request (same content within 2s)");
                return Some((String::new(), vec![], None));
            }
        }
        None
    }

    /// Return the tool definitions that should be sent to the LLM, respecting
    /// the `extended_tools` config flag to filter by tier, and `plan_mode` to
    /// restrict to read-only tools.
    ///
    /// The result is cached because a single `AgentLoop` treats its tool set and
    /// plan-mode configuration as immutable for the lifetime of the loop.
    pub(super) fn active_tool_defs(&self) -> Vec<ava_types::Tool> {
        if let Some(cached) = self
            .cached_tool_defs
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone()
        {
            return cached;
        }

        let all_tools = if self.config.extended_tools {
            self.tools.list_tools_for_tiers(&[
                ToolTier::Default,
                ToolTier::Extended,
                ToolTier::Plugin,
            ])
        } else {
            self.tools
                .list_tools_for_tiers(&[ToolTier::Default, ToolTier::Plugin])
        };

        let mut tools = if self.config.plan_mode {
            all_tools
                .into_iter()
                .filter(|t| PLAN_MODE_ALLOWED_TOOLS.contains(&t.name.as_str()))
                .collect()
        } else {
            all_tools
        };

        tools = match self.tool_visibility_profile {
            crate::routing::ToolVisibilityProfile::Full => tools,
            crate::routing::ToolVisibilityProfile::ReadOnly => tools
                .into_iter()
                .filter(|t| crate::agent_loop::READ_ONLY_TOOLS.contains(&t.name.as_str()))
                .collect(),
            crate::routing::ToolVisibilityProfile::AnswerOnly => Vec::new(),
        };

        let mut cache = self
            .cached_tool_defs
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        *cache = Some(tools.clone());
        tools
    }

    /// Return tool definitions after applying the `tool.definition` plugin hook.
    ///
    /// Calls `active_tool_defs()` to get the base list, serialises each definition
    /// to JSON, passes them through any subscribed `tool.definition` plugins, then
    /// deserialises the (possibly modified) result back to `ava_types::Tool`.
    /// Definitions that fail to deserialise after a plugin modifies them are silently
    /// dropped.
    pub(super) async fn active_tool_defs_with_hooks(&self) -> Vec<ava_types::Tool> {
        if let Some(cached) = self
            .cached_hooked_tool_defs
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone()
        {
            return cached;
        }

        let base = self.active_tool_defs();

        let Some(ref pm) = self.plugin_manager else {
            return base;
        };

        let mut pm = pm.lock().await;
        if !pm.has_hook_subscribers(ava_plugin::HookEvent::ToolDefinition) {
            let mut cache = self
                .cached_hooked_tool_defs
                .lock()
                .unwrap_or_else(|error| error.into_inner());
            *cache = Some(base.clone());
            return base;
        }

        // Serialise to JSON so the plugin hook can inspect/modify them as plain data.
        let tool_json: Vec<serde_json::Value> = base
            .iter()
            .map(|t| {
                serde_json::json!({
                    "name": t.name,
                    "description": t.description,
                    "parameters": t.parameters,
                })
            })
            .collect();

        let modified = pm.apply_tool_definition_hook(tool_json).await;

        // Deserialise back to Tool structs.
        let tools: Vec<_> = modified
            .into_iter()
            .filter_map(|v| {
                let name = v.get("name")?.as_str()?.to_string();
                let description = v
                    .get("description")
                    .and_then(|d| d.as_str())
                    .unwrap_or("")
                    .to_string();
                let parameters = v
                    .get("parameters")
                    .cloned()
                    .unwrap_or(serde_json::json!({}));
                Some(ava_types::Tool {
                    name,
                    description,
                    parameters,
                })
            })
            .collect();

        let mut cache = self
            .cached_hooked_tool_defs
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        *cache = Some(tools.clone());
        tools
    }

    /// Generate a response, using native tool calling when the provider supports it.
    /// Returns (response_text, tool_calls, usage).
    #[cfg_attr(not(test), allow(dead_code))]
    pub(super) async fn generate_response(
        &mut self,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        let prepared = self.prepare_llm_request();
        if let Some(empty) = self.check_dedup_guard(prepared.dedup_hash) {
            return Ok(empty);
        }
        self.generate_response_prepared(prepared).await
    }

    /// Like `generate_response` but uses thinking when configured.
    pub(super) async fn generate_response_with_thinking(
        &mut self,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        let prepared = self.prepare_llm_request();
        self.generate_response_with_thinking_prepared(prepared)
            .await
    }

    pub(super) async fn generate_response_with_thinking_prepared(
        &mut self,
        prepared: PreparedRequest,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        if self.config.thinking_level == ThinkingLevel::Off {
            return self.generate_response_prepared(prepared).await;
        }
        let messages = prepared.messages.as_slice();

        let timeout_secs = self.config.stream_timeout_secs;
        let result = if self.llm.supports_tools() {
            let tool_defs = self.active_tool_defs_with_hooks().await;
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
            let fut = self
                .llm
                .generate_with_thinking_config(messages, &tool_defs, thinking);
            let response = if timeout_secs > 0 {
                tokio::time::timeout(Duration::from_secs(timeout_secs), fut)
                    .await
                    .map_err(|_| AvaError::ProviderError {
                        provider: self.config.model.clone(),
                        message: format!(
                            "LLM request timed out after {timeout_secs} seconds. \
                             The provider may be overloaded."
                        ),
                    })??
            } else {
                fut.await?
            };
            Ok((response.content, response.tool_calls, response.usage))
        } else {
            let fut = self.llm.generate(messages);
            let response = if timeout_secs > 0 {
                tokio::time::timeout(Duration::from_secs(timeout_secs), fut)
                    .await
                    .map_err(|_| AvaError::ProviderError {
                        provider: self.config.model.clone(),
                        message: format!(
                            "LLM request timed out after {timeout_secs} seconds. \
                             The provider may be overloaded."
                        ),
                    })??
            } else {
                fut.await?
            };
            let tool_calls = parse_tool_calls(&response)?;
            Ok((response, tool_calls, None))
        };

        // Only set dedup hash for non-empty responses (see generate_response comment).
        result
    }

    async fn generate_response_prepared(
        &mut self,
        prepared: PreparedRequest,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        let messages = prepared.messages.as_slice();

        let timeout_secs = self.config.stream_timeout_secs;
        let result = if self.llm.supports_tools() {
            let tool_defs = self.active_tool_defs_with_hooks().await;
            let fut = self.llm.generate_with_tools(messages, &tool_defs);
            let response = if timeout_secs > 0 {
                tokio::time::timeout(Duration::from_secs(timeout_secs), fut)
                    .await
                    .map_err(|_| AvaError::ProviderError {
                        provider: self.config.model.clone(),
                        message: format!(
                            "LLM request timed out after {timeout_secs} seconds. \
                             The provider may be overloaded."
                        ),
                    })??
            } else {
                fut.await?
            };
            Ok((response.content, response.tool_calls, response.usage))
        } else {
            let fut = self.llm.generate(messages);
            let response = if timeout_secs > 0 {
                tokio::time::timeout(Duration::from_secs(timeout_secs), fut)
                    .await
                    .map_err(|_| AvaError::ProviderError {
                        provider: self.config.model.clone(),
                        message: format!(
                            "LLM request timed out after {timeout_secs} seconds. \
                             The provider may be overloaded."
                        ),
                    })??
            } else {
                fut.await?
            };
            let tool_calls = parse_tool_calls(&response)?;
            Ok((response, tool_calls, None))
        };

        result
    }
}

#[cfg(test)]
mod tests {
    use std::pin::Pin;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    use async_trait::async_trait;
    use ava_context::ContextManager;
    use ava_tools::registry::ToolRegistry;
    use ava_types::Role;
    use futures::Stream;

    use super::*;

    struct CountingProvider {
        calls: Arc<AtomicUsize>,
        fail: bool,
    }

    #[async_trait]
    impl crate::llm_trait::LLMProvider for CountingProvider {
        async fn generate(&self, _messages: &[Message]) -> Result<String> {
            self.calls.fetch_add(1, Ordering::SeqCst);
            if self.fail {
                Err(AvaError::TimeoutError("transient failure".to_string()))
            } else {
                Ok("ok".to_string())
            }
        }

        async fn generate_stream(
            &self,
            _messages: &[Message],
        ) -> Result<Pin<Box<dyn Stream<Item = ava_types::StreamChunk> + Send>>> {
            unreachable!("streaming is not used in these tests")
        }

        fn estimate_tokens(&self, input: &str) -> usize {
            input.len()
        }

        fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
            0.0
        }

        fn model_name(&self) -> &str {
            "counting-provider"
        }
    }

    fn test_config() -> super::super::AgentConfig {
        super::super::AgentConfig {
            max_turns: 1,
            max_budget_usd: 0.0,
            token_limit: 4_096,
            provider: String::new(),
            model: "mock-model".to_string(),
            max_cost_usd: 1.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            auto_compact: true,
            post_edit_validation: None,
            stream_timeout_secs: 90,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        }
    }

    #[test]
    fn finalize_tool_calls_sorts_by_stream_index() {
        let tool_calls = finalize_tool_calls(vec![
            ToolCallAccumulator {
                index: 2,
                id: "call-3".to_string(),
                name: "third".to_string(),
                arguments_json: r#"{"path":"/tmp/three"}"#.to_string(),
            },
            ToolCallAccumulator {
                index: 0,
                id: "call-1".to_string(),
                name: "first".to_string(),
                arguments_json: r#"{"path":"/tmp/one"}"#.to_string(),
            },
            ToolCallAccumulator {
                index: 1,
                id: "call-2".to_string(),
                name: "second".to_string(),
                arguments_json: r#"{"path":"/tmp/two"}"#.to_string(),
            },
        ]);

        let ordered_names: Vec<&str> = tool_calls.iter().map(|call| call.name.as_str()).collect();
        assert_eq!(ordered_names, vec!["first", "second", "third"]);
    }

    #[test]
    fn accumulate_tool_call_prefers_complete_arguments_from_done_event() {
        let mut accumulators = Vec::new();

        accumulate_tool_call(
            &mut accumulators,
            &StreamToolCall {
                index: 0,
                id: Some("call-1".to_string()),
                name: Some("glob".to_string()),
                arguments_delta: None,
            },
        );
        accumulate_tool_call(
            &mut accumulators,
            &StreamToolCall {
                index: 0,
                id: Some("call-1".to_string()),
                name: Some("glob".to_string()),
                arguments_delta: Some("{\"pattern\":\"**/*.md\"}".to_string()),
            },
        );

        let tool_calls = finalize_tool_calls(accumulators);
        assert_eq!(tool_calls.len(), 1);
        assert_eq!(
            tool_calls[0].arguments,
            serde_json::json!({"pattern": "**/*.md"})
        );
    }

    #[test]
    fn accumulate_tool_call_ignores_duplicate_complete_arguments() {
        let mut accumulators = Vec::new();

        accumulate_tool_call(
            &mut accumulators,
            &StreamToolCall {
                index: 0,
                id: Some("call-1".to_string()),
                name: Some("glob".to_string()),
                arguments_delta: Some("{\"pattern\":\"**/*.md\"}".to_string()),
            },
        );
        accumulate_tool_call(
            &mut accumulators,
            &StreamToolCall {
                index: 0,
                id: Some("call-1".to_string()),
                name: Some("glob".to_string()),
                arguments_delta: Some("{\"pattern\":\"**/*.md\"}".to_string()),
            },
        );

        let tool_calls = finalize_tool_calls(accumulators);
        assert_eq!(tool_calls.len(), 1);
        assert_eq!(
            tool_calls[0].arguments,
            serde_json::json!({"pattern": "**/*.md"})
        );
    }

    #[test]
    fn accumulator_is_complete_with_valid_json() {
        let acc = ToolCallAccumulator {
            index: 0,
            id: "call-1".to_string(),
            name: "read".to_string(),
            arguments_json: r#"{"path":"src/main.rs"}"#.to_string(),
        };
        assert!(acc.is_complete());
        let tc = acc.to_tool_call().unwrap();
        assert_eq!(tc.name, "read");
        assert_eq!(tc.arguments["path"], "src/main.rs");
    }

    #[test]
    fn accumulator_is_incomplete_with_partial_json() {
        let acc = ToolCallAccumulator {
            index: 0,
            id: "call-1".to_string(),
            name: "read".to_string(),
            arguments_json: r#"{"path":"src/m"#.to_string(),
        };
        assert!(!acc.is_complete());
        assert!(acc.to_tool_call().is_none());
    }

    #[test]
    fn accumulator_is_incomplete_without_name() {
        let acc = ToolCallAccumulator {
            index: 0,
            id: "call-1".to_string(),
            name: String::new(),
            arguments_json: r#"{"path":"x"}"#.to_string(),
        };
        assert!(!acc.is_complete());
    }

    #[test]
    fn parse_tool_calls_accepts_plain_text_responses() {
        let calls = parse_tool_calls("I checked the file and it looks good.").unwrap();
        assert!(calls.is_empty());
    }

    #[test]
    fn merge_tool_arguments_keeps_appending_after_valid_partial_json() {
        let mut existing = r#"{"pattern":"*.rs"}"#.to_string();
        merge_tool_arguments(&mut existing, r#","path":"src"}"#);
        assert_eq!(existing, r#"{"pattern":"*.rs","path":"src"}"#);
    }

    #[test]
    fn merge_tool_arguments_replaces_with_new_complete_snapshot() {
        let mut existing = r#"{"path":"src/main.rs"}"#.to_string();
        merge_tool_arguments(
            &mut existing,
            r#"{"path":"src/main.rs","offset":1,"limit":200}"#,
        );
        assert_eq!(existing, r#"{"path":"src/main.rs","offset":1,"limit":200}"#);
    }

    #[test]
    fn merge_tool_arguments_replaces_with_growing_json_snapshot() {
        let mut existing = r#"{"path":"src""#.to_string();
        merge_tool_arguments(&mut existing, r#"{"path":"src/main.rs""#);
        assert_eq!(existing, r#"{"path":"src/main.rs""#);
    }

    #[test]
    fn parse_tool_calls_rejects_malformed_tool_envelope() {
        let error = parse_tool_calls("{\"tool_calls\":[")
            .expect_err("malformed tool envelope should error");
        assert!(error
            .to_string()
            .contains("malformed tool response envelope"));
    }

    #[test]
    fn parse_tool_calls_treats_schema_mismatches_as_plain_text() {
        let calls = parse_tool_calls("{\"tool_calls\":\"not-an-array\"}").unwrap();
        assert!(calls.is_empty());
    }

    #[tokio::test]
    async fn generate_response_does_not_cache_failed_requests() {
        let calls = Arc::new(AtomicUsize::new(0));
        let provider = CountingProvider {
            calls: calls.clone(),
            fail: true,
        };
        let mut agent = AgentLoop::new(
            Box::new(provider),
            ToolRegistry::new(),
            ContextManager::new(4_096),
            test_config(),
        );
        agent
            .context
            .add_message(Message::new(Role::User, "same request"));

        assert!(agent.generate_response().await.is_err());
        assert!(agent.generate_response().await.is_err());
        assert_eq!(calls.load(Ordering::SeqCst), 2);
        assert!(agent.last_request_hash.is_none());
        assert!(agent.last_request_time.is_none());
    }

    #[tokio::test]
    async fn generate_response_hash_changes_when_message_count_changes() {
        let calls = Arc::new(AtomicUsize::new(0));
        let provider = CountingProvider {
            calls: calls.clone(),
            fail: false,
        };
        let mut agent = AgentLoop::new(
            Box::new(provider),
            ToolRegistry::new(),
            ContextManager::new(4_096),
            test_config(),
        );
        agent.context.replace_messages(vec![
            Message::new(Role::System, "system"),
            Message::new(Role::User, "same request"),
        ]);

        let first = agent.generate_response().await.unwrap();
        assert_eq!(first.0, "ok");

        agent.context.replace_messages(vec![
            Message::new(Role::System, "system"),
            Message::new(Role::Assistant, "intermediate"),
            Message::new(Role::User, "same request"),
        ]);

        let second = agent.generate_response().await.unwrap();
        assert_eq!(second.0, "ok");
        assert_eq!(calls.load(Ordering::SeqCst), 2);
    }
}
