//! GitHub Copilot LLM provider.
//!
//! Uses OpenAI-compatible chat completions API through the Copilot proxy.
//! All models (Claude, GPT, Gemini) go through the same `/chat/completions` endpoint.
//! The Copilot proxy handles routing to the appropriate backend.

use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, Role, StreamChunk, ThinkingLevel};
use futures::{Stream, StreamExt};
use reqwest::header::{HeaderMap, HeaderValue};
use serde_json::{json, Value};
use tokio::sync::RwLock;
use tracing::instrument;

use crate::circuit_breaker::CircuitBreaker;
use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse};
use crate::providers::common;

/// Re-export for use from providers::mod.
pub use ava_auth::copilot::CopilotToken;

/// GitHub Copilot LLM provider.
///
/// Exchanges a GitHub OAuth token for a short-lived Copilot API token,
/// caches it in memory, and re-exchanges when expired (~30 min lifetime).
pub struct CopilotProvider {
    pool: Arc<ConnectionPool>,
    github_oauth_token: String,
    model: String,
    cached_token: Arc<RwLock<Option<CopilotToken>>>,
    circuit_breaker: Option<Arc<CircuitBreaker>>,
}

impl CopilotProvider {
    pub fn new(
        pool: Arc<ConnectionPool>,
        github_oauth_token: impl Into<String>,
        model: impl Into<String>,
    ) -> Self {
        Self {
            pool,
            github_oauth_token: github_oauth_token.into(),
            model: model.into(),
            cached_token: Arc::new(RwLock::new(None)),
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    /// Ensure we have a valid Copilot API token, exchanging if needed.
    async fn ensure_token(&self) -> Result<CopilotToken> {
        // Check cache first
        {
            let cached = self.cached_token.read().await;
            if let Some(ref token) = *cached {
                if !token.is_expired() {
                    return Ok(token.clone());
                }
            }
        }

        // Exchange for new token
        let token =
            ava_auth::copilot::exchange_copilot_token(&self.github_oauth_token)
                .await
                .map_err(|e| AvaError::ProviderError {
                    provider: "copilot".to_string(),
                    message: e.to_string(),
                })?;

        // Pre-warm the connection pool for the resolved endpoint
        let _ = self.pool.get_client(&token.api_endpoint).await;

        let result = token.clone();
        *self.cached_token.write().await = Some(token);
        Ok(result)
    }

    /// Determine x-initiator value from message history.
    /// Agent-initiated = last message role is NOT "user" (tool responses, continuations).
    fn infer_initiator(messages: &[Message]) -> &'static str {
        match messages.last() {
            Some(msg) if msg.role == Role::User => "user",
            _ => "agent",
        }
    }

    /// Build Copilot-specific headers for API requests.
    fn build_headers(token: &str, initiator: &str) -> HeaderMap {
        let mut headers = HeaderMap::new();
        headers.insert(
            "Authorization",
            HeaderValue::from_str(&format!("Bearer {token}")).unwrap_or_else(|_| {
                HeaderValue::from_static("Bearer invalid")
            }),
        );
        headers.insert("X-Initiator", HeaderValue::from_static(
            if initiator == "user" { "user" } else { "agent" }
        ));
        headers.insert("Openai-Intent", HeaderValue::from_static("conversation-edits"));
        headers.insert("User-Agent", HeaderValue::from_static("GitHubCopilotChat/0.35.0"));
        headers.insert("Editor-Version", HeaderValue::from_static("vscode/1.107.0"));
        headers.insert(
            "Editor-Plugin-Version",
            HeaderValue::from_static("copilot-chat/0.35.0"),
        );
        headers.insert(
            "Copilot-Integration-Id",
            HeaderValue::from_static("vscode-chat"),
        );
        headers
    }

    fn build_request_body(&self, messages: &[Message], stream: bool) -> Value {
        json!({
            "model": self.model,
            "messages": common::map_messages_openai(messages),
            "stream": stream,
        })
    }

    async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
        common::send_with_retry_cb(request, "Copilot", 3, self.circuit_breaker.as_deref()).await
    }

