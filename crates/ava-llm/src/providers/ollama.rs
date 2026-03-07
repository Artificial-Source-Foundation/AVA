use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::instrument;

use crate::pool::ConnectionPool;
use crate::provider::LLMProvider;
use crate::providers::common;

#[derive(Clone)]
pub struct OllamaProvider {
    pool: Arc<ConnectionPool>,
    model: String,
    base_url: String,
}

impl OllamaProvider {
    pub fn new(pool: Arc<ConnectionPool>, base_url: impl Into<String>, model: impl Into<String>) -> Self {
        Self {
            pool,
            model: model.into(),
            base_url: base_url.into(),
        }
    }

    fn build_request_body(&self, messages: &[Message], stream: bool) -> Value {
        json!({
            "model": self.model,
            "messages": common::map_messages_openai(messages),
            "stream": stream,
        })
    }

    async fn client(&self) -> Arc<reqwest::Client> {
        self.pool.get_client(&self.base_url).await
    }
}

#[async_trait]
impl LLMProvider for OllamaProvider {
    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let client = self.client().await;
        let request = client
            .post(format!("{}/api/chat", self.base_url.trim_end_matches('/')))
            .json(&self.build_request_body(messages, false));

        let response = common::send_retrying(request, "Ollama").await?;
        let response = common::validate_status(response, "Ollama").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        common::parse_ollama_completion_payload(&payload)
    }

    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        let client = self.client().await;
        let request = client
            .post(format!("{}/api/chat", self.base_url.trim_end_matches('/')))
            .json(&self.build_request_body(messages, true));

        let response = common::send_retrying(request, "Ollama").await?;
        let response = common::validate_status(response, "Ollama").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let content = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    text.lines()
                        .filter_map(|line| serde_json::from_str::<Value>(line).ok())
                        .filter_map(|payload| {
                            payload
                                .get("message")
                                .and_then(|message| message.get("content"))
                                .and_then(Value::as_str)
                                .map(ToString::to_string)
                        })
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

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}
