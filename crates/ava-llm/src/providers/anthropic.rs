use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::{debug, instrument, trace, warn};

use crate::circuit_breaker::CircuitBreaker;
use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse};
use crate::providers::common;

const ANTHROPIC_BASE_URL: &str = "https://api.anthropic.com";

/// Models that support Anthropic's adaptive thinking mode.
/// These models use the `thinking: { type: "adaptive" }` API.
const ADAPTIVE_THINKING_MODELS: &[&str] = &[
    "claude-opus-4-6",
    "claude-opus-4.6",
    "claude-sonnet-4-6",
    "claude-sonnet-4.6",
];

#[derive(Clone)]
pub struct AnthropicProvider {
    pool: Arc<ConnectionPool>,
    api_key: String,
    model: String,
    max_tokens: usize,
    base_url: String,
    /// Whether this is a third-party Anthropic-compatible provider (skip Anthropic-specific headers).
    third_party: bool,
    circuit_breaker: Option<Arc<CircuitBreaker>>,
}

impl AnthropicProvider {
    pub fn new(pool: Arc<ConnectionPool>, api_key: impl Into<String>, model: impl Into<String>) -> Self {
        Self {
            pool,
            api_key: api_key.into(),
            model: model.into(),
            max_tokens: 4096,
            base_url: ANTHROPIC_BASE_URL.to_string(),
            third_party: false,
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    pub fn with_base_url(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
        base_url: impl Into<String>,
    ) -> Self {
        Self {
            pool,
            api_key: api_key.into(),
            model: model.into(),
            max_tokens: 4096,
            base_url: base_url.into(),
            third_party: true,
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    /// Check if the current model supports Anthropic's adaptive thinking mode.
    fn supports_adaptive_thinking(&self) -> bool {
        let model_lower = self.model.to_lowercase();
        ADAPTIVE_THINKING_MODELS
            .iter()
            .any(|&m| model_lower.contains(&m.to_lowercase()))
    }

    /// Check if this is a Kimi model that uses `thinking: { type: "enabled", budgetTokens }`.
    fn is_kimi_thinking_model(&self) -> bool {
        let m = self.model.to_lowercase();
        m.contains("k2p5") || m.contains("kimi-k2.5") || m.contains("kimi-k2p5")
    }

    fn build_request_body(&self, messages: &[Message], stream: bool) -> Value {
        let (system, mapped_messages) = common::map_messages_anthropic(messages);
        let mut body = json!({
            "model": self.model,
            "max_tokens": self.max_tokens,
            "messages": mapped_messages,
            "stream": stream,
        });

        if let Some(system_message) = system {
            body["system"] = json!([{
                "type": "text",
                "text": system_message,
                "cache_control": {"type": "ephemeral"}
            }]);
        }

        body
    }

    fn build_request_body_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
    ) -> Value {
        let mut body = self.build_request_body(messages, stream);
        if !tools.is_empty() {
            body["tools"] = json!(common::tools_to_anthropic_format(tools));
        }
        body
    }

    /// Build request body with thinking support for adaptive thinking models.
    fn build_request_body_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
        thinking: ThinkingLevel,
    ) -> Value {
        let mut body = self.build_request_body(messages, stream);
        
        if !tools.is_empty() {
            body["tools"] = json!(common::tools_to_anthropic_format(tools));
        }

        if thinking != ThinkingLevel::Off && self.is_kimi_thinking_model() {
            // Kimi K2.5 thinking: type "enabled" with budgetTokens
            // budgetTokens = min(16000, max_output/2 - 1)
            let max_output = body["max_tokens"].as_u64().unwrap_or(4096);
            let budget = std::cmp::min(16000, max_output / 2 - 1);
            body["thinking"] = json!({ "type": "enabled", "budgetTokens": budget });

            if self.max_tokens < 16000 {
                body["max_tokens"] = json!(16000);
            }
        } else if thinking != ThinkingLevel::Off && self.supports_adaptive_thinking() {
            // Anthropic adaptive thinking API:
            // - thinking.type = "adaptive"
            // - output_config.effort = "low" | "medium" | "high" | "max"
            // Note: budget_tokens is NOT used with adaptive mode (it's for type: "enabled")
            let effort = match thinking {
                ThinkingLevel::Off => unreachable!(),
                ThinkingLevel::Low => "low",
                ThinkingLevel::Medium => "medium",
                ThinkingLevel::High => "high",
                ThinkingLevel::Max => "max",
            };

            body["thinking"] = json!({ "type": "adaptive" });
            body["output_config"] = json!({ "effort": effort });

            // Ensure max_tokens is reasonably large for thinking-enabled responses
            // Anthropic examples use 16000 as a baseline
            if self.max_tokens < 16000 {
                body["max_tokens"] = json!(16000);
            }
        }

        body
    }

    /// Parse thinking content from Anthropic response.
    /// Thinking blocks have type "thinking" with a "thinking" field.
    fn parse_thinking(&self, payload: &Value) -> Option<String> {
        let content = payload.get("content")?.as_array()?;
        let thinking_parts: Vec<String> = content
            .iter()
            .filter(|block| block.get("type").and_then(Value::as_str) == Some("thinking"))
            .filter_map(|block| block.get("thinking").and_then(Value::as_str))
            .map(String::from)
            .collect();
        
        if thinking_parts.is_empty() {
            None
        } else {
            Some(thinking_parts.join("\n"))
        }
    }

    /// Build the full messages endpoint URL, avoiding double `/v1` for
    /// third-party providers whose base URL already ends with `/v1`.
    fn messages_url(&self) -> String {
        if self.base_url.ends_with("/v1") {
            format!("{}/messages", self.base_url)
        } else {
            format!("{}/v1/messages", self.base_url)
        }
    }

    /// Build a base request with appropriate headers.
    /// Both native and third-party providers get `anthropic-version` (required by
    /// Anthropic-compatible APIs like Alibaba Coding Plan, Kimi, MiniMax).
    /// Third-party providers also get a coding-agent `User-Agent` header
    /// (required by some providers that gate access to coding agents).
    fn build_request(&self, client: &reqwest::Client) -> reqwest::RequestBuilder {
        let req = client
            .post(self.messages_url())
            .header("x-api-key", &self.api_key)
            .header("anthropic-version", "2023-06-01");
        if self.third_party {
            req.header("User-Agent", "ava/coding-agent")
        } else {
            req
        }
    }

    async fn client(&self) -> Result<Arc<reqwest::Client>> {
        self.pool.get_client(&self.base_url).await
    }

    async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
        common::send_with_retry_cb(request, "Anthropic", 3, self.circuit_breaker.as_deref()).await
    }
}

#[async_trait]
impl LLMProvider for AnthropicProvider {
    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let client = self.client().await?;
        let request = self.build_request(&client)
            .json(&self.build_request_body(messages, false));

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Anthropic").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        debug!(provider = "Anthropic", third_party = self.third_party, "raw response payload: {payload}");

