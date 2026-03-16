use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::time::Instant;

use ava_llm::ThinkingConfig;
use ava_types::{AvaError, Result, StreamToolCall, ThinkingLevel, TokenUsage, ToolCall};
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

impl AgentLoop {
    /// Return the tool definitions that should be sent to the LLM, respecting
    /// the `extended_tools` config flag to filter by tier.
    pub(super) fn active_tool_defs(&self) -> Vec<ava_types::Tool> {
        if self.config.extended_tools {
            self.tools.list_tools_for_tiers(&[
                ToolTier::Default,
                ToolTier::Extended,
                ToolTier::Plugin,
            ])
        } else {
            self.tools
                .list_tools_for_tiers(&[ToolTier::Default, ToolTier::Plugin])
        }
    }

    /// Generate a response, using native tool calling when the provider supports it.
    /// Returns (response_text, tool_calls, usage).
    pub(super) async fn generate_response(
        &mut self,
    ) -> Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        // Dedup guard: skip if identical request within 2s
        let messages = self.context.get_messages();
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

        let result = if self.llm.supports_tools() {
            let tool_defs = self.active_tool_defs();
            let response = self.llm.generate_with_tools(messages, &tool_defs).await?;
            Ok((response.content, response.tool_calls, response.usage))
        } else {
            // Gap: LLMProvider::generate() returns Result<String>, so no token usage
            // is available when the provider doesn't support native tool calling.
            // Fixing this requires changing the trait signature to return a struct
            // with both text and optional usage.
            let response = self.llm.generate(messages).await?;
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
        let messages = self.context.get_messages();
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

        let result = if self.llm.supports_tools() {
            let tool_defs = self.active_tool_defs();
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
            let response = self
                .llm
                .generate_with_thinking_config(messages, &tool_defs, thinking)
                .await?;
            Ok((response.content, response.tool_calls, response.usage))
        } else {
            // Gap: LLMProvider::generate() returns Result<String>, so no token usage
            // is available when the provider doesn't support native tool calling.
            // Fixing this requires changing the trait signature to return a struct
            // with both text and optional usage.
            let response = self.llm.generate(messages).await?;
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