    fn build_request_body_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
    ) -> Value {
        let mut body = self.build_request_body(messages, stream);
        if !tools.is_empty() {
            body["tools"] = json!(common::tools_to_openai_format(tools));
        }
        body
    }

    /// Whether the current model is a Claude model (routes to Anthropic backend).
    fn is_claude_model(&self) -> bool {
        self.model.to_lowercase().contains("claude")
    }

    /// Whether the current model is an OpenAI reasoning model (o3, o4, gpt-5).
    fn is_openai_reasoning_model(&self) -> bool {
        let m = self.model.to_lowercase();
        m.starts_with("o3") || m.starts_with("o4") || m.contains("gpt-5")
    }

    /// Whether the current model supports thinking/reasoning through the Copilot proxy.
    fn supports_reasoning(&self) -> bool {
        self.is_claude_model() || self.is_openai_reasoning_model()
    }

    /// Map a ThinkingLevel to the reasoning_effort string value.
    /// Copilot uses OpenAI-compatible format for all backends, so reasoning_effort
    /// is the universal parameter.
    fn thinking_level_to_effort(thinking: ThinkingLevel) -> &'static str {
        match thinking {
            ThinkingLevel::Off => unreachable!(),
            ThinkingLevel::Low => "low",
            ThinkingLevel::Medium => "medium",
            ThinkingLevel::High | ThinkingLevel::Max => "high",
        }
    }

    /// Build request body with thinking/reasoning support.
    fn build_request_body_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
        thinking: ThinkingLevel,
    ) -> Value {
        let mut body = self.build_request_body_with_tools(messages, tools, stream);

        if thinking != ThinkingLevel::Off && self.supports_reasoning() {
            body["reasoning_effort"] = json!(Self::thinking_level_to_effort(thinking));
        }

        body
    }

    /// Parse reasoning content from a non-streaming response payload.
    fn parse_reasoning(payload: &Value) -> Option<String> {
        payload
            .get("choices")
            .and_then(Value::as_array)
            .and_then(|choices| choices.first())
            .and_then(|choice| choice.get("message"))
            .and_then(|message| {
                message
                    .get("reasoning_content")
                    .or_else(|| message.get("reasoning"))
            })
            .and_then(Value::as_str)
            .map(String::from)
    }
}

#[async_trait]
impl LLMProvider for CopilotProvider {
    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let token = self.ensure_token().await?;
        let initiator = Self::infer_initiator(messages);
        let headers = Self::build_headers(&token.token, initiator);
        let url = format!("{}/chat/completions", token.api_endpoint);