        common::parse_anthropic_completion_payload(&payload)
    }

    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let client = self.client().await?;
        let request = self.build_request(&client)
            .json(&self.build_request_body(messages, true));

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Anthropic").await?;
        Ok(Box::pin(sse_to_stream(response, self.third_party)))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        common::estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        let (in_rate, out_rate) = common::model_pricing_usd_per_million(&self.model);
        common::estimate_cost_usd(input_tokens, output_tokens, in_rate, out_rate)
    }

    fn model_name(&self) -> &str {
        &self.model
    }

    fn supports_tools(&self) -> bool {
        true
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model))]
    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<LLMResponse> {
        let body = self.build_request_body_with_tools(messages, tools, false);
        let client = self.client().await?;
        let request = self.build_request(&client).json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Anthropic").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        debug!(provider = "Anthropic", third_party = self.third_party, "raw response payload: {payload}");

        let content = common::parse_anthropic_completion_payload(&payload).unwrap_or_else(|e| {
            warn!(provider = "Anthropic", third_party = self.third_party, "failed to parse completion: {e}");
            String::new()
        });
        let tool_calls = common::parse_anthropic_tool_calls(&payload);
        let usage = common::parse_usage(&payload);

        if content.is_empty() && tool_calls.is_empty() {
            warn!(
                provider = "Anthropic",
                third_party = self.third_party,
                model = %self.model,
                "empty response from provider — no content and no tool calls. \
                 Raw payload keys: {:?}. Content field: {:?}. \
                 This may indicate a response format mismatch for a third-party Anthropic-compatible API.",
                payload.as_object().map(|o| o.keys().collect::<Vec<_>>()),
                payload.get("content"),
            );
        }

        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
            thinking: None,
        })
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model))]
    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let body = self.build_request_body_with_tools(messages, tools, true);
        let client = self.client().await?;
        let request = self.build_request(&client).json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Anthropic").await?;
        Ok(Box::pin(sse_to_stream(response, self.third_party)))
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        if (!self.supports_adaptive_thinking() && !self.is_kimi_thinking_model()) || thinking == ThinkingLevel::Off {
            return self.generate_stream_with_tools(messages, tools).await;
        }

        let body = self.build_request_body_with_thinking(messages, tools, true, thinking);
        let client = self.client().await?;
        let mut request = self.build_request(&client);
        if !self.third_party {
            request = request.header("anthropic-beta", "interleaved-thinking-2025-05-14");
        }
        let request = request.json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Anthropic").await?;
        Ok(Box::pin(sse_to_stream(response, self.third_party)))
    }

    fn supports_thinking(&self) -> bool {
        self.supports_adaptive_thinking() || self.is_kimi_thinking_model()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        if self.supports_adaptive_thinking() {
            &[
                ThinkingLevel::Low,
                ThinkingLevel::Medium,
                ThinkingLevel::High,
                ThinkingLevel::Max,
            ]
        } else if self.is_kimi_thinking_model() {
            // Kimi uses budget-based thinking, expose as a single "High" level
            &[ThinkingLevel::High]
        } else {
            &[]
        }
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        // For non-thinking models, fall back to standard generate_with_tools
        if (!self.supports_adaptive_thinking() && !self.is_kimi_thinking_model()) || thinking == ThinkingLevel::Off {
            return self.generate_with_tools(messages, tools).await;
        }

        let body = self.build_request_body_with_thinking(messages, tools, false, thinking);
        let client = self.client().await?;
        let mut request = self.build_request(&client);
        if !self.third_party {
            request = request.header("anthropic-beta", "interleaved-thinking-2025-05-14");
        }
        let request = request.json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Anthropic").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        debug!(provider = "Anthropic", third_party = self.third_party, "raw thinking response payload: {payload}");

        // When thinking is enabled, content blocks come in order: thinking, text, tool_use
        // We need to find the text block specifically, not just the first block
        let content = payload
            .get("content")
            .and_then(Value::as_array)
            .and_then(|content| {
                content
                    .iter()
                    .find(|block| block.get("type").and_then(Value::as_str) == Some("text"))
                    .and_then(|block| block.get("text").and_then(Value::as_str))
                    .map(String::from)
            })
            .unwrap_or_default();

        let tool_calls = common::parse_anthropic_tool_calls(&payload);
        let usage = common::parse_usage(&payload);
        let thinking_content = self.parse_thinking(&payload);

        if content.is_empty() && tool_calls.is_empty() {
            warn!(
                provider = "Anthropic",
                third_party = self.third_party,
                model = %self.model,
                "empty thinking response — no content and no tool calls. \
                 Raw payload keys: {:?}. Content field: {:?}.",
                payload.as_object().map(|o| o.keys().collect::<Vec<_>>()),
                payload.get("content"),
            );
        }

        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
            thinking: thinking_content,
        })
    }
}

