use std::pin::Pin;
use std::sync::Arc;
use std::time::Instant;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::instrument;

use crate::circuit_breaker::CircuitBreaker;
use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse, ProviderCapabilities};
use crate::providers::common;

/// Tracks Responses API reasoning timing to convert placeholder sentinels
/// into human-readable "Thought for X.Xs" messages.
///
/// When an unverified org uses the Responses API, no reasoning summary text
/// is streamed. Instead, we get start/end sentinels and compute elapsed time.
struct ReasoningTimer {
    start: Option<Instant>,
    had_real_content: bool,
}

impl ReasoningTimer {
    fn new() -> Self {
        Self {
            start: None,
            had_real_content: false,
        }
    }

    /// Process a parsed stream chunk, translating reasoning sentinels into
    /// timed thinking messages. Returns the (possibly modified) chunks to emit.
    fn process(&mut self, chunk: StreamChunk) -> Vec<StreamChunk> {
        if let Some(ref thinking) = chunk.thinking {
            if thinking == common::REASONING_START_SENTINEL {
                self.start = Some(Instant::now());
                self.had_real_content = false;
                // Don't emit anything yet — wait to see if real content arrives.
                // If no summary text is streamed, we suppress the thinking bubble entirely.
                return vec![];
            }
            if thinking == common::REASONING_END_SENTINEL {
                self.start = None;
                if !self.had_real_content {
                    // No summary text was streamed — suppress thinking display entirely.
                    // The model still reasoned (thinking is enabled), but the backend
                    // didn't expose readable summaries for this model.
                    return vec![];
                }
                return vec![];
            }
            // Real thinking content arrived (e.g., reasoning summary deltas)
            if self.start.is_some() {
                self.had_real_content = true;
            }
        }
        vec![chunk]
    }
}

/// Heuristic check for whether a base URL likely points to a LiteLLM proxy.
///
/// Returns `true` if the URL contains common LiteLLM proxy indicators:
/// - The hostname contains "litellm"
/// - The URL path ends with `/v1` (common for self-hosted proxies on non-standard ports)
///
/// This is used for auto-detection when `litellm_compatible` is not explicitly set.
pub fn looks_like_litellm_proxy(base_url: &str) -> bool {
    let lower = base_url.to_lowercase();
    lower.contains("litellm")
}

/// Returns a minimal dummy tool definition for LiteLLM proxy compatibility.
///
/// Some LiteLLM proxy configurations fail to route requests correctly when no
/// tools are present. This injects a harmless no-op tool that the model will
/// never call, satisfying LiteLLM's routing requirements.
fn litellm_dummy_tools() -> Vec<Value> {
    vec![json!({
        "type": "function",
        "function": {
            "name": "litellm_noop",
            "description": "Reserved no-op tool for LiteLLM proxy compatibility. Do not call this tool.",
            "parameters": {
                "type": "object",
                "properties": {},
                "required": []
            }
        }
    })]
}

/// Thinking/reasoning format variants for OpenAI-compatible providers.
#[derive(Clone, Debug, PartialEq)]
pub enum ThinkingFormat {
    /// Standard OpenAI: `reasoning_effort` field
    OpenAI,
    /// DashScope (Alibaba): `enable_thinking: true` for reasoning models
    DashScope,
    /// ZAI/ZhipuAI: `thinking: { type: "enabled", clear_thinking: false }`
    Zhipu,
}

#[derive(Clone)]
pub struct OpenAIProvider {
    pool: Arc<ConnectionPool>,
    api_key: String,
    model: String,
    base_url: String,
    thinking_format: ThinkingFormat,
    circuit_breaker: Option<Arc<CircuitBreaker>>,
    /// When true, use the OpenAI Responses API format (`/responses` endpoint)
    /// instead of the Chat Completions API (`/v1/chat/completions`).
    /// Required for ChatGPT OAuth tokens at `chatgpt.com/backend-api/codex`.
    use_responses_api: bool,
    /// When true, inject a dummy tool into requests with empty tool lists
    /// to prevent LiteLLM proxy routing issues with certain models.
    litellm_compatible: bool,
    /// When true, this provider uses an OAuth subscription (ChatGPT Plus/Pro)
    /// so cost estimation returns 0 (subscription-billed, no per-token cost).
    subscription: bool,
    /// ChatGPT account ID for OAuth subscriptions.
    /// Sent as `ChatGPT-Account-ID` header with requests to chatgpt.com.
    chatgpt_account_id: Option<String>,
}

