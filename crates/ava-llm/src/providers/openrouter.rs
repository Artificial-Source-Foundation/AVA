use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{Message, Result};
use futures::Stream;

use tracing::instrument;

use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse};
use crate::providers::openai::OpenAIProvider;

#[derive(Clone)]
pub struct OpenRouterProvider {
    inner: OpenAIProvider,
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
        Self {
            inner: OpenAIProvider::with_base_url(pool, api_key, model, base_url),
        }
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
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
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
}
