use std::sync::Arc;
use std::time::Duration;

use ava_commander::workflow::{Workflow, WorkflowExecutor};
use ava_commander::{Budget, CommanderEvent};
use ava_llm::provider::LLMProvider;
use ava_llm::providers::mock::MockProvider;
use ava_platform::StandardPlatform;
use ava_types::Role;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

fn completion_response(result: &str) -> String {
    format!(
        r#"{{"tool_calls":[{{"name":"attempt_completion","arguments":{{"result":"{result}"}}}}]}}"#
    )
}

fn sample_budget() -> Budget {
    Budget {
        max_tokens: 10_000,
        max_turns: 15,
        max_cost_usd: 5.0,
    }
}

#[tokio::test]
async fn workflow_runs_all_phases_sequentially() {
    // 3 phases, each gets one response to complete
    let provider = Arc::new(MockProvider::new(
        "mock",
        vec![
            completion_response("plan: step 1, step 2"),
            completion_response("implemented changes"),
            completion_response("LGTM - looks good"),
        ],
    )) as Arc<dyn LLMProvider>;

    let workflow = Workflow::plan_code_review();
    let platform = Arc::new(StandardPlatform);
    let executor = WorkflowExecutor::new(workflow, sample_budget(), provider, platform);

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    let session = executor
        .execute("add a health check", cancel, tx)
        .await
        .expect("workflow should complete");

    assert!(!session.messages.is_empty());

    // Collect events
    let events: Vec<CommanderEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();

    // Should have 3 PhaseStarted events
    let phase_started: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, CommanderEvent::PhaseStarted { .. }))
        .collect();
    assert_eq!(phase_started.len(), 3, "should start 3 phases");

    // Should have 3 PhaseCompleted events
    let phase_completed: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, CommanderEvent::PhaseCompleted { .. }))
        .collect();
    assert_eq!(phase_completed.len(), 3, "should complete 3 phases");

    // Should have WorkflowComplete
    let wf_complete = events
        .iter()
        .find(|e| matches!(e, CommanderEvent::WorkflowComplete { .. }))
        .expect("should have WorkflowComplete event");

    if let CommanderEvent::WorkflowComplete {
        phases_completed,
        total_phases,
        iterations,
        ..
    } = wf_complete
    {
        assert_eq!(*phases_completed, 3);
        assert_eq!(*total_phases, 3);
        assert_eq!(*iterations, 1);
    }
}

#[tokio::test]
async fn events_fire_in_correct_order() {
    let provider = Arc::new(MockProvider::new(
        "mock",
        vec![
            completion_response("plan"),
            completion_response("code"),
            completion_response("LGTM"),
        ],
    )) as Arc<dyn LLMProvider>;

    let workflow = Workflow::plan_code_review();
    let platform = Arc::new(StandardPlatform);
    let executor = WorkflowExecutor::new(workflow, sample_budget(), provider, platform);

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    executor
        .execute("test task", cancel, tx)
        .await
        .expect("should complete");

    let events: Vec<CommanderEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();

    // IterationStarted should come before any PhaseStarted
    let iter_idx = events
        .iter()
        .position(|e| matches!(e, CommanderEvent::IterationStarted { .. }))
        .expect("should have IterationStarted");
    let first_phase_idx = events
        .iter()
        .position(|e| matches!(e, CommanderEvent::PhaseStarted { .. }))
        .expect("should have PhaseStarted");
    let wf_complete_idx = events
        .iter()
        .position(|e| matches!(e, CommanderEvent::WorkflowComplete { .. }))
        .expect("should have WorkflowComplete");

    assert!(iter_idx < first_phase_idx, "IterationStarted before PhaseStarted");
    assert!(first_phase_idx < wf_complete_idx, "PhaseStarted before WorkflowComplete");
}