        let client = self.pool.get_client(&token.api_endpoint).await?;
        let request = client
            .post(&url)
            .headers(headers)
            .json(&self.build_request_body(messages, false));

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Copilot").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        common::parse_openai_completion_payload(&payload)
    }

    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let token = self.ensure_token().await?;
        let initiator = Self::infer_initiator(messages);
        let headers = Self::build_headers(&token.token, initiator);
        let url = format!("{}/chat/completions", token.api_endpoint);

        let client = self.pool.get_client(&token.api_endpoint).await?;
        let request = client
            .post(&url)
            .headers(headers)
            .json(&self.build_request_body(messages, true));

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Copilot").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    common::parse_sse_lines(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| common::parse_openai_stream_chunk(&payload))
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(chunks)
        });

        Ok(Box::pin(stream))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        common::estimate_tokens(input)
    }

    /// Copilot is subscription-billed — all models are $0 per token.
    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }

    fn supports_tools(&self) -> bool {
        true
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model))]
    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let token = self.ensure_token().await?;
        let initiator = Self::infer_initiator(messages);
        let headers = Self::build_headers(&token.token, initiator);
        let url = format!("{}/chat/completions", token.api_endpoint);

        let mut body = self.build_request_body_with_tools(messages, tools, true);
        body["stream_options"] = serde_json::json!({"include_usage": true});

        let client = self.pool.get_client(&token.api_endpoint).await?;
        let request = client.post(&url).headers(headers).json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Copilot").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    common::parse_sse_lines(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<serde_json::Value>(&line).ok())
                        .filter_map(|payload| common::parse_openai_stream_chunk(&payload))
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(chunks)
        });

        Ok(Box::pin(stream))
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model))]
    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<LLMResponse> {
        let token = self.ensure_token().await?;
        let initiator = Self::infer_initiator(messages);
        let headers = Self::build_headers(&token.token, initiator);
        let url = format!("{}/chat/completions", token.api_endpoint);

        let body = self.build_request_body_with_tools(messages, tools, false);
        tracing::debug!(
            message_count = messages.len(),
            tool_count = tools.len(),
            "Sending Copilot generate_with_tools request"
        );

        let client = self.pool.get_client(&token.api_endpoint).await?;
        let request = client.post(&url).headers(headers).json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Copilot").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let content = common::parse_openai_completion_payload(&payload).unwrap_or_default();
        let tool_calls = common::parse_openai_tool_calls(&payload);
        let usage = common::parse_usage(&payload);

        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
            thinking: None,
        })
    }

    fn supports_thinking(&self) -> bool {
        self.supports_reasoning()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        if !self.supports_reasoning() {
            &[]
        } else {
            // Copilot proxy caps at "high" for all backends
            &[
                ThinkingLevel::Low,
                ThinkingLevel::Medium,
                ThinkingLevel::High,
            ]
        }
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        if !self.supports_reasoning() || thinking == ThinkingLevel::Off {
            return self.generate_with_tools(messages, tools).await;
        }

        let token = self.ensure_token().await?;
        let initiator = Self::infer_initiator(messages);
        let headers = Self::build_headers(&token.token, initiator);
        let url = format!("{}/chat/completions", token.api_endpoint);

        let body = self.build_request_body_with_thinking(messages, tools, false, thinking);
        tracing::debug!(
            message_count = messages.len(),
            tool_count = tools.len(),
            ?thinking,
            "Sending Copilot generate_with_thinking request"
        );

        let client = self.pool.get_client(&token.api_endpoint).await?;
        let request = client.post(&url).headers(headers).json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Copilot").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let content = common::parse_openai_completion_payload(&payload).unwrap_or_default();
        let tool_calls = common::parse_openai_tool_calls(&payload);
        let usage = common::parse_usage(&payload);
        let thinking_content = Self::parse_reasoning(&payload);

        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
            thinking: thinking_content,
        })
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        if !self.supports_reasoning() || thinking == ThinkingLevel::Off {
            return self.generate_stream_with_tools(messages, tools).await;
        }

        let token = self.ensure_token().await?;
        let initiator = Self::infer_initiator(messages);
        let headers = Self::build_headers(&token.token, initiator);
        let url = format!("{}/chat/completions", token.api_endpoint);

        let mut body = self.build_request_body_with_thinking(messages, tools, true, thinking);
        body["stream_options"] = json!({"include_usage": true});

        let client = self.pool.get_client(&token.api_endpoint).await?;
        let request = client.post(&url).headers(headers).json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Copilot").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    common::parse_sse_lines(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<serde_json::Value>(&line).ok())
                        .filter_map(|payload| common::parse_openai_stream_chunk(&payload))
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(chunks)
        });

        Ok(Box::pin(stream))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn infer_initiator_user_message() {
        let messages = vec![Message::new(Role::User, "Hello")];
        assert_eq!(CopilotProvider::infer_initiator(&messages), "user");
    }

    #[test]
    fn infer_initiator_assistant_message() {
        let messages = vec![
            Message::new(Role::User, "Hello"),
            Message::new(Role::Assistant, "Hi"),
        ];
        assert_eq!(CopilotProvider::infer_initiator(&messages), "agent");
    }

    #[test]
    fn infer_initiator_tool_message() {
        let messages = vec![Message::new(Role::Tool, "result")];
        assert_eq!(CopilotProvider::infer_initiator(&messages), "agent");
    }

    #[test]
    fn infer_initiator_empty_messages() {
        let messages: Vec<Message> = vec![];
        assert_eq!(CopilotProvider::infer_initiator(&messages), "agent");
    }

    #[test]
    fn build_headers_contains_required_fields() {
        let headers = CopilotProvider::build_headers("test-token", "user");

        assert!(headers.get("Authorization").unwrap().to_str().unwrap().contains("Bearer test-token"));
        assert_eq!(headers.get("X-Initiator").unwrap(), "user");
        assert_eq!(headers.get("Openai-Intent").unwrap(), "conversation-edits");
        assert_eq!(headers.get("User-Agent").unwrap(), "GitHubCopilotChat/0.35.0");
        assert_eq!(headers.get("Editor-Version").unwrap(), "vscode/1.107.0");
        assert_eq!(headers.get("Editor-Plugin-Version").unwrap(), "copilot-chat/0.35.0");
        assert_eq!(headers.get("Copilot-Integration-Id").unwrap(), "vscode-chat");
    }

    #[test]
    fn build_headers_agent_initiator() {
        let headers = CopilotProvider::build_headers("tok", "agent");
        assert_eq!(headers.get("X-Initiator").unwrap(), "agent");
    }

    #[test]
    fn cost_is_always_zero() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "gpt-4o");
        assert_eq!(provider.estimate_cost(1_000_000, 1_000_000), 0.0);
    }

    #[test]
    fn supports_tools_is_true() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "claude-sonnet-4");
        assert!(provider.supports_tools());
    }

    #[test]
    fn model_name_returns_configured_model() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "gpt-5");
        assert_eq!(provider.model_name(), "gpt-5");
    }

    #[test]
    fn supports_thinking_claude_models() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool.clone(), "token", "claude-sonnet-4");
        assert!(provider.supports_thinking());

        let provider = CopilotProvider::new(pool.clone(), "token", "claude-3.5-sonnet");
        assert!(provider.supports_thinking());

        let provider = CopilotProvider::new(pool, "token", "claude-haiku-4.5");
        assert!(provider.supports_thinking());
    }

    #[test]
    fn supports_thinking_openai_reasoning_models() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool.clone(), "token", "o3");
        assert!(provider.supports_thinking());

        let provider = CopilotProvider::new(pool.clone(), "token", "o4-mini");
        assert!(provider.supports_thinking());

        let provider = CopilotProvider::new(pool, "token", "gpt-5");
        assert!(provider.supports_thinking());
    }

    #[test]
    fn no_thinking_for_unsupported_models() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool.clone(), "token", "gpt-4o");
        assert!(!provider.supports_thinking());

        let provider = CopilotProvider::new(pool, "token", "gemini-2.5-pro");
        assert!(!provider.supports_thinking());
    }

    #[test]
    fn thinking_levels_for_supported_models() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool.clone(), "token", "claude-sonnet-4");
        let levels = provider.thinking_levels();
        assert_eq!(levels.len(), 3);
        assert_eq!(levels[0], ThinkingLevel::Low);
        assert_eq!(levels[1], ThinkingLevel::Medium);
        assert_eq!(levels[2], ThinkingLevel::High);
    }

    #[test]
    fn thinking_levels_empty_for_unsupported() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "gpt-4o");
        assert!(provider.thinking_levels().is_empty());
    }

    #[test]
    fn is_claude_model_detection() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool.clone(), "token", "claude-sonnet-4");
        assert!(provider.is_claude_model());

        let provider = CopilotProvider::new(pool, "token", "gpt-5");
        assert!(!provider.is_claude_model());
    }

    #[test]
    fn is_openai_reasoning_model_detection() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool.clone(), "token", "o3");
        assert!(provider.is_openai_reasoning_model());

        let provider = CopilotProvider::new(pool.clone(), "token", "o4-mini");
        assert!(provider.is_openai_reasoning_model());

        let provider = CopilotProvider::new(pool.clone(), "token", "gpt-5");
        assert!(provider.is_openai_reasoning_model());

        let provider = CopilotProvider::new(pool, "token", "gpt-4o");
        assert!(!provider.is_openai_reasoning_model());
    }

    #[test]
    fn build_request_body_with_thinking_includes_reasoning_effort() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "claude-sonnet-4");

        let messages = vec![Message::new(Role::User, "Hello")];
        let body = provider.build_request_body_with_thinking(&messages, &[], false, ThinkingLevel::Medium);

        assert_eq!(body["reasoning_effort"], "medium");
    }

    #[test]
    fn build_request_body_with_thinking_off_no_reasoning_effort() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "claude-sonnet-4");

        let messages = vec![Message::new(Role::User, "Hello")];
        let body = provider.build_request_body_with_thinking(&messages, &[], false, ThinkingLevel::Off);

        assert!(body.get("reasoning_effort").is_none());
    }

    #[test]
    fn build_request_body_with_thinking_max_maps_to_high() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "o3");

        let messages = vec![Message::new(Role::User, "Hello")];
        let body = provider.build_request_body_with_thinking(&messages, &[], false, ThinkingLevel::Max);

        assert_eq!(body["reasoning_effort"], "high");
    }

    #[test]
    fn build_request_body_with_thinking_unsupported_model_no_effort() {
        let pool = Arc::new(ConnectionPool::new());
        let provider = CopilotProvider::new(pool, "token", "gpt-4o");

        let messages = vec![Message::new(Role::User, "Hello")];
        let body = provider.build_request_body_with_thinking(&messages, &[], false, ThinkingLevel::High);

        assert!(body.get("reasoning_effort").is_none());
    }

    #[test]
    fn parse_reasoning_from_response() {
        let payload = json!({
            "choices": [{
                "message": {
                    "content": "Hello!",
                    "reasoning_content": "Let me think about this..."
                }
            }]
        });
        assert_eq!(
            CopilotProvider::parse_reasoning(&payload),
            Some("Let me think about this...".to_string())
        );
    }

    #[test]
    fn parse_reasoning_absent() {
        let payload = json!({
            "choices": [{
                "message": {
                    "content": "Hello!"
                }
            }]
        });
        assert_eq!(CopilotProvider::parse_reasoning(&payload), None);
    }
}
