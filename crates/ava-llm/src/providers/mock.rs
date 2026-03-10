use std::collections::VecDeque;
use std::pin::Pin;
use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, StreamChunk};
use futures::stream;
use futures::Stream;

use crate::provider::LLMProvider;
use crate::providers::common;

#[derive(Clone)]
pub struct MockProvider {
    model: String,
    responses: Arc<Mutex<VecDeque<String>>>,
}

impl MockProvider {
    pub fn new(model: impl Into<String>, responses: Vec<String>) -> Self {
        Self {
            model: model.into(),
            responses: Arc::new(Mutex::new(responses.into())),
        }
    }
}

#[async_trait]
impl LLMProvider for MockProvider {
    async fn generate(&self, _messages: &[Message]) -> Result<String> {
        let mut lock = self
            .responses
            .lock()
            .map_err(|_| AvaError::ToolError("mock provider mutex poisoned".to_string()))?;

        lock.pop_front()
            .ok_or_else(|| AvaError::NotFound("mock provider has no queued responses".to_string()))
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let output = self.generate(messages).await?;
        Ok(Box::pin(stream::iter(vec![StreamChunk::text(output)])))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        common::estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        (input_tokens as f64 + output_tokens as f64) * 0.000_000_5
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}
