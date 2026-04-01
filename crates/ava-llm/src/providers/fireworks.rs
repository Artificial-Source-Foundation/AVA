use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{Message, Result, StreamChunk, ThinkingLevel};
use futures::Stream;

use tracing::instrument;

use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse, ProviderCapabilities};
use crate::providers::openai::OpenAIProvider;

/// Fireworks AI provider — OpenAI-compatible inference API.
///
/// Supports two billing modes:
/// - **API** (pay-per-token): standard Fireworks API key, any hosted model.
/// - **Fire Pass** ($7/wk subscription): flat-rate access to select models.
///   Currently includes `accounts/fireworks/routers/kimi-k2p5-turbo` (Kimi K2.5 Turbo).
///
/// Both modes use the same endpoint (`https://api.fireworks.ai/inference/v1`)
/// and `fw-*` API keys. Fire Pass billing applies automatically when the
/// subscription is active.
///
/// Models use full Fireworks IDs like `accounts/fireworks/models/llama-v3p1-405b-instruct`
/// or router IDs like `accounts/fireworks/routers/kimi-k2p5-turbo`.
#[derive(Clone)]
pub struct FireworksProvider {
    inner: OpenAIProvider,
    is_fire_pass_model: bool,
}

const FIREWORKS_BASE_URL: &str = "https://api.fireworks.ai/inference";

/// Models included in Fire Pass (flat-rate subscription).
const FIRE_PASS_MODELS: &[&str] = &[
    "accounts/fireworks/routers/kimi-k2p5-turbo",
    "kimi-k2p5-turbo",
    "kimi-k2.5-turbo",
];

impl FireworksProvider {
    pub fn new(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
    ) -> Self {
        Self::with_base_url(pool, api_key, model, FIREWORKS_BASE_URL)
    }

    pub fn with_base_url(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
        base_url: impl Into<String>,
    ) -> Self {
        let model = Self::resolve_alias(model.into());
        let is_fire_pass_model = FIRE_PASS_MODELS
            .iter()
            .any(|m| m.eq_ignore_ascii_case(&model));
        Self {
            inner: OpenAIProvider::with_base_url(pool, api_key, model, base_url)
                .with_provider_label("fireworks")
                .with_subscription(is_fire_pass_model),
            is_fire_pass_model,
        }
    }

    /// Resolve short model aliases to full Fireworks model IDs.
    fn resolve_alias(model: String) -> String {
        match model.as_str() {
            "kimi-k2p5-turbo" | "kimi-k2.5-turbo" => {
                "accounts/fireworks/routers/kimi-k2p5-turbo".to_string()
            }
            _ => model,
        }
    }
}

#[async_trait]
impl LLMProvider for FireworksProvider {
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
        if self.is_fire_pass_model {
            // Fire Pass is flat-rate — no per-token cost
            0.0
        } else {
            self.inner.estimate_cost(input_tokens, output_tokens)
        }
    }

    fn model_name(&self) -> &str {
        self.inner.model_name()
    }

    fn capabilities(&self) -> ProviderCapabilities {
        ProviderCapabilities {
            supports_streaming: true,
            supports_tool_use: true,
            supports_thinking: false,
            supports_thinking_levels: false,
            supports_images: true,
            max_context_window: 256_000,
            supports_prompt_caching: false,
            is_subscription: self.is_fire_pass_model,
        }
    }

    fn provider_kind(&self) -> crate::message_transform::ProviderKind {
        crate::message_transform::ProviderKind::OpenAI
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
        false
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        &[]
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.inner.model_name(), thinking = ?thinking))]
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        let _ = thinking;
        self.inner.generate_with_tools(messages, tools).await
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.inner.model_name(), thinking = ?thinking))]
    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let _ = thinking;
        self.inner.generate_stream_with_tools(messages, tools).await
    }
}
