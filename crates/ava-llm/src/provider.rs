use std::pin::Pin;

use async_trait::async_trait;
use ava_types::{Message, Result};
use futures::Stream;

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
}
