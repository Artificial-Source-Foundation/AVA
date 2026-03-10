use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::instrument;

use crate::circuit_breaker::CircuitBreaker;
use crate::pool::ConnectionPool;
use crate::provider::LLMProvider;
use crate::providers::common;

const GEMINI_BASE_URL: &str = "https://generativelanguage.googleapis.com";

#[derive(Clone)]
pub struct GeminiProvider {
    pool: Arc<ConnectionPool>,
    api_key: String,
    model: String,
    circuit_breaker: Option<Arc<CircuitBreaker>>,
}

impl GeminiProvider {
    pub fn new(pool: Arc<ConnectionPool>, api_key: impl Into<String>, model: impl Into<String>) -> Self {
        Self {
            pool,
            api_key: api_key.into(),
            model: model.into(),
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    /// Check if the current model supports thinking.
    /// Supports Gemini 2.5 and Gemini 3.x models.
    fn supports_thinking_mode(&self) -> bool {
        let model_lower = self.model.to_lowercase();
        model_lower.contains("gemini-2.5") || model_lower.contains("gemini-3")
    }

    /// Check if this is a Gemini 3.x model (uses thinkingLevel instead of thinkingBudget).
    fn is_gemini3(&self) -> bool {
        self.model.to_lowercase().contains("gemini-3")
    }

    fn generate_url(&self) -> String {
        format!(
            "{GEMINI_BASE_URL}/v1beta/models/{}:generateContent",
            self.model
        )
    }

    fn stream_url(&self) -> String {
        format!(
            "{GEMINI_BASE_URL}/v1beta/models/{}:streamGenerateContent?alt=sse",
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

    /// Build request body with thinking support.
    fn build_request_body_with_thinking(
        &self,
        messages: &[Message],
        thinking: ThinkingLevel,
    ) -> Value {
        let mut body = self.build_request_body(messages);

        if thinking != ThinkingLevel::Off && self.supports_thinking_mode() {
            if self.is_gemini3() {
                // Gemini 3.x uses thinkingLevel: "low" | "high"
                // Gemini 3.1+ also supports "medium"
                let level = match thinking {
                    ThinkingLevel::Off => unreachable!(),
                    ThinkingLevel::Low | ThinkingLevel::Medium => "low",
                    ThinkingLevel::High | ThinkingLevel::Max => "high",
                };

                body["thinkingConfig"] = json!({
                    "includeThoughts": true,
                    "thinkingLevel": level,
                });
            } else {
                // Gemini 2.5 uses thinkingBudget
                // Low=4000, Medium=8000, High=16000, Max=24576 (Gemini 2.5 max)
                let budget = match thinking {
                    ThinkingLevel::Off => unreachable!(),
                    ThinkingLevel::Low => 4000,
                    ThinkingLevel::Medium => 8000,
                    ThinkingLevel::High => 16000,
                    ThinkingLevel::Max => 24576,
                };

                body["thinkingConfig"] = json!({
                    "includeThoughts": true,
                    "thinkingBudget": budget,
                });
            }
        }

        body
    }

    /// Parse thinking content from Gemini response.
    /// Gemini returns thoughts in parts with thought: true flag.
    fn parse_thinking(&self, payload: &Value) -> Option<String> {
        let candidates = payload.get("candidates")?.as_array()?;
        let content = candidates.first()?.get("content")?;
        let parts = content.get("parts")?.as_array()?;

        let thoughts: Vec<String> = parts
            .iter()
            .filter(|part| part.get("thought").and_then(Value::as_bool) == Some(true))
            .filter_map(|part| part.get("text").and_then(Value::as_str))
            .map(String::from)
            .collect();

        if thoughts.is_empty() {
            None
        } else {
            Some(thoughts.join("\n"))
        }
    }

    async fn client(&self) -> Result<Arc<reqwest::Client>> {
        self.pool.get_client(GEMINI_BASE_URL).await
    }

    async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
        common::send_with_retry_cb(request, "Gemini", 3, self.circuit_breaker.as_deref()).await
    }
}

#[async_trait]
impl LLMProvider for GeminiProvider {
    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let client = self.client().await?;
        let request = client
            .post(self.generate_url())
            .header("x-goog-api-key", &self.api_key)
            .json(&self.build_request_body(messages));

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Gemini").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        common::parse_gemini_completion_payload(&payload)
    }

    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let client = self.client().await?;
        let request = client
            .post(self.stream_url())
            .header("x-goog-api-key", &self.api_key)
            .json(&self.build_request_body(messages));

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Gemini").await?;
        let stream = response.bytes_stream().flat_map(|chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    common::parse_sse_lines(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| {
                            let content = common::parse_gemini_completion_payload(&payload).ok();
                            let usage = common::parse_gemini_usage(&payload);

                            if content.is_some() || usage.is_some() {
                                Some(StreamChunk {
                                    content,
                                    usage,
                                    ..Default::default()
                                })
                            } else {
                                None
                            }
                        })
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

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        let (in_rate, out_rate) = common::model_pricing_usd_per_million(&self.model);
        common::estimate_cost_usd(input_tokens, output_tokens, in_rate, out_rate)
    }

    fn model_name(&self) -> &str {
        &self.model
    }

    fn supports_thinking(&self) -> bool {
        self.supports_thinking_mode()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        if self.supports_thinking_mode() {
            &[
                ThinkingLevel::Low,
                ThinkingLevel::Medium,
                ThinkingLevel::High,
                ThinkingLevel::Max,
            ]
        } else {
            &[]
        }
    }

    #[instrument(skip(self, messages), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<crate::provider::LLMResponse> {
        // For non-thinking models, fall back to standard generate (Gemini doesn't have native tool support)
        if !self.supports_thinking_mode() || thinking == ThinkingLevel::Off {
            let content = self.generate(messages).await?;
            return Ok(crate::provider::LLMResponse {
                content,
                tool_calls: vec![],
                usage: None,
                thinking: None,
            });
        }

        let body = self.build_request_body_with_thinking(messages, thinking);
        let client = self.client().await?;
        let request = client
            .post(self.generate_url())
            .header("x-goog-api-key", &self.api_key)
            .json(&body);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Gemini").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        let content = common::parse_gemini_completion_payload(&payload).unwrap_or_default();
        let usage = common::parse_gemini_usage(&payload);
        let thinking_content = self.parse_thinking(&payload);

        Ok(crate::provider::LLMResponse {
            content,
            tool_calls: vec![],
            usage,
            thinking: thinking_content,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pool::ConnectionPool;
    use serde_json::json;

    fn test_provider(model: &str) -> GeminiProvider {
        let pool = Arc::new(ConnectionPool::new());
        GeminiProvider::new(pool, "test-key", model)
    }

    #[test]
    fn generate_url_format() {
        let p = test_provider("gemini-2.5-pro");
        assert_eq!(
            p.generate_url(),
            "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-pro:generateContent"
        );
    }

    #[test]
    fn stream_url_format() {
        let p = test_provider("gemini-2.5-flash");
        assert!(p.stream_url().contains("streamGenerateContent?alt=sse"));
        assert!(p.stream_url().contains("gemini-2.5-flash"));
    }

    #[test]
    fn build_request_body_basic() {
        let p = test_provider("gemini-2.5-pro");
        let messages = vec![Message::new(ava_types::Role::User, "Hello")];
        let body = p.build_request_body(&messages);
        assert!(body.get("contents").is_some());
    }

    #[test]
    fn build_request_body_with_system_instruction() {
        let p = test_provider("gemini-2.5-pro");
        let messages = vec![
            Message::new(ava_types::Role::System, "You are a helper"),
            Message::new(ava_types::Role::User, "Hi"),
        ];
        let body = p.build_request_body(&messages);
        assert!(body.get("system_instruction").is_some());
    }

    #[test]
    fn thinking_config_gemini25() {
        let p = test_provider("gemini-2.5-pro");
        assert!(p.supports_thinking_mode());
        assert!(!p.is_gemini3());

        let body = p.build_request_body_with_thinking(
            &[Message::new(ava_types::Role::User, "test")],
            ThinkingLevel::High,
        );
        let config = body.get("thinkingConfig").expect("should have thinkingConfig");
        assert_eq!(config["includeThoughts"], json!(true));
        assert_eq!(config["thinkingBudget"], json!(16000));
    }

    #[test]
    fn thinking_config_gemini3() {
        let p = test_provider("gemini-3-pro-preview");
        assert!(p.supports_thinking_mode());
        assert!(p.is_gemini3());

        let body = p.build_request_body_with_thinking(
            &[Message::new(ava_types::Role::User, "test")],
            ThinkingLevel::High,
        );
        let config = body.get("thinkingConfig").expect("should have thinkingConfig");
        assert_eq!(config["includeThoughts"], json!(true));
        assert_eq!(config["thinkingLevel"], json!("high"));
    }

    #[test]
    fn thinking_config_off_no_config() {
        let p = test_provider("gemini-2.5-pro");
        let body = p.build_request_body_with_thinking(
            &[Message::new(ava_types::Role::User, "test")],
            ThinkingLevel::Off,
        );
        assert!(body.get("thinkingConfig").is_none());
    }

    #[test]
    fn thinking_not_supported_for_old_models() {
        let p = test_provider("gemini-1.5-pro");
        assert!(!p.supports_thinking_mode());
        assert!(p.thinking_levels().is_empty());
    }

    #[test]
    fn thinking_levels_for_supported_models() {
        let p = test_provider("gemini-2.5-flash");
        assert!(p.supports_thinking());
        assert_eq!(p.thinking_levels().len(), 4);
    }

    #[test]
    fn parse_thinking_from_response() {
        let p = test_provider("gemini-2.5-pro");

        let payload = json!({
            "candidates": [{
                "content": {
                    "parts": [
                        { "text": "thinking step 1", "thought": true },
                        { "text": "thinking step 2", "thought": true },
                        { "text": "The answer is 42" }
                    ]
                }
            }]
        });

        let thinking = p.parse_thinking(&payload);
        assert!(thinking.is_some());
        let text = thinking.unwrap();
        assert!(text.contains("thinking step 1"));
        assert!(text.contains("thinking step 2"));
    }

    #[test]
    fn parse_thinking_returns_none_without_thoughts() {
        let p = test_provider("gemini-2.5-pro");
        let payload = json!({
            "candidates": [{
                "content": {
                    "parts": [
                        { "text": "The answer is 42" }
                    ]
                }
            }]
        });
        assert!(p.parse_thinking(&payload).is_none());
    }

    #[test]
    fn model_name_returns_model() {
        let p = test_provider("gemini-2.5-flash");
        assert_eq!(p.model_name(), "gemini-2.5-flash");
    }

    #[test]
    fn thinking_budget_values() {
        let p = test_provider("gemini-2.5-pro");

        for (level, expected_budget) in [
            (ThinkingLevel::Low, 4000),
            (ThinkingLevel::Medium, 8000),
            (ThinkingLevel::High, 16000),
            (ThinkingLevel::Max, 24576),
        ] {
            let body = p.build_request_body_with_thinking(
                &[Message::new(ava_types::Role::User, "x")],
                level,
            );
            let budget = body["thinkingConfig"]["thinkingBudget"].as_u64().unwrap();
            assert_eq!(budget, expected_budget, "wrong budget for {level:?}");
        }
    }
}
