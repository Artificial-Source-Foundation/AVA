use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{
    Message, Result, StreamChunk, StreamToolCall, ThinkingLevel, TokenUsage, Tool, ToolCall,
};
use futures::Stream;

use crate::message_transform::{normalize_messages, ProviderKind};
use crate::thinking::{ResolvedThinkingConfig, ThinkingBudgetFallback, ThinkingConfig};

/// Response from an LLM that may include both text content and native tool calls.
#[derive(Debug, Clone, Default)]
pub struct LLMResponse {
    pub content: String,
    pub tool_calls: Vec<ToolCall>,
    pub usage: Option<TokenUsage>,
    /// Thinking/reasoning content from models that support extended thinking.
    /// This is the model's internal reasoning, separate from the main response.
    pub thinking: Option<String>,
}

/// Interface for LLM providers (Anthropic, OpenAI, Gemini, Ollama, OpenRouter).
///
/// Implementations handle API communication, token estimation, and cost tracking.
/// The agent loop calls `generate_with_tools` for native tool-calling models,
/// falling back to `generate` for text-only completions.
#[async_trait]
pub trait LLMProvider: Send + Sync {
    /// Generate a text completion from the given message history.
    async fn generate(&self, messages: &[Message]) -> Result<String>;
    /// Generate a streaming completion, yielding rich chunks with content, tool calls, usage, and thinking.
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>>;
    /// Estimate the token count for the given text (heuristic, not exact).
    fn estimate_tokens(&self, input: &str) -> usize;
    /// Estimate cost in USD for the given token counts.
    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64;
    /// The model identifier string (e.g., "claude-sonnet-4-6").
    fn model_name(&self) -> &str;

    /// The provider family for cross-provider message normalization.
    /// Override this in provider implementations; defaults to `OpenAI` (most common).
    fn provider_kind(&self) -> ProviderKind {
        ProviderKind::OpenAI
    }

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
            usage: None,
            thinking: None,
        })
    }

    /// Whether this provider/model supports thinking/reasoning modes.
    fn supports_thinking(&self) -> bool {
        false
    }

    /// Available thinking levels for the current model.
    /// Returns empty slice if thinking not supported.
    fn thinking_levels(&self) -> &[ThinkingLevel] {
        &[]
    }

    /// Resolve a requested thinking config into the provider-specific settings
    /// the runtime can safely apply.
    fn resolve_thinking_config(&self, config: ThinkingConfig) -> ResolvedThinkingConfig {
        if !config.is_enabled() {
            return ResolvedThinkingConfig::disabled();
        }

        if !self.supports_thinking() {
            return ResolvedThinkingConfig::unsupported(config);
        }

        let fallback = config
            .budget_tokens
            .map(|_| ThinkingBudgetFallback::Ignored);
        ResolvedThinkingConfig::qualitative(config, fallback)
    }

    /// Generate with tools AND thinking level.
    /// Default falls back to generate_with_tools ignoring thinking.
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        _thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        // Default: ignore thinking level
        self.generate_with_tools(messages, tools).await
    }

    /// Generate with tools and a provider-specific thinking configuration.
    async fn generate_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<LLMResponse> {
        self.generate_with_thinking(messages, tools, config.level)
            .await
    }

    /// Streaming generation with tool definitions.
    /// Returns a stream of `StreamChunk`s carrying text deltas, tool call fragments, usage, and thinking.
    /// Default implementation falls back to non-streaming `generate_with_tools()` and emits the result as chunks.
    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let response = self.generate_with_tools(messages, tools).await?;
        let mut chunks = Vec::new();
        if !response.content.is_empty() {
            chunks.push(StreamChunk::text(&response.content));
        }
        for (i, tc) in response.tool_calls.into_iter().enumerate() {
            chunks.push(StreamChunk {
                tool_call: Some(StreamToolCall {
                    index: i,
                    id: Some(tc.id),
                    name: Some(tc.name),
                    arguments_delta: Some(tc.arguments.to_string()),
                }),
                ..Default::default()
            });
        }
        if let Some(usage) = response.usage {
            chunks.push(StreamChunk::with_usage(usage));
        } else {
            chunks.push(StreamChunk::finished());
        }
        Ok(Box::pin(futures::stream::iter(chunks)))
    }

    /// Streaming generation with tools AND thinking level.
    /// Default falls back to `generate_stream_with_tools()` ignoring thinking.
    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        _thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.generate_stream_with_tools(messages, tools).await
    }

    /// Streaming generation with tools and a provider-specific thinking configuration.
    async fn generate_stream_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.generate_stream_with_thinking(messages, tools, config.level)
            .await
    }
}

