use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_types::{AvaError, Message, Result, StreamChunk};
use futures::Stream;
use tokio_util::sync::CancellationToken;

fn completion_response(result: &str) -> String {
    format!(
        r#"{{"tool_calls":[{{"name":"attempt_completion","arguments":{{"result":"{result}"}}}}]}}"#
    )
}

#[tokio::test]
async fn agent_stack_new_initializes_components() {
    let dir = tempfile::tempdir().expect("tempdir");
    let stack = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(Arc::new(MockProvider::new("test", vec![]))),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let tools = stack.tools.read().await.list_tools();
    let names = tools.iter().map(|tool| tool.name.as_str()).collect::<Vec<_>>();
    assert!(names.contains(&"read"));
    assert!(names.contains(&"write"));
    assert!(names.contains(&"edit"));
    assert!(names.contains(&"bash"));
    assert!(names.contains(&"glob"));
    assert!(names.contains(&"grep"));
}

#[tokio::test]
async fn agent_stack_run_with_mock_provider_completes() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(MockProvider::new(
        "test-model",
        vec![completion_response("done")],
    ));
    let stack = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let result = stack
        .run("finish task", 5, None, CancellationToken::new(), Vec::new())
        .await
        .expect("run should succeed");

    assert!(result.success);
    assert!(result.turns >= 1);
}

#[tokio::test]
async fn agent_stack_run_honors_cancellation() {
    let dir = tempfile::tempdir().expect("tempdir");
    let provider = Arc::new(SlowProvider {
        model: "slow-model".to_string(),
        delay: Duration::from_millis(250),
    });
    let stack = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(provider),
        ..Default::default()
    })
    .await
    .expect("stack init should succeed");

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_millis(10)).await;
        cancel_clone.cancel();
    });

    let err = stack
        .run("slow task", 5, None, cancel, Vec::new())
        .await
        .expect_err("run should be cancelled");
    assert!(matches!(err, AvaError::Cancelled));
}

struct SlowProvider {
    model: String,
    delay: Duration,
}

#[async_trait]
impl LLMProvider for SlowProvider {
    async fn generate(&self, _messages: &[Message]) -> Result<String> {
        tokio::time::sleep(self.delay).await;
        Ok(completion_response("slow"))
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let out = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![StreamChunk::text(out)])))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model
    }
}
