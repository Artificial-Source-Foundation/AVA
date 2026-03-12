use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::instrument;

use crate::circuit_breaker::CircuitBreaker;
use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse};
use crate::providers::common;
use crate::providers::openai::OpenAIProvider;

#[derive(Clone)]
pub struct OpenRouterProvider {
    inner: OpenAIProvider,
    pool: Arc<ConnectionPool>,
    api_key: String,
    model: String,
    base_url: String,
    circuit_breaker: Option<Arc<CircuitBreaker>>,
}

impl OpenRouterProvider {
    pub fn new(pool: Arc<ConnectionPool>, api_key: impl Into<String>, model: impl Into<String>) -> Self {
        Self::with_base_url(pool, api_key, model, "https://openrouter.ai/api")
    }

    pub fn with_base_url(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
        base_url: impl Into<String>,
    ) -> Self {
        let api_key = api_key.into();
        let model = model.into();
        let base_url = base_url.into();
        Self {
            inner: OpenAIProvider::with_base_url(
                pool.clone(),
                api_key.clone(),
                model.clone(),
                base_url.clone(),
            ),
            pool,
            api_key,
            model,
            base_url,
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    /// Check if the underlying model supports reasoning via OpenRouter.
    /// OpenRouter supports reasoning.effort for GPT-5, Claude, Gemini-3 models.
    fn supports_reasoning(&self) -> bool {
        let m = self.model.to_lowercase();
        m.contains("gpt-5")
            || m.contains("codex")
            || m.contains("claude")
            || m.contains("gemini-3")
            || m.starts_with("o3")
            || m.starts_with("o4")
    }

    /// Build request body with OpenRouter's reasoning format.
    fn build_request_body_with_reasoning(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
        thinking: ThinkingLevel,
    ) -> Value {
        let mut body = json!({
            "model": self.model,
            "messages": common::map_messages_openai(messages),
            "stream": stream,
        });

        if !tools.is_empty() {
            body["tools"] = json!(common::tools_to_openai_format(tools));
        }

        if thinking != ThinkingLevel::Off && self.supports_reasoning() {
            // OpenRouter uses { reasoning: { effort: "low"|"medium"|"high" } }
            let effort = match thinking {
                ThinkingLevel::Off => unreachable!(),
                ThinkingLevel::Low => "low",
                ThinkingLevel::Medium => "medium",
                ThinkingLevel::High | ThinkingLevel::Max => "high",
            };
            body["reasoning"] = json!({ "effort": effort });
        }

        body
    }

    async fn client(&self) -> Result<Arc<reqwest::Client>> {
        self.pool.get_client(&self.base_url).await
    }

    async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
        common::send_with_retry_cb(request, "OpenRouter", 3, self.circuit_breaker.as_deref()).await
    }
}

#[async_trait]
impl LLMProvider for OpenRouterProvider {
    #[instrument(skip(self, messages), fields(model = %self.inner.model_name()))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        self.inner.generate(messages).await
    }

    #[instrument(skip(self, messages), fields(model = %self.inner.model_name()))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.inner.generate_stream(messages).await
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        self.inner.estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        self.inner.estimate_cost(input_tokens, output_tokens) * 1.10
    }

    fn model_name(&self) -> &str {
        self.inner.model_name()
    }

    fn provider_kind(&self) -> crate::message_transform::ProviderKind {
        crate::message_transform::ProviderKind::OpenRouter
    }

    fn supports_tools(&self) -> bool {
        true
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.inner.model_name()))]
    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<LLMResponse> {
        self.inner.generate_with_tools(messages, tools).await
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.inner.model_name()))]
    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.inner.generate_stream_with_tools(messages, tools).await
    }

    fn supports_thinking(&self) -> bool {
        self.supports_reasoning()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        if self.supports_reasoning() {
            // OpenRouter supports low/medium/high for all reasoning models
            &[
                ThinkingLevel::Low,
                ThinkingLevel::Medium,
                ThinkingLevel::High,
            ]
        } else {
            &[]
        }
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.inner.model_name(), thinking = ?thinking))]
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        if !self.supports_reasoning() || thinking == ThinkingLevel::Off {
            return self.inner.generate_with_tools(messages, tools).await;
        }

        // Use OpenRouter-specific reasoning format
        let body = self.build_request_body_with_reasoning(messages, tools, false, thinking);
        let client = self.client().await?;
        let request = client
            .post(format!("{}/v1/chat/completions", self.base_url))
            .bearer_auth(&self.api_key)
            .json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "OpenRouter").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        let content = OpenAIProvider::parse_response_payload(&payload).unwrap_or_default();
        let tool_calls = common::parse_openai_tool_calls(&payload);
        let usage = common::parse_usage(&payload);

        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
            thinking: None, // OpenRouter doesn't return thinking content in a standard field
        })
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.inner.model_name(), thinking = ?thinking))]
    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        if !self.supports_reasoning() || thinking == ThinkingLevel::Off {
            return self.inner.generate_stream_with_tools(messages, tools).await;
        }

        // Use OpenRouter-specific reasoning format with streaming
        let mut body = self.build_request_body_with_reasoning(messages, tools, true, thinking);
        body["stream_options"] = json!({"include_usage": true});
        let client = self.client().await?;
        let request = client
            .post(format!("{}/v1/chat/completions", self.base_url))
            .bearer_auth(&self.api_key)
            .json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "OpenRouter").await?;
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
}
