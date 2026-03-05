use std::pin::Pin;

use async_trait::async_trait;
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_llm::providers::openai::OpenAIProvider;
use ava_llm::router::{ModelRouter, RoutingTaskType};
use ava_types::{Message, Role};
use futures::{Stream, StreamExt};
use serde_json::json;

struct StubProvider {
    model: String,
}

#[async_trait]
impl LLMProvider for StubProvider {
    async fn generate(&self, _messages: &[Message]) -> ava_types::Result<String> {
        Ok("ok".to_string())
    }

    async fn generate_stream(
        &self,
        _messages: &[Message],
    ) -> ava_types::Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        Ok(Box::pin(futures::stream::iter(vec!["ok".to_string()])))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        (input.len() / 4).max(1)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        (input_tokens + output_tokens) as f64 / 1_000_000.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}

#[tokio::test]
async fn mock_provider_generate_and_stream() {
    let provider = MockProvider::new("mock", vec!["first".to_string(), "second".to_string()]);

    let generated = provider.generate(&[]).await.expect("generate should work");
    assert_eq!(generated, "first");

    let stream = provider
        .generate_stream(&[])
        .await
        .expect("stream should work");
    let parts: Vec<String> = stream.collect().await;
    assert_eq!(parts, vec!["second"]);
}

#[test]
fn openai_request_body_serialization() {
    let provider = OpenAIProvider::new("key", "gpt-4o-mini");
    let body = provider.build_request_body(&[Message::new(Role::User, "hello")], false);

    assert_eq!(body["model"], "gpt-4o-mini");
    assert_eq!(body["stream"], false);
    assert!(body["messages"].is_array());
}

#[test]
fn openai_bad_response_returns_error() {
    let payload = json!({"choices": []});
    let error = OpenAIProvider::parse_response_payload(&payload)
        .expect_err("bad payload should fail parsing");

    assert!(error.to_string().contains("missing OpenAI completion content"));
}

#[test]
fn token_and_cost_estimation_are_non_zero() {
    let provider = OpenAIProvider::new("key", "gpt-4o-mini");
    let tokens = provider.estimate_tokens("estimate me");
    let cost = provider.estimate_cost(1500, 800);

    assert!(tokens > 0);
    assert!(cost > 0.0);
}

#[test]
fn router_routes_to_expected_tier() {
    let mut router = ModelRouter::new("mid");
    router.register(
        "strongest",
        Box::new(StubProvider {
            model: "strong".to_string(),
        }),
    );
    router.register(
        "mid",
        Box::new(StubProvider {
            model: "mid".to_string(),
        }),
    );
    router.register(
        "cheap",
        Box::new(StubProvider {
            model: "cheap".to_string(),
        }),
    );

    let planning = router
        .route(RoutingTaskType::Planning)
        .expect("planning route should resolve");
    assert_eq!(planning.model_name(), "strong");

    let codegen = router
        .route(RoutingTaskType::CodeGeneration)
        .expect("code route should resolve");
    assert_eq!(codegen.model_name(), "mid");

    let simple = router
        .route(RoutingTaskType::Simple)
        .expect("simple route should resolve");
    assert_eq!(simple.model_name(), "cheap");
}

#[test]
fn router_falls_back_to_default_tier_when_preferred_missing() {
    let mut router = ModelRouter::new("mid");
    router.register(
        "mid",
        Box::new(StubProvider {
            model: "mid".to_string(),
        }),
    );

    let provider = router
        .route(RoutingTaskType::Simple)
        .expect("simple route should fall back to default");

    assert_eq!(provider.model_name(), "mid");
}

#[test]
fn router_returns_error_when_empty() {
    let router = ModelRouter::new("mid");
    match router.route(RoutingTaskType::Planning) {
        Err(error) => assert!(error.to_string().contains("no provider registered")),
        Ok(_) => panic!("empty router should fail"),
    }
}
