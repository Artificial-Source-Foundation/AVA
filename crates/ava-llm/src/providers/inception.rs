use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{Message, Result, StreamChunk, ThinkingLevel};
use futures::Stream;

use tracing::instrument;

use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse, ProviderCapabilities};
use crate::providers::openai::OpenAIProvider;

/// Inception Labs provider for Mercury diffusion models.
///
/// Mercury models are OpenAI-compatible and accessed via `https://api.inceptionlabs.ai/v1`.
/// They support tool/function calling and streaming. Mercury models are diffusion-based
/// LLMs — they do not support reasoning/thinking modes.
///
/// Models:
/// - `mercury-2` — flagship diffusion model (128K context, 50K max output)
/// - `mercury` — previous generation (128K context, 32K max output)
/// - `mercury-coder-small` — compact coding model (128K context, 32K max output)
#[derive(Clone)]
pub struct InceptionProvider {
    inner: OpenAIProvider,
}

const INCEPTION_BASE_URL: &str = "https://api.inceptionlabs.ai";

impl InceptionProvider {
    pub fn new(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
    ) -> Self {
        Self::with_base_url(pool, api_key, model, INCEPTION_BASE_URL)
    }

    pub fn with_base_url(
        pool: Arc<ConnectionPool>,
        api_key: impl Into<String>,
        model: impl Into<String>,
        base_url: impl Into<String>,
    ) -> Self {
        let model = Self::resolve_alias(model.into());
        Self {
            inner: OpenAIProvider::with_base_url(pool, api_key, model, base_url),
        }
    }

    /// Resolve model aliases to canonical model IDs.
    fn resolve_alias(model: String) -> String {
        match model.as_str() {
            "mercury-coder" => "mercury-coder-small".to_string(),
            _ => model,
        }
    }
}

#[async_trait]
impl LLMProvider for InceptionProvider {
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
        self.inner.estimate_cost(input_tokens, output_tokens)
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
            supports_images: false,
            max_context_window: 32_000,
            supports_prompt_caching: false,
            is_subscription: false,
        }
    }

    fn provider_kind(&self) -> crate::message_transform::ProviderKind {
        crate::message_transform::ProviderKind::Inception
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

    /// Mercury models are diffusion-based and do not support reasoning/thinking.
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
        // Mercury models don't support thinking — fall back to standard tool calling
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
        // Mercury models don't support thinking — fall back to standard streaming
        let _ = thinking;
        self.inner.generate_stream_with_tools(messages, tools).await
    }
}