impl OpenAIProvider {
    pub fn new(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
    ) -> Self {
        Self::with_base_url(pool, api_key, model, "https://api.openai.com")
    }

    pub fn with_base_url(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
        base_url: impl Into<String>,
    ) -> Self {
        let base_url = base_url.into();
        // Use Responses API for native OpenAI (api.openai.com, chatgpt.com).
        // Responses API supports reasoning summaries for both API keys and OAuth tokens.
        // Use Chat Completions for third-party OpenAI-compatible APIs (Ollama, Inception, etc.)
        let is_native = base_url.to_lowercase().contains("api.openai.com")
            || base_url.to_lowercase().contains("chatgpt.com");
        let use_responses_api = is_native;
        Self {
            pool,
            api_key: api_key.into(),
            model: model.into(),
            base_url,
            thinking_format: ThinkingFormat::OpenAI,
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
            use_responses_api,
            litellm_compatible: false,
            subscription: false,
            chatgpt_account_id: None,
        }
    }

    /// Returns `true` if using OAuth subscription (ChatGPT Plus/Pro — no per-token cost).
    fn is_subscription(&self) -> bool {
        self.subscription
    }

    /// Set the thinking format for this provider (DashScope, Zhipu, etc.).
    pub fn with_thinking_format(mut self, format: ThinkingFormat) -> Self {
        self.thinking_format = format;
        self
    }

    /// Enable the Responses API mode for ChatGPT OAuth tokens.
    ///
    /// When enabled, requests use the Responses API format (`/responses` endpoint)
    /// instead of the Chat Completions API (`/v1/chat/completions`).
    pub fn with_responses_api(mut self, enabled: bool) -> Self {
        self.use_responses_api = enabled;
        self
    }

    /// Enable LiteLLM proxy compatibility mode.
    ///
    /// When enabled, a minimal dummy tool is injected into requests that have
    /// no tools, preventing LiteLLM proxy routing issues with certain models.
    pub fn with_litellm_compatible(mut self, enabled: bool) -> Self {
        self.litellm_compatible = enabled;
        self
    }

    /// Mark this provider as using an OAuth subscription (no per-token cost).
    pub fn with_subscription(mut self, enabled: bool) -> Self {
        self.subscription = enabled;
        self
    }

    /// Set the ChatGPT account ID for OAuth subscriptions.
    pub fn with_chatgpt_account_id(mut self, account_id: Option<String>) -> Self {
        self.chatgpt_account_id = account_id;
        self
    }

    /// Check if the current model supports reasoning/thinking.
    /// Supports GPT-5.x, Codex, o3, o4, GLM, Qwen reasoning models.
    fn supports_reasoning(&self) -> bool {
        let model_lower = self.model.to_lowercase();
        match self.thinking_format {
            ThinkingFormat::OpenAI => {
                model_lower.contains("gpt-5")
                    || model_lower.contains("codex")
                    || model_lower.starts_with("o3")
                    || model_lower.starts_with("o4")
            }
            // DashScope: reasoning models (excluding kimi-k2-thinking which returns thinking by default)
            ThinkingFormat::DashScope => {
                !model_lower.contains("kimi-k2-thinking")
                    && (model_lower.contains("qwen")
                        || model_lower.contains("qwq")
                        || model_lower.contains("deepseek-r1")
                        || model_lower.contains("kimi"))
            }
            // ZAI/ZhipuAI: all GLM models support thinking
            ThinkingFormat::Zhipu => true,
        }
    }

    /// Whether this model supports the "xhigh" reasoning effort level.
    /// Codex 5.2+, Codex 5.3+, GPT-5.3+ support xhigh per OpenCode's transform.ts.
    fn supports_xhigh(&self) -> bool {
        let model_lower = self.model.to_lowercase();
        // Codex 5.2 or 5.3
        if model_lower.contains("codex") {
            return model_lower.contains("5.2") || model_lower.contains("5.3");
        }
        // GPT-5.3+, GPT-5.4+
        model_lower.contains("gpt-5.3")
            || model_lower.contains("gpt-5.4")
            || model_lower.contains("gpt-5.5")
    }

    /// Return the maximum reasoning effort string for this model.
    fn max_reasoning_effort(&self) -> &str {
        if self.supports_xhigh() {
            "xhigh"
        } else {
            "high"
        }
    }

