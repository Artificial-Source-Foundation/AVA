use std::collections::HashMap;
use std::pin::Pin;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_platform::StandardPlatform;
use ava_praxis::{Budget, Director, DirectorConfig, Domain, PraxisEvent, Task, TaskType};
use ava_types::{Message, Result, Role, StreamChunk};
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

fn director_with_default(provider: Arc<dyn LLMProvider>) -> Director {
    Director::new(DirectorConfig {
        budget: sample_budget(),
        default_provider: provider,
        domain_providers: HashMap::new(),
        platform: None,
        scout_provider: None,
        board_providers: vec![],
    })
}

fn director_with_platform(provider: Arc<dyn LLMProvider>) -> Director {
    Director::new(DirectorConfig {
        budget: sample_budget(),
        default_provider: provider,
        domain_providers: HashMap::new(),
        platform: Some(Arc::new(StandardPlatform)),
        scout_provider: None,
        board_providers: vec![],
    })
}

// --- Story 1: Domain Routing ---

#[test]
fn delegation_routes_to_expected_domain() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("ok")],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);

    let worker = director
        .delegate(Task {
            description: "implement API endpoint".to_string(),
            task_type: TaskType::CodeGeneration,
            files: vec!["src/api.rs".to_string()],
        })
        .expect("delegation should produce worker");

    let lead = director
        .leads()
        .iter()
        .find(|lead| lead.name() == worker.lead())
        .expect("lead should exist");

    assert_eq!(lead.domain(), &Domain::Backend);
}

#[test]
fn domain_routing_covers_all_task_types() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![
            completion_response("1"),
            completion_response("2"),
            completion_response("3"),
            completion_response("4"),
            completion_response("5"),
            completion_response("6"),
            completion_response("7"),
        ],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);

    let cases = vec![
        (TaskType::CodeGeneration, Domain::Backend),
        (TaskType::Testing, Domain::QA),
        (TaskType::Review, Domain::QA),
        (TaskType::Research, Domain::Research),
        (TaskType::Debug, Domain::Debug),
        (TaskType::Planning, Domain::Fullstack),
        (TaskType::Simple, Domain::Fullstack),
    ];

    for (task_type, expected_domain) in cases {
        let worker = director
            .delegate(Task {
                description: format!("task for {:?}", task_type),
                task_type,
                files: vec![],
            })
            .expect("delegation should succeed");

        let lead = director
            .leads()
            .iter()
            .find(|lead| lead.name() == worker.lead())
            .expect("lead should exist");

        assert_eq!(
            lead.domain(),
            &expected_domain,
            "task type {:?} should route to {:?}",
            worker.task().task_type,
            expected_domain
        );
    }
}

// --- Story 1: Budget Enforcement ---

#[test]
fn budget_allocation_halves_top_level_budget() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("ok")],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);
    let worker = director
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

// --- Story 1: Provider routing ---

#[test]
fn worker_spawning_uses_domain_provider_model_name() {
    let default_provider =
        Arc::new(MockProvider::new("default-model", vec![])) as Arc<dyn LLMProvider>;
    let backend_provider =
        Arc::new(MockProvider::new("backend-model", vec![])) as Arc<dyn LLMProvider>;

    let mut overrides: HashMap<Domain, Arc<dyn LLMProvider>> = HashMap::new();
    overrides.insert(Domain::Backend, backend_provider);

    let mut director = Director::new(DirectorConfig {
        budget: sample_budget(),
        default_provider,
        domain_providers: overrides,
        platform: None,
        scout_provider: None,
        board_providers: vec![],
    });

    let worker = director
        .delegate(Task {
            description: "build endpoint".to_string(),
            task_type: TaskType::CodeGeneration,
            files: vec![],
        })
        .expect("delegation should succeed");

    assert_eq!(worker.model_name(), "backend-model");
}

// --- Story 1: Single worker delegation (e2e) ---

