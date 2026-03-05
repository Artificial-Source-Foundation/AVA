use std::pin::Pin;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use crate::provider::LLMProvider;
use crate::providers::common;

#[derive(Clone)]
pub struct GeminiProvider {
    client: reqwest::Client,
    api_key: String,
    model: String,
}

impl GeminiProvider {
    pub fn new(api_key: impl Into<String>, model: impl Into<String>) -> Self {
        Self {
            client: reqwest::Client::new(),
            api_key: api_key.into(),
            model: model.into(),
        }
    }

    fn generate_url(&self) -> String {
        format!(
            "https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent",
            self.model
        )
    }

    fn stream_url(&self) -> String {
        format!(
            "https://generativelanguage.googleapis.com/v1beta/models/{}:streamGenerateContent?alt=sse",
            self.model
        )
    }

    fn build_request_body(&self, messages: &[Message]) -> Value {
        let (system_instruction, contents) = common::map_messages_gemini_parts(messages);
        let mut body = json!({"contents": contents});
        if let Some(system_instruction) = system_instruction {
            body["system_instruction"] = system_instruction;
        }
        body
    }
}

#[async_trait]
impl LLMProvider for GeminiProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let response = self
            .client
            .post(self.generate_url())
            .header("x-goog-api-key", &self.api_key)
            .json(&self.build_request_body(messages))
            .send()
            .await
            .map_err(common::reqwest_error)?;

        let response = common::validate_status(response, "Gemini").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        common::parse_gemini_completion_payload(&payload)
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        let response = self
            .client
            .post(self.stream_url())
            .header("x-goog-api-key", &self.api_key)
            .json(&self.build_request_body(messages))
            .send()
            .await
            .map_err(common::reqwest_error)?;

        let response = common::validate_status(response, "Gemini").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let content = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    common::parse_sse_lines(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| common::parse_gemini_completion_payload(&payload).ok())
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
}