/// Wrapper that normalizes messages for cross-provider compatibility before
/// delegating to the inner provider. Use this when switching providers
/// mid-conversation or for provider fallback.
///
/// ```rust,ignore
/// let provider = create_provider("openai", "gpt-4.1", &creds, pool)?;
/// let normalizing = NormalizingProvider::new(provider);
/// // Messages from an Anthropic conversation will be automatically
/// // normalized before being sent to OpenAI.
/// normalizing.generate_with_tools(&messages, &tools).await?;
/// ```
pub struct NormalizingProvider {
    inner: Box<dyn LLMProvider>,
}

impl NormalizingProvider {
    pub fn new(inner: Box<dyn LLMProvider>) -> Self {
        Self { inner }
    }

    fn normalize(&self, messages: &[Message]) -> Vec<Message> {
        normalize_messages(messages, self.inner.provider_kind())
    }
}

#[async_trait]
impl LLMProvider for NormalizingProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let normalized = self.normalize(messages);
        self.inner.generate(&normalized).await
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let normalized = self.normalize(messages);
        self.inner.generate_stream(&normalized).await
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

    fn provider_kind(&self) -> ProviderKind {
        self.inner.provider_kind()
    }

    fn supports_tools(&self) -> bool {
        self.inner.supports_tools()
    }

    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<LLMResponse> {
        let normalized = self.normalize(messages);
        self.inner.generate_with_tools(&normalized, tools).await
    }

    fn supports_thinking(&self) -> bool {
        self.inner.supports_thinking()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        self.inner.thinking_levels()
    }

    fn resolve_thinking_config(&self, config: ThinkingConfig) -> ResolvedThinkingConfig {
        self.inner.resolve_thinking_config(config)
    }

    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        let normalized = self.normalize(messages);
        self.inner
            .generate_with_thinking(&normalized, tools, thinking)
            .await
    }

    async fn generate_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<LLMResponse> {
        let normalized = self.normalize(messages);
        self.inner
            .generate_with_thinking_config(&normalized, tools, config)
            .await
    }

    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let normalized = self.normalize(messages);
        self.inner
            .generate_stream_with_tools(&normalized, tools)
            .await
    }

    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let normalized = self.normalize(messages);
        self.inner
            .generate_stream_with_thinking(&normalized, tools, thinking)
            .await
    }

    async fn generate_stream_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let normalized = self.normalize(messages);
        self.inner
            .generate_stream_with_thinking_config(&normalized, tools, config)
            .await
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

    fn provider_kind(&self) -> ProviderKind {
        self.inner.provider_kind()
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

    fn supports_thinking(&self) -> bool {
        self.inner.supports_thinking()
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        self.inner.thinking_levels()
    }

    fn resolve_thinking_config(&self, config: ThinkingConfig) -> ResolvedThinkingConfig {
        self.inner.resolve_thinking_config(config)
    }

    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        self.inner
            .generate_with_thinking(messages, tools, thinking)
            .await
    }

    async fn generate_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<LLMResponse> {
        self.inner
            .generate_with_thinking_config(messages, tools, config)
            .await
    }

    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.inner.generate_stream_with_tools(messages, tools).await
    }

    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.inner
            .generate_stream_with_thinking(messages, tools, thinking)
            .await
    }

    async fn generate_stream_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.inner
            .generate_stream_with_thinking_config(messages, tools, config)
            .await
    }
}