#[tokio::test]
async fn single_worker_completes_successfully() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("done")],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);

    let worker = director
        .delegate(Task {
            description: "simple task".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("should spawn worker");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    let session = director
        .coordinate(vec![worker], cancel, tx)
        .await
        .expect("coordinate should succeed");

    assert!(!session.messages.is_empty());

    let events: Vec<PraxisEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    assert!(events
        .iter()
        .any(|e| matches!(e, PraxisEvent::WorkerStarted { .. })));
    assert!(events
        .iter()
        .any(|e| matches!(e, PraxisEvent::WorkerCompleted { success: true, .. })));
    assert!(events.iter().any(|e| matches!(
        e,
        PraxisEvent::AllComplete {
            total_workers: 1,
            succeeded: 1,
            failed: 0
        }
    )));
}

// --- Story 1: Multi-worker coordination ---

#[tokio::test]
async fn coordinate_runs_workers_and_merges_session_messages() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("a"), completion_response("b")],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);
    let worker_a = director
        .delegate(Task {
            description: "task a".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker a should spawn");
    let worker_b = director
        .delegate(Task {
            description: "task b".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker b should spawn");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    let session = director
        .coordinate(vec![worker_a, worker_b], cancel, tx)
        .await
        .expect("coordinate should succeed");

    assert!(!session.messages.is_empty());

    // Check that messages are grouped by worker (system header messages)
    let system_msgs: Vec<&Message> = session
        .messages
        .iter()
        .filter(|m| m.role == Role::System && m.content.starts_with("[worker-"))
        .collect();
    assert!(
        system_msgs.len() >= 2,
        "should have worker attribution headers"
    );

    // Check for summary message
    let summary = session
        .messages
        .iter()
        .find(|m| m.content.starts_with("Completed"))
        .expect("should have summary message");
    assert!(summary.content.contains("workers successfully"));

    let events: Vec<PraxisEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    assert!(events
        .iter()
        .any(|event| matches!(event, PraxisEvent::AllComplete { .. })));
    assert!(events
        .iter()
        .any(|event| matches!(event, PraxisEvent::Summary { .. })));
}

// --- Story 1: Cancellation ---

#[tokio::test]
async fn cancellation_token_stops_workers() {
    let provider = Arc::new(SlowProvider {
        delay: Duration::from_millis(200),
        model: "slow-model".to_string(),
    }) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);

    let worker = director
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
    let session = director
        .coordinate(vec![worker], cancel, tx)
        .await
        .expect("coordinate returns partial success session");

    // Failed workers produce error messages in session
    let has_error = session.messages.iter().any(|m| m.content.contains("ERROR"));
    assert!(has_error || session.messages.is_empty());

    let events: Vec<PraxisEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    assert!(events
        .iter()
        .any(|event| matches!(event, PraxisEvent::WorkerFailed { .. })));
}

// --- Story 1: Worker failure isolation ---

#[tokio::test]
async fn one_worker_failure_isolated_from_successful_worker() {
    let default_provider = Arc::new(MockProvider::new("good", vec![completion_response("ok")]))
        as Arc<dyn LLMProvider>;
    let failing_backend = Arc::new(MockProvider::new("bad", vec![])) as Arc<dyn LLMProvider>;
    let mut overrides: HashMap<Domain, Arc<dyn LLMProvider>> = HashMap::new();
    overrides.insert(Domain::Backend, failing_backend);

    let mut director = Director::new(DirectorConfig {
        budget: sample_budget(),
        default_provider,
        domain_providers: overrides,
        platform: None,
        scout_provider: None,
        board_providers: vec![],
    });

    let good_worker = director
        .delegate(Task {
            description: "simple success".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("good worker");
    let bad_worker = director
        .delegate(Task {
            description: "backend fail".to_string(),
            task_type: TaskType::CodeGeneration,
            files: vec![],
        })
        .expect("bad worker");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();
    let session = director
        .coordinate(vec![good_worker, bad_worker], cancel, tx)
        .await
        .expect("coordinate should still succeed");

    assert!(!session.messages.is_empty());

    // Failed worker error is preserved in session
    let has_error_msg = session
        .messages
        .iter()
        .any(|m| m.role == Role::System && m.content.contains("ERROR"));
    assert!(has_error_msg, "failed worker error should be in session");

    let events: Vec<PraxisEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    assert!(events.iter().any(|event| {
        matches!(
            event,
            PraxisEvent::AllComplete {
                total_workers: 2,
                succeeded: 1,
                failed: 1
            }
        )
    }));
}

// --- Story 1: Event stream order ---

#[tokio::test]
async fn event_stream_fires_in_order() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("ok")],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);

    let worker = director
        .delegate(Task {
            description: "ordered task".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker should spawn");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    director
        .coordinate(vec![worker], cancel, tx)
        .await
        .expect("coordinate should succeed");

    let events: Vec<PraxisEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();

    // WorkerStarted must come before WorkerCompleted/WorkerFailed
    let started_idx = events
        .iter()
        .position(|e| matches!(e, PraxisEvent::WorkerStarted { .. }))
        .expect("should have WorkerStarted");
    let completed_idx = events
        .iter()
        .position(|e| matches!(e, PraxisEvent::WorkerCompleted { .. }))
        .expect("should have WorkerCompleted");
    let all_complete_idx = events
        .iter()
        .position(|e| matches!(e, PraxisEvent::AllComplete { .. }))
        .expect("should have AllComplete");
    let summary_idx = events
        .iter()
        .position(|e| matches!(e, PraxisEvent::Summary { .. }))
        .expect("should have Summary");

    assert!(started_idx < completed_idx, "Started before Completed");
    assert!(
        completed_idx < all_complete_idx,
        "Completed before AllComplete"
    );
    assert!(all_complete_idx < summary_idx, "AllComplete before Summary");
}

// --- Story 2: Workers have tools when platform is provided ---

#[tokio::test]
async fn worker_with_platform_has_core_tools() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("ok")],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_platform(provider);

    let worker = director
        .delegate(Task {
            description: "tooled task".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker should spawn");

    // Run it — the agent loop should execute with tools available
    let cancel = CancellationToken::new();
    let (tx, _rx) = mpsc::unbounded_channel();

    let session = director
        .coordinate(vec![worker], cancel, tx)
        .await
        .expect("coordinate should succeed");

    assert!(!session.messages.is_empty());
}

// --- Story 4: Summary event has total_turns ---

#[tokio::test]
async fn summary_event_includes_total_turns() {
    let provider = Arc::new(MockProvider::new(
        "default",
        vec![completion_response("ok")],
    )) as Arc<dyn LLMProvider>;
    let mut director = director_with_default(provider);

    let worker = director
        .delegate(Task {
            description: "count turns".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("worker should spawn");

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    director
        .coordinate(vec![worker], cancel, tx)
        .await
        .expect("coordinate should succeed");

    let events: Vec<PraxisEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();
    let summary = events
        .iter()
        .find(|e| matches!(e, PraxisEvent::Summary { .. }))
        .expect("should have Summary event");

    if let PraxisEvent::Summary {
        total_workers,
        succeeded,
        failed,
        total_turns,
    } = summary
    {
        assert_eq!(*total_workers, 1);
        assert_eq!(*succeeded, 1);
        assert_eq!(*failed, 0);
        assert!(*total_turns > 0, "total_turns should be > 0");
    }
}

// --- Helper: SlowProvider for cancellation test ---

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
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let out = self.generate(messages).await?;
        Ok(Box::pin(futures::stream::iter(vec![StreamChunk::text(
            out,
        )])))
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