    /// The completions endpoint URL, respecting the Responses API mode.
    fn completions_url(&self) -> String {
        if self.use_responses_api {
            // ChatGPT OAuth uses a custom base URL that already includes the path prefix.
            // Native OpenAI (api.openai.com) needs /v1/responses.
            let base = self.base_url.trim_end_matches('/');
            if base.contains("chatgpt.com") {
                format!("{base}/responses")
            } else {
                format!("{base}/v1/responses")
            }
        } else {
            format!("{}/v1/chat/completions", self.base_url)
        }
    }

    /// Build a Responses API request body.
    ///
    /// The Responses API uses `instructions` (system prompt) + `input` (messages)
    /// instead of a flat `messages` array.
    fn build_responses_request_body(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
    ) -> Value {
        // Extract system message as "instructions"
        let instructions: String = messages
            .iter()
            .filter(|m| m.role == ava_types::Role::System)
            .map(|m| m.content.as_str())
            .collect::<Vec<_>>()
            .join("\n");

        // Convert non-system messages to "input" format
        let input: Vec<Value> = messages
            .iter()
            .filter(|m| m.role != ava_types::Role::System)
            .map(|m| {
                let role = match m.role {
                    ava_types::Role::User => "user",
                    ava_types::Role::Assistant => "assistant",
                    ava_types::Role::Tool => "tool",
                    ava_types::Role::System => unreachable!(),
                };

                // Handle tool result messages
                if m.role == ava_types::Role::Tool {
                    if let Some(ref tool_call_id) = m.tool_call_id {
                        return json!({
                            "type": "function_call_output",
                            "call_id": tool_call_id,
                            "output": m.content,
                        });
                    }
                }

                // Handle assistant messages with tool calls
                if m.role == ava_types::Role::Assistant && !m.tool_calls.is_empty() {
                    let mut items = Vec::new();
                    // Add text content if present
                    if !m.content.is_empty() {
                        items.push(json!({
                            "type": "message",
                            "role": "assistant",
                            "content": [{"type": "output_text", "text": m.content}],
                        }));
                    }
                    // Add function calls
                    for tc in &m.tool_calls {
                        items.push(json!({
                            "type": "function_call",
                            "call_id": tc.id,
                            "name": tc.name,
                            "arguments": tc.arguments.to_string(),
                        }));
                    }
                    // For a single function call with no text, return it directly
                    if items.len() == 1 {
                        return items
                            .into_iter()
                            .next()
                            .unwrap_or_else(|| Value::Array(vec![]));
                    }
                    // Multiple items: return as array (the API accepts mixed arrays)
                    return Value::Array(items);
                }

                json!({
                    "role": role,
                    "content": m.content,
                })
            })
            .flat_map(|v| {
                // Flatten arrays from multi-item assistant messages
                if let Value::Array(items) = v {
                    items
                } else {
                    vec![v]
                }
            })
            .collect();

        let mut body = json!({
            "model": self.model,
            "instructions": instructions,
            "input": input,
            "stream": stream,
            "store": false,
        });

        // Add tools in Responses API format
        if !tools.is_empty() {
            body["tools"] = json!(common::tools_to_responses_api_format(tools));
        } else if self.litellm_compatible {
            body["tools"] = json!(litellm_dummy_tools());
        }

        body
    }

