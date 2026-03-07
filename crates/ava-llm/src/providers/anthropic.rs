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

const ANTHROPIC_BASE_URL: &str = "https://api.anthropic.com";

#[derive(Clone)]
pub struct AnthropicProvider {
    pool: Arc<ConnectionPool>,
    api_key: String,
    model: String,
    max_tokens: usize,
}

impl AnthropicProvider {
    pub fn new(pool: Arc<ConnectionPool>, api_key: impl Into<String>, model: impl Into<String>) -> Self {
        Self {
            pool,
            api_key: api_key.into(),
            model: model.into(),
            max_tokens: 4096,
        }
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
            body["system"] = json!(system_message);
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

    async fn client(&self) -> Arc<reqwest::Client> {
        self.pool.get_client(ANTHROPIC_BASE_URL).await
    }
}

#[async_trait]
impl LLMProvider for AnthropicProvider {
    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let client = self.client().await;
        let request = client
            .post(format!("{ANTHROPIC_BASE_URL}/v1/messages"))
            .header("x-api-key", &self.api_key)
            .header("anthropic-version", "2023-06-01")
            .json(&self.build_request_body(messages, false));

        let response = common::send_retrying(request, "Anthropic").await?;
        let response = common::validate_status(response, "Anthropic").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        common::parse_anthropic_completion_payload(&payload)
    }

    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        let client = self.client().await;
        let request = client
            .post(format!("{ANTHROPIC_BASE_URL}/v1/messages"))
            .header("x-api-key", &self.api_key)
            .header("anthropic-version", "2023-06-01")
            .json(&self.build_request_body(messages, true));

        let response = common::send_retrying(request, "Anthropic").await?;
        let response = common::validate_status(response, "Anthropic").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let content = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    common::parse_sse_lines(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| common::parse_anthropic_delta_payload(&payload))
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
        let client = self.client().await;
        let request = client
            .post(format!("{ANTHROPIC_BASE_URL}/v1/messages"))
            .header("x-api-key", &self.api_key)
            .header("anthropic-version", "2023-06-01")
            .json(&body);

        let response = common::send_retrying(request, "Anthropic").await?;
        let response = common::validate_status(response, "Anthropic").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        let content = common::parse_anthropic_completion_payload(&payload).unwrap_or_default();
        let tool_calls = common::parse_anthropic_tool_calls(&payload);

        Ok(LLMResponse {
            content,
            tool_calls,
        })
    }
}
