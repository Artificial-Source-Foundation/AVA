use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::instrument;

use crate::circuit_breaker::CircuitBreaker;
use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse, ProviderCapabilities};
use crate::providers::common;

/// Azure OpenAI provider.
///
/// Uses the same OpenAI API format but with Azure-specific URLs and auth:
/// - Base URL: `https://{resource}.openai.azure.com/openai/deployments/{deployment}`
/// - Auth: `api-key` header (not `Authorization: Bearer`)
/// - API version: `?api-version=2024-10-21` query parameter
///
/// # Credentials
///
/// ```json
/// {
///   "providers": {
///     "azure": {
///       "api_key": "your-azure-api-key",
///       "base_url": "https://my-resource.openai.azure.com",
///       "org_id": "my-deployment-name"
///     }
///   }
/// }
/// ```
///
/// - `api_key`: Azure API key
/// - `base_url`: Azure OpenAI resource URL (e.g., `https://my-resource.openai.azure.com`)
/// - `org_id`: Deployment name (defaults to the model name if not set)
#[derive(Clone)]
pub struct AzureOpenAIProvider {
    pool: Arc<ConnectionPool>,
    api_key: String,
    model: String,
    /// Azure resource base URL (e.g., `https://my-resource.openai.azure.com`).
    base_url: String,
    /// Azure deployment name (e.g., `gpt-4`, `gpt-4o`).
    deployment: String,
    /// Azure API version query parameter.
    api_version: String,
    circuit_breaker: Option<Arc<CircuitBreaker>>,
}

/// Default Azure OpenAI API version.
const DEFAULT_API_VERSION: &str = "2024-10-21";

impl AzureOpenAIProvider {
    pub fn new(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
        base_url: impl Into<String>,
        deployment: impl Into<String>,
    ) -> Self {
        Self {
            pool,
            api_key: api_key.into(),
            model: model.into(),
            base_url: base_url.into().trim_end_matches('/').to_string(),
            deployment: deployment.into(),
            api_version: DEFAULT_API_VERSION.to_string(),
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    /// Set a custom API version (default: `2024-10-21`).
    pub fn with_api_version(mut self, version: impl Into<String>) -> Self {
        self.api_version = version.into();
        self
    }

    /// The Azure completions endpoint URL.
    ///
    /// Format: `{base_url}/openai/deployments/{deployment}/chat/completions?api-version={version}`
    fn completions_url(&self) -> String {
        format!(
            "{}/openai/deployments/{}/chat/completions?api-version={}",
            self.base_url, self.deployment, self.api_version
        )
    }

    /// Build an OpenAI-format request body.
    fn build_request_body(&self, messages: &[Message], stream: bool) -> Value {
        json!({
            "messages": common::map_messages_openai(messages),
            "stream": stream,
        })
    }

    /// Build a request body with tool definitions.
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

    /// Add Azure-specific auth headers (`api-key` instead of `Authorization: Bearer`).
    fn auth_request(&self, request: reqwest::RequestBuilder) -> reqwest::RequestBuilder {
        request.header("api-key", &self.api_key)
    }

    async fn client(&self) -> Result<Arc<reqwest::Client>> {
        self.pool.get_client(&self.base_url).await
    }

    async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
        common::send_with_retry_cb(
            request,
            "Azure OpenAI",
            common::DEFAULT_MAX_RETRIES,
            self.circuit_breaker.as_deref(),
        )
        .await
    }
}

#[async_trait]
impl LLMProvider for AzureOpenAIProvider {
    #[instrument(skip(self, messages), fields(model = %self.model, deployment = %self.deployment))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let stream = self.generate_stream(messages).await?;
        let mut content = String::new();
        futures::pin_mut!(stream);
        while let Some(chunk) = stream.next().await {
            if let Some(text) = chunk.content {
                content.push_str(&text);
            }
        }
        if content.is_empty() {
            Err(AvaError::ProviderError {
                provider: "azure".to_string(),
                message: "empty response from streaming collect".to_string(),
            })
        } else {
            Ok(content)
        }
    }

    #[instrument(skip(self, messages), fields(model = %self.model, deployment = %self.deployment))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let client = self.client().await?;
        let body = self.build_request_body(messages, true);
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Azure OpenAI").await?;
        let mut sse_parser = common::SseParser::new();
        let stream = response.bytes_stream().flat_map(move |chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    sse_parser
                        .feed(&text)
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
        common::estimate_tokens_for_model(input, &self.model)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        let (in_rate, out_rate) = common::model_pricing_usd_per_million(&self.model);
        common::estimate_cost_usd(input_tokens, output_tokens, in_rate, out_rate)
    }

    fn model_name(&self) -> &str {
        &self.model
    }

    fn capabilities(&self) -> ProviderCapabilities {
        ProviderCapabilities {
            supports_streaming: true,
            supports_tool_use: true,
            supports_thinking: false,
            supports_thinking_levels: false,
            supports_images: true,
            max_context_window: 128_000,
            supports_prompt_caching: false,
            is_subscription: false,
        }
    }

    fn provider_kind(&self) -> crate::message_transform::ProviderKind {
        crate::message_transform::ProviderKind::AzureOpenAI
    }

