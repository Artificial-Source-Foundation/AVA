use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{Message, Result, Tool, ToolCall};
use futures::Stream;

/// Response from an LLM that may include both text content and native tool calls.
#[derive(Debug, Clone, Default)]
pub struct LLMResponse {
    pub content: String,
    pub tool_calls: Vec<ToolCall>,
}

#[async_trait]
pub trait LLMProvider: Send + Sync {
    async fn generate(&self, messages: &[Message]) -> Result<String>;
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>>;
    fn estimate_tokens(&self, input: &str) -> usize;
    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64;
    fn model_name(&self) -> &str;

    /// Whether this provider supports native tool calling via the API.
    fn supports_tools(&self) -> bool {
        false
    }

    /// Generate a response with tool definitions sent to the provider.
    /// Default implementation falls back to `generate()`.
    async fn generate_with_tools(
        &self,
        messages: &[Message],
        _tools: &[Tool],
    ) -> Result<LLMResponse> {
        let text = self.generate(messages).await?;
        Ok(LLMResponse {
            content: text,
            tool_calls: vec![],
        })
    }
}

/// Wrapper that delegates all `LLMProvider` methods to an `Arc<dyn LLMProvider>`.
/// Allows sharing a single provider across multiple consumers that need `Box<dyn LLMProvider>`.
pub struct SharedProvider {
    inner: Arc<dyn LLMProvider>,
}

impl SharedProvider {
    pub fn new(inner: Arc<dyn LLMProvider>) -> Self {
        Self { inner }
    }
}

#[async_trait]
impl LLMProvider for SharedProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        self.inner.generate(messages).await
    }

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
        self.inner.estimate_cost(input_tokens, output_tokens)
    }

    fn model_name(&self) -> &str {
        self.inner.model_name()
    }

    fn supports_tools(&self) -> bool {
        self.inner.supports_tools()
    }

    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<LLMResponse> {
        self.inner.generate_with_tools(messages, tools).await
    }
}
