use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::time::{Duration, Instant};

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
        acc.arguments_json.push_str(args);
    }
}

pub(super) fn finalize_tool_calls(accumulators: Vec<ToolCallAccumulator>) -> Vec<ToolCall> {
    accumulators
        .into_iter()
        // Skip accumulators with no name — these are incomplete fragments from
        // streaming (e.g. a Responses API function_call whose output_item.added
        // event never carried a `name` field). Sending a ToolCall with an empty
        // name back to the Responses API triggers a 400 "empty_string" error.
        .filter(|acc| !acc.name.is_empty())
        .map(|acc| {
            let arguments =
                serde_json::from_str(&acc.arguments_json).unwrap_or(serde_json::json!({}));
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
        return Ok(Vec::new());
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

        let tools = if self.config.plan_mode {
            all_tools
                .into_iter()
                .filter(|t| PLAN_MODE_ALLOWED_TOOLS.contains(&t.name.as_str()))
                .collect()
        } else {
            all_tools
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

        if !pm
            .lock()
            .await
            .has_hook_subscribers(ava_plugin::HookEvent::ToolDefinition)
        {
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

        let modified = pm.lock().await.apply_tool_definition_hook(tool_json).await;

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
    pub(super) async fn generate_response(
        &mut self,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        // Dedup guard: skip if identical request within 2s
        // Only send agent-visible messages to the LLM (compacted messages excluded).
        let messages: Vec<Message> = self
            .context
            .get_messages()
            .iter()
            .filter(|m| m.agent_visible)
            .cloned()
            .collect();
        let messages = messages.as_slice();
        let hash = {
            let mut hasher = DefaultHasher::new();
            if let Some(last) = messages.last() {
                last.content.hash(&mut hasher);
            }
            hasher.finish()
        };
        if let (Some(prev_hash), Some(prev_time)) = (self.last_request_hash, self.last_request_time)
        {
            if hash == prev_hash && prev_time.elapsed().as_secs() < 2 {
                warn!("Skipping duplicate request (same content within 2s)");
                return Ok((String::new(), vec![], None));
            }
        }

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

        // Only set dedup hash when we got a non-empty response, so that
        // empty responses (e.g. from format mismatches) don't prevent the
        // next turn from making a real API call.
        if let Ok((ref text, ref calls, _)) = result {
            if !text.trim().is_empty() || !calls.is_empty() {
                self.last_request_hash = Some(hash);
                self.last_request_time = Some(Instant::now());
            }
        } else {
            self.last_request_hash = Some(hash);
            self.last_request_time = Some(Instant::now());
        }

        result
    }

    /// Like `generate_response` but uses thinking when configured.
    pub(super) async fn generate_response_with_thinking(
        &mut self,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        if self.config.thinking_level == ThinkingLevel::Off {
            return self.generate_response().await;
        }

        // Dedup guard
        // Only send agent-visible messages to the LLM (compacted messages excluded).
        let messages: Vec<Message> = self
            .context
            .get_messages()
            .iter()
            .filter(|m| m.agent_visible)
            .cloned()
            .collect();
        let messages = messages.as_slice();
        let hash = {
            let mut hasher = DefaultHasher::new();
            if let Some(last) = messages.last() {
                last.content.hash(&mut hasher);
            }
            hasher.finish()
        };
        if let (Some(prev_hash), Some(prev_time)) = (self.last_request_hash, self.last_request_time)
        {
            if hash == prev_hash && prev_time.elapsed().as_secs() < 2 {
                warn!("Skipping duplicate request (same content within 2s)");
                return Ok((String::new(), vec![], None));
            }
        }

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
        if let Ok((ref text, ref calls, _)) = result {
            if !text.trim().is_empty() || !calls.is_empty() {
                self.last_request_hash = Some(hash);
                self.last_request_time = Some(Instant::now());
            }
        } else {
            self.last_request_hash = Some(hash);
            self.last_request_time = Some(Instant::now());
        }

        result
    }
}
