use std::collections::HashMap;
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_commander::{
    Budget, Commander, CommanderConfig, CommanderEvent, Domain, Task, TaskType,
};
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_types::{Message, Result};
use futures::Stream;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

fn sample_budget() -> Budget {
    Budget {
        max_tokens: 10_000,
        max_turns: 12,
        max_cost_usd: 2.0,
    }
}

fn completion_response(result: &str) -> String {
    format!(
        r#"{{"tool_calls":[{{"name":"attempt_completion","arguments":{{"result":"{result}"}}}}]}}"#
    )
}

fn commander_with_default(provider: Arc<dyn LLMProvider>) -> Commander {
    Commander::new(CommanderConfig {
        budget: sample_budget(),
        default_provider: provider,
        domain_providers: HashMap::new(),
    })
}

#[test]
fn delegation_routes_to_expected_domain() {
    let provider = Arc::new(MockProvider::new("default", vec![completion_response("ok")]))
        as Arc<dyn LLMProvider>;
    let mut commander = commander_with_default(provider);

    let worker = commander
        .delegate(Task {
            description: "implement API endpoint".to_string(),
            task_type: TaskType::CodeGeneration,
            files: vec!["src/api.rs".to_string()],
        })
        .expect("delegation should produce worker");

    let lead = commander
        .leads()
        .iter()
        .find(|lead| lead.name() == worker.lead())
        .expect("lead should exist");

    assert_eq!(lead.domain(), &Domain::Backend);
}

#[test]
fn budget_allocation_halves_top_level_budget() {
    let provider = Arc::new(MockProvider::new("default", vec![completion_response("ok")]))
        as Arc<dyn LLMProvider>;
    let mut commander = commander_with_default(provider);
    let worker = commander
        .delegate(Task {
            description: "test suite".to_string(),
            task_type: TaskType::Testing,
            files: vec![],
        })
        .expect("delegation should succeed");

    assert_eq!(worker.budget().max_tokens, 5_000);
    assert_eq!(worker.budget().max_turns, 6);
    assert!((worker.budget().max_cost_usd - 1.0).abs() < f64::EPSILON);
}

#[test]
fn worker_spawning_uses_domain_provider_model_name() {
    let default_provider = Arc::new(MockProvider::new("default-model", vec![]))
        as Arc<dyn LLMProvider>;
    let backend_provider = Arc::new(MockProvider::new("backend-model", vec![]))
        as Arc<dyn LLMProvider>;

    let mut overrides: HashMap<Domain, Arc<dyn LLMProvider>> = HashMap::new();
    overrides.insert(Domain::Backend, backend_provider);

    let mut commander = Commander::new(CommanderConfig {
        budget: sample_budget(),
        default_provider,
        domain_providers: overrides,
    });

    let worker = commander
        .delegate(Task {
            description: "build endpoint".to_string(),
            task_type: TaskType::CodeGeneration,
            files: vec![],
        })
        .expect("delegation should succeed");

    assert_eq!(worker.model_name(), "backend-model");
}

#[tokio::test]
async fn coordinate_runs_workers_and_merges_session_messages() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("a"), completion_response("b")],
    )) as Arc<dyn LLMProvider>;
    let mut commander = commander_with_default(provider);
    let worker_a = commander
        .delegate(Task {
            description: "task a".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker a should spawn");
    let worker_b = commander
        .delegate(Task {
            description: "task b".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker b should spawn");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    let session = commander
        .coordinate(vec![worker_a, worker_b], cancel, tx)
        .await
        .expect("coordinate should succeed");

    assert!(!session.messages.is_empty());
    let events: Vec<CommanderEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    assert!(
        events
            .iter()
            .any(|event| matches!(event, CommanderEvent::AllComplete { .. }))
    );
}

#[tokio::test]
async fn cancellation_token_stops_workers() {
    let provider = Arc::new(SlowProvider {
        delay: Duration::from_millis(200),
        model: "slow-model".to_string(),
    }) as Arc<dyn LLMProvider>;
    let mut commander = commander_with_default(provider);

    let worker = commander
        .delegate(Task {
            description: "slow task".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker should spawn");

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_millis(20)).await;
        cancel_clone.cancel();
    });

    let (tx, mut rx) = mpsc::unbounded_channel();
    let session = commander
        .coordinate(vec![worker], cancel, tx)
        .await
        .expect("coordinate returns partial success session");

    assert!(session.messages.is_empty());
    let events: Vec<CommanderEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    assert!(
        events
            .iter()
            .any(|event| matches!(event, CommanderEvent::WorkerFailed { .. }))
    );
}

#[tokio::test]
async fn one_worker_failure_isolated_from_successful_worker() {
    let default_provider = Arc::new(MockProvider::new("good", vec![completion_response("ok")]))
        as Arc<dyn LLMProvider>;
    let failing_backend = Arc::new(MockProvider::new("bad", vec![])) as Arc<dyn LLMProvider>;
    let mut overrides: HashMap<Domain, Arc<dyn LLMProvider>> = HashMap::new();
    overrides.insert(Domain::Backend, failing_backend);

    let mut commander = Commander::new(CommanderConfig {
        budget: sample_budget(),
        default_provider,
        domain_providers: overrides,
    });

    let good_worker = commander
        .delegate(Task {
            description: "simple success".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("good worker");
    let bad_worker = commander
        .delegate(Task {
            description: "backend fail".to_string(),
            task_type: TaskType::CodeGeneration,
            files: vec![],
        })
        .expect("bad worker");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();
    let session = commander
        .coordinate(vec![good_worker, bad_worker], cancel, tx)
        .await
        .expect("coordinate should still succeed");

    assert!(!session.messages.is_empty());
    let events: Vec<CommanderEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    assert!(events.iter().any(|event| {
        matches!(
            event,
            CommanderEvent::AllComplete {
                total_workers: 2,
                succeeded: 1,
                failed: 1
            }
        )
    }));
}

struct SlowProvider {
    delay: Duration,
    model: String,
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
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        let out = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![out])))
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