    /// Build a Responses API request body with reasoning support.
    fn build_responses_request_body_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
        thinking: ThinkingLevel,
    ) -> Value {
        let mut body = self.build_responses_request_body(messages, tools, stream);

        if thinking != ThinkingLevel::Off && self.supports_reasoning() {
            let effort = match thinking {
                ThinkingLevel::Off => unreachable!(),
                ThinkingLevel::Low => "low",
                ThinkingLevel::Medium => "medium",
                ThinkingLevel::High => "high",
                ThinkingLevel::Max => self.max_reasoning_effort(),
            };
            body["reasoning"] = json!({
                "effort": effort,
                "summary": "auto",
            });
            body["include"] = json!(["reasoning.encrypted_content"]);
        }

        body
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
        } else if self.litellm_compatible {
            body["tools"] = json!(litellm_dummy_tools());
        }
        body
    }

    /// Build request body with reasoning support (Chat Completions API).
    fn build_request_body_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        stream: bool,
        thinking: ThinkingLevel,
    ) -> Value {
        let mut body = self.build_request_body(messages, stream);

        if !tools.is_empty() {
            body["tools"] = json!(common::tools_to_openai_format(tools));
        } else if self.litellm_compatible {
            body["tools"] = json!(litellm_dummy_tools());
        }

        if thinking != ThinkingLevel::Off && self.supports_reasoning() {
            match self.thinking_format {
                ThinkingFormat::OpenAI => {
                    // Chat Completions API only supports `reasoning_effort`.
                    // `reasoning_summary` and `include` are Responses API fields.
                    let effort = match thinking {
                        ThinkingLevel::Off => unreachable!(),
                        ThinkingLevel::Low => "low",
                        ThinkingLevel::Medium => "medium",
                        ThinkingLevel::High => "high",
                        ThinkingLevel::Max => self.max_reasoning_effort(),
                    };
                    body["reasoning_effort"] = json!(effort);
                }
                ThinkingFormat::DashScope => {
                    // Alibaba DashScope: enable_thinking field
                    body["enable_thinking"] = json!(true);
                }
                ThinkingFormat::Zhipu => {
                    // ZAI/ZhipuAI: thinking object with clear_thinking
                    body["thinking"] = json!({
                        "type": "enabled",
                        "clear_thinking": false
                    });
                }
            }
        }

        body
    }

    /// Parse reasoning content from OpenAI response.
    fn parse_reasoning(&self, payload: &Value) -> Option<String> {
        // OpenAI returns reasoning in message.reasoning_content or similar field
        // The exact field may vary by model, try common locations
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

    pub fn parse_response_payload(payload: &Value) -> Result<String> {
        common::parse_openai_completion_payload(payload)
    }

    /// Label used in error messages and credential lookups.
    fn provider_label(&self) -> String {
        "openai".to_string()
    }

    async fn client(&self) -> Result<Arc<reqwest::Client>> {
        self.pool.get_client(&self.base_url).await
    }

    /// Add auth headers to a request. Includes ChatGPT-Account-ID for OAuth subscriptions.
    fn auth_request(&self, request: reqwest::RequestBuilder) -> reqwest::RequestBuilder {
        let mut req = request.bearer_auth(&self.api_key);
        if let Some(ref account_id) = self.chatgpt_account_id {
            req = req
                .header("ChatGPT-Account-ID", account_id)
                .header("originator", "codex_cli_rs");
        }
        req
    }

    async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
        common::send_with_retry_cb(request, "OpenAI", 3, self.circuit_breaker.as_deref()).await
    }
}