/// Convert a reqwest SSE response into a `Stream<Item = StreamChunk>`.
/// Logs parsing details at debug level so they appear in the always-on file log.
fn sse_to_stream(
    response: reqwest::Response,
    third_party: bool,
) -> impl Stream<Item = StreamChunk> {
    response.bytes_stream().flat_map(move |chunk| {
        let chunks = chunk
            .ok()
            .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
            .map(|text| {
                trace!(provider = "Anthropic", third_party, "SSE raw: {text}");
                let sse_lines = common::parse_sse_lines(&text);
                trace!(provider = "Anthropic", third_party, lines = sse_lines.len(), "SSE parsed data lines");
                sse_lines
                    .into_iter()
                    .filter_map(|line| {
                        let parsed = serde_json::from_str::<Value>(&line);
                        if let Err(ref e) = parsed {
                            warn!(provider = "Anthropic", third_party, "SSE JSON parse error: {e} — line: {line}");
                        }
                        parsed.ok()
                    })
                    .filter_map(|payload| {
                        let event_type = payload.get("type").and_then(Value::as_str).unwrap_or("unknown");
                        let result = common::parse_anthropic_stream_chunk(&payload);
                        if result.is_none() && event_type != "ping" && event_type != "message_stop" {
                            trace!(provider = "Anthropic", third_party, event_type, "SSE event produced no StreamChunk");
                        }
                        result
                    })
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default();

        futures::stream::iter(chunks)
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pool::ConnectionPool;

    fn pool() -> Arc<ConnectionPool> {
        Arc::new(ConnectionPool::new())
    }

    #[test]
    fn messages_url_native_anthropic_appends_v1() {
        let provider = AnthropicProvider::new(pool(), "key", "claude-sonnet-4");
        assert_eq!(
            provider.messages_url(),
            "https://api.anthropic.com/v1/messages"
        );
    }

    #[test]
    fn messages_url_third_party_with_v1_suffix_no_double() {
        // All Anthropic-compatible coding plan providers end with /v1
        let urls = [
            "https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1",
            "https://coding.dashscope.aliyuncs.com/apps/anthropic/v1",
            "https://api.kimi.com/coding/v1",
            "https://api.minimax.io/anthropic/v1",
            "https://api.minimaxi.com/anthropic/v1",
        ];

        for base_url in urls {
            let provider = AnthropicProvider::with_base_url(pool(), "key", "model", base_url);
            let url = provider.messages_url();
            assert!(
                url.ends_with("/v1/messages"),
                "URL should end with /v1/messages, got: {url}"
            );
            assert!(
                !url.contains("/v1/v1/"),
                "URL should not contain double /v1/v1/, got: {url}"
            );
        }
    }

    #[test]
    fn messages_url_custom_base_without_v1() {
        let provider = AnthropicProvider::with_base_url(
            pool(),
            "key",
            "model",
            "https://custom-proxy.example.com",
        );
        assert_eq!(
            provider.messages_url(),
            "https://custom-proxy.example.com/v1/messages"
        );
    }
}
