use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::instrument;

use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse};
use crate::providers::common;

#[derive(Clone)]
pub struct OpenAIProvider {
    pool: Arc<ConnectionPool>,
    api_key: String,
    model: String,
    base_url: String,
}

impl OpenAIProvider {
    pub fn new(pool: Arc<ConnectionPool>, api_key: impl Into<String>, model: impl Into<String>) -> Self {
        Self::with_base_url(pool, api_key, model, "https://api.openai.com")
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
            base_url: base_url.into(),
        }
    }

    pub fn build_request_body(&self, messages: &[Message], stream: bool) -> Value {
        json!({
            "model": self.model,
            "messages": common::map_messages_openai(messages),
            "stream": stream,
        })
    }

    pub fn build_request_body_with_tools(
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

    pub fn parse_response_payload(payload: &Value) -> Result<String> {
        common::parse_openai_completion_payload(payload)
    }

    async fn client(&self) -> Arc<reqwest::Client> {
        self.pool.get_client(&self.base_url).await
    }
}

#[async_trait]
impl LLMProvider for OpenAIProvider {
    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let client = self.client().await;
        let request = client
            .post(format!("{}/v1/chat/completions", self.base_url))
            .bearer_auth(&self.api_key)
            .json(&self.build_request_body(messages, false));

        let response = common::send_retrying(request, "OpenAI").await?;
        let response = common::validate_status(response, "OpenAI").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        Self::parse_response_payload(&payload)
    }

    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        let client = self.client().await;
        let request = client
            .post(format!("{}/v1/chat/completions", self.base_url))
            .bearer_auth(&self.api_key)
            .json(&self.build_request_body(messages, true));

        let response = common::send_retrying(request, "OpenAI").await?;
        let response = common::validate_status(response, "OpenAI").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let content = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    common::parse_sse_lines(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| common::parse_openai_delta_payload(&payload))
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(content)
        });

        Ok(Box::pin(stream))
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
        tracing::debug!(
            message_count = messages.len(),
            tool_count = tools.len(),
            "Sending generate_with_tools request"
        );
        let client = self.client().await;
        let request = client
            .post(format!("{}/v1/chat/completions", self.base_url))
            .bearer_auth(&self.api_key)
            .json(&body);

        let response = common::send_retrying(request, "OpenAI").await?;
        let response = common::validate_status(response, "OpenAI").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        let content = Self::parse_response_payload(&payload).unwrap_or_default();
        let tool_calls = common::parse_openai_tool_calls(&payload);
        let usage = common::parse_usage(&payload);

        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
        })
    }
}