    fn supports_tools(&self) -> bool {
        true
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, deployment = %self.deployment))]
    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<LLMResponse> {
        let stream = self.generate_stream_with_tools(messages, tools).await?;
        let mut content = String::new();
        let mut tool_calls = Vec::new();
        let mut usage = None;
        futures::pin_mut!(stream);
        while let Some(chunk) = stream.next().await {
            if let Some(text) = chunk.content {
                content.push_str(&text);
            }
            if let Some(tc) = chunk.tool_call {
                while tool_calls.len() <= tc.index {
                    tool_calls.push(ava_types::ToolCall {
                        id: String::new(),
                        name: String::new(),
                        arguments: serde_json::Value::Null,
                    });
                }
                if let Some(id) = tc.id {
                    tool_calls[tc.index].id = id;
                }
                if let Some(name) = tc.name {
                    tool_calls[tc.index].name = name;
                }
                if let Some(args_delta) = tc.arguments_delta {
                    let existing = &mut tool_calls[tc.index].arguments;
                    if existing.is_null() {
                        *existing = serde_json::Value::String(args_delta);
                    } else if let Some(s) = existing.as_str().map(String::from) {
                        *existing = serde_json::Value::String(s + &args_delta);
                    }
                }
            }
            if let Some(u) = chunk.usage {
                usage = Some(u);
            }
        }
        // Parse accumulated JSON argument strings
        for tc in &mut tool_calls {
            if let Some(s) = tc.arguments.as_str().map(String::from) {
                tc.arguments = serde_json::from_str(&s).unwrap_or(json!({}));
            }
        }
        tool_calls.retain(|tc| !tc.name.is_empty());
        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
            thinking: None,
        })
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, deployment = %self.deployment))]
    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let client = self.client().await?;
        let mut body = self.build_request_body_with_tools(messages, tools, true);
        body["stream_options"] = json!({"include_usage": true});
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Azure OpenAI").await?;
        let mut sse_parser = common::SseParser::new();
        let stream = response.bytes_stream().flat_map(move |chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    sse_parser
                        .feed(&text)
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

    fn supports_thinking(&self) -> bool {
        false
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        &[]
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        let _ = thinking;
        self.generate_with_tools(messages, tools).await
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let _ = thinking;
        self.generate_stream_with_tools(messages, tools).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pool::ConnectionPool;

    fn pool() -> Arc<ConnectionPool> {
        Arc::new(ConnectionPool::new())
    }

    #[test]
    fn completions_url_format() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "gpt-4o",
        );
        assert_eq!(
            provider.completions_url(),
            "https://my-resource.openai.azure.com/openai/deployments/gpt-4o/chat/completions?api-version=2024-10-21"
        );
    }

    #[test]
    fn completions_url_trims_trailing_slash() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com/",
            "my-deploy",
        );
        assert!(provider
            .completions_url()
            .starts_with("https://my-resource.openai.azure.com/openai/deployments/my-deploy/"));
    }

    #[test]
    fn custom_api_version() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "gpt-4o",
        )
        .with_api_version("2025-01-01");
        assert!(provider
            .completions_url()
            .contains("api-version=2025-01-01"));
    }

    #[test]
    fn model_name_returns_model() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "my-deploy",
        );
        assert_eq!(provider.model_name(), "gpt-4o");
    }

    #[test]
    fn supports_tools_returns_true() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "gpt-4o",
        );
        assert!(provider.supports_tools());
    }

    #[test]
    fn does_not_support_thinking() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "gpt-4o",
        );
        assert!(!provider.supports_thinking());
        assert!(provider.thinking_levels().is_empty());
    }

    #[test]
    fn build_request_body_uses_openai_format() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "gpt-4o",
        );
        let messages = vec![Message::new(ava_types::Role::User, "hello")];
        let body = provider.build_request_body(&messages, false);

        // Azure uses OpenAI message format but no model field in body
        // (model is in the URL via deployment name)
        assert!(body.get("messages").is_some());
        assert!(body.get("model").is_none());
        assert_eq!(body["stream"], json!(false));
    }

    #[test]
    fn build_request_body_with_tools_includes_tools() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "gpt-4o",
        );
        let messages = vec![Message::new(ava_types::Role::User, "hello")];
        let tools = vec![ava_types::Tool {
            name: "read".to_string(),
            description: "Read a file".to_string(),
            parameters: json!({"type": "object", "properties": {"path": {"type": "string"}}}),
        }];
        let body = provider.build_request_body_with_tools(&messages, &tools, false);

        assert!(body.get("tools").is_some());
        let tool_defs = body["tools"].as_array().unwrap();
        assert_eq!(tool_defs.len(), 1);
    }

    #[test]
    fn provider_kind_is_azure() {
        let provider = AzureOpenAIProvider::new(
            pool(),
            "test-key",
            "gpt-4o",
            "https://my-resource.openai.azure.com",
            "gpt-4o",
        );
        assert_eq!(
            provider.provider_kind(),
            crate::message_transform::ProviderKind::AzureOpenAI
        );
    }
}