#[async_trait]
impl LLMProvider for OpenAIProvider {
    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let provider_label = self.provider_label();
        let client = self.client().await?;
        let body = if self.use_responses_api {
            self.build_responses_request_body(messages, &[], false)
        } else {
            self.build_request_body(messages, false)
        };
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, &provider_label).await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        if self.use_responses_api {
            let (content, _, _, _) = common::parse_responses_api_payload(&payload);
            if content.is_empty() {
                Err(AvaError::ProviderError {
                    provider: provider_label,
                    message: "empty response from Responses API".to_string(),
                })
            } else {
                Ok(content)
            }
        } else {
            Self::parse_response_payload(&payload)
        }
    }

    #[instrument(skip(self, messages), fields(model = %self.model))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let provider_label = self.provider_label();
        let client = self.client().await?;
        let body = if self.use_responses_api {
            self.build_responses_request_body(messages, &[], true)
        } else {
            self.build_request_body(messages, true)
        };
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, &provider_label).await?;
        let use_responses = self.use_responses_api;
        let mut sse_parser = common::SseParser::new();
        let mut reasoning_timer = ReasoningTimer::new();
        let stream = response.bytes_stream().flat_map(move |chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    sse_parser
                        .feed(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| {
                            if use_responses {
                                common::parse_responses_api_stream_chunk(&payload)
                            } else {
                                common::parse_openai_stream_chunk(&payload)
                            }
                        })
                        .flat_map(|c| {
                            if use_responses {
                                reasoning_timer.process(c)
                            } else {
                                vec![c]
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
        common::estimate_tokens_for_model(input, &self.model)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        // ChatGPT OAuth is subscription-billed — no per-token cost.
        // Native OpenAI API key users pay per-token even with Responses API.
        if self.is_subscription() {
            return 0.0;
        }
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
            supports_thinking: self.supports_reasoning(),
            supports_thinking_levels: self.supports_reasoning(),
            supports_images: true,
            max_context_window: 128_000,
            supports_prompt_caching: false,
            is_subscription: self.is_subscription(),
        }
    }

    fn provider_kind(&self) -> crate::message_transform::ProviderKind {
        crate::message_transform::ProviderKind::OpenAI
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
        let provider_label = self.provider_label();
        let body = if self.use_responses_api {
            self.build_responses_request_body(messages, tools, false)
        } else {
            self.build_request_body_with_tools(messages, tools, false)
        };
        tracing::debug!(
            message_count = messages.len(),
            tool_count = tools.len(),
            responses_api = self.use_responses_api,
            "Sending generate_with_tools request"
        );
        let client = self.client().await?;
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, &provider_label).await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        if self.use_responses_api {
            let (content, tool_calls, usage, thinking) =
                common::parse_responses_api_payload(&payload);
            Ok(LLMResponse {
                content,
                tool_calls,
                usage,
                thinking,
            })
        } else {
            let content = Self::parse_response_payload(&payload).unwrap_or_default();
            let tool_calls = common::parse_openai_tool_calls(&payload);
            let usage = common::parse_usage(&payload);

            Ok(LLMResponse {
                content,
                tool_calls,
                usage,
                thinking: None,
            })
        }
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model))]
    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let provider_label = self.provider_label();
        let mut body = if self.use_responses_api {
            self.build_responses_request_body(messages, tools, true)
        } else {
            self.build_request_body_with_tools(messages, tools, true)
        };
        if !self.use_responses_api {
            // Request usage in the final streaming chunk (Chat Completions only)
            body["stream_options"] = json!({"include_usage": true});
        }
        let client = self.client().await?;
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, &provider_label).await?;
        let use_responses = self.use_responses_api;
        let mut sse_parser = common::SseParser::new();
        let mut reasoning_timer = ReasoningTimer::new();
        let stream = response.bytes_stream().flat_map(move |chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    sse_parser
                        .feed(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| {
                            if use_responses {
                                common::parse_responses_api_stream_chunk(&payload)
                            } else {
                                common::parse_openai_stream_chunk(&payload)
                            }
                        })
                        .flat_map(|c| {
                            if use_responses {
                                reasoning_timer.process(c)
                            } else {
                                vec![c]
                            }
                        })
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(chunks)
        });

        Ok(Box::pin(stream))
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

        let provider_label = self.provider_label();
        let mut body = if self.use_responses_api {
            self.build_responses_request_body_with_thinking(messages, tools, true, thinking)
        } else {
            self.build_request_body_with_thinking(messages, tools, true, thinking)
        };
        if !self.use_responses_api {
            body["stream_options"] = json!({"include_usage": true});
        }
        let client = self.client().await?;
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, &provider_label).await?;
        let use_responses = self.use_responses_api;
        let mut sse_parser = common::SseParser::new();
        let mut reasoning_timer = ReasoningTimer::new();
        let stream = response.bytes_stream().flat_map(move |chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    sse_parser
                        .feed(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| {
                            if use_responses {
                                common::parse_responses_api_stream_chunk(&payload)
                            } else {
                                common::parse_openai_stream_chunk(&payload)
                            }
                        })
                        .flat_map(|c| {
                            if use_responses {
                                reasoning_timer.process(c)
                            } else {
                                vec![c]
                            }
                        })
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(chunks)
        });

        Ok(Box::pin(stream))
    }

    fn supports_thinking(&self) -> bool {
        self.supports_reasoning()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        if !self.supports_reasoning() {
            &[]
        } else if self.supports_xhigh() {
            // Codex 5.2+, GPT-5.3+: support xhigh
            &[
                ThinkingLevel::Low,
                ThinkingLevel::Medium,
                ThinkingLevel::High,
                ThinkingLevel::Max,
            ]
        } else {
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
        // For non-reasoning models, fall back to standard generate_with_tools
        if !self.supports_reasoning() || thinking == ThinkingLevel::Off {
            return self.generate_with_tools(messages, tools).await;
        }

        let provider_label = self.provider_label();
        let body = if self.use_responses_api {
            self.build_responses_request_body_with_thinking(messages, tools, false, thinking)
        } else {
            self.build_request_body_with_thinking(messages, tools, false, thinking)
        };
        let client = self.client().await?;
        let request = client.post(self.completions_url()).json(&body);
        let request = self.auth_request(request);

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, &provider_label).await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        if self.use_responses_api {
            let (content, tool_calls, usage, thinking) =
                common::parse_responses_api_payload(&payload);
            Ok(LLMResponse {
                content,
                tool_calls,
                usage,
                thinking,
            })
        } else {
            let content = Self::parse_response_payload(&payload).unwrap_or_default();
            let tool_calls = common::parse_openai_tool_calls(&payload);
            let usage = common::parse_usage(&payload);
            let thinking_content = self.parse_reasoning(&payload);

            Ok(LLMResponse {
                content,
                tool_calls,
                usage,
                thinking: thinking_content,
            })
        }
    }
}