#[tokio::test]
async fn feedback_loop_triggers_on_revision_request() {
    // Iteration 1: plan, code, reviewer says "fix the error handling"
    // Iteration 2: plan, code, reviewer says "LGTM"
    let provider = Arc::new(MockProvider::new(
        "mock",
        vec![
            // Iteration 1
            completion_response("plan: add endpoint"),
            completion_response("added the endpoint"),
            completion_response("fix the error handling - there is an issue"),
            // Iteration 2
            completion_response("revised plan"),
            completion_response("fixed error handling"),
            completion_response("LGTM - looks good"),
        ],
    )) as Arc<dyn LLMProvider>;

    let workflow = Workflow::plan_code_review();
    let platform = Arc::new(StandardPlatform);
    let executor = WorkflowExecutor::new(workflow, sample_budget(), provider, platform);

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    executor
        .execute("add endpoint", cancel, tx)
        .await
        .expect("should complete");

    let events: Vec<CommanderEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();

    // Should have 2 IterationStarted events
    let iterations: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, CommanderEvent::IterationStarted { .. }))
        .collect();
    assert_eq!(iterations.len(), 2, "should have 2 iterations");

    // Should have 6 PhaseStarted (3 per iteration)
    let phases: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, CommanderEvent::PhaseStarted { .. }))
        .collect();
    assert_eq!(phases.len(), 6, "should have 6 phase starts (3 x 2 iterations)");

    // WorkflowComplete should show 2 iterations
    if let Some(CommanderEvent::WorkflowComplete { iterations, .. }) = events
        .iter()
        .find(|e| matches!(e, CommanderEvent::WorkflowComplete { .. }))
    {
        assert_eq!(*iterations, 2);
    }
}

#[tokio::test]
async fn feedback_loop_stops_on_lgtm() {
    let provider = Arc::new(MockProvider::new(
        "mock",
        vec![
            completion_response("code done"),
            completion_response("LGTM - approved"),
        ],
    )) as Arc<dyn LLMProvider>;

    let workflow = Workflow::code_review();
    let platform = Arc::new(StandardPlatform);
    let executor = WorkflowExecutor::new(workflow, sample_budget(), provider, platform);

    let cancel = CancellationToken::new();
    let (tx, mut rx) = mpsc::unbounded_channel();

    executor
        .execute("simple task", cancel, tx)
        .await
        .expect("should complete");

    let events: Vec<CommanderEvent> = std::iter::from_fn(|| rx.try_recv().ok()).collect();

    // Only 1 iteration (LGTM stops the loop)
    let iterations: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, CommanderEvent::IterationStarted { .. }))
        .collect();
    assert_eq!(iterations.len(), 1, "LGTM should stop after 1 iteration");
}

#[tokio::test]
async fn cancellation_stops_mid_workflow() {
    // SlowProvider would hang, but cancellation stops it
    let provider = Arc::new(MockProvider::new("mock", vec![])) as Arc<dyn LLMProvider>;

    let workflow = Workflow::plan_code_review();
    let platform = Arc::new(StandardPlatform);
    let executor = WorkflowExecutor::new(workflow, sample_budget(), provider, platform);

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::time::sleep(Duration::from_millis(10)).await;
        cancel_clone.cancel();
    });

    let (tx, _rx) = mpsc::unbounded_channel();

    let result = executor.execute("cancelled task", cancel, tx).await;
    assert!(result.is_err(), "cancelled workflow should error");
}

#[tokio::test]
async fn combined_session_contains_phase_markers() {
    let provider = Arc::new(MockProvider::new(
        "mock",
        vec![
            completion_response("planned"),
            completion_response("coded"),
        ],
    )) as Arc<dyn LLMProvider>;

    let workflow = Workflow::plan_code();
    let platform = Arc::new(StandardPlatform);
    let executor = WorkflowExecutor::new(workflow, sample_budget(), provider, platform);

    let cancel = CancellationToken::new();
    let (tx, _rx) = mpsc::unbounded_channel();

    let session = executor
        .execute("build feature", cancel, tx)
        .await
        .expect("should complete");

    let phase_markers: Vec<_> = session
        .messages
        .iter()
        .filter(|m| m.role == Role::System && m.content.starts_with("[phase-"))
        .collect();

    assert_eq!(phase_markers.len(), 2, "should have 2 phase markers");
    assert!(phase_markers[0].content.contains("Plan"));
    assert!(phase_markers[1].content.contains("Code"));
}
