use ava_commander::{Budget, Commander, Domain, Task, TaskType};

fn sample_budget() -> Budget {
    Budget {
        max_tokens: 10_000,
        max_turns: 12,
        max_cost_usd: 2.0,
    }
}

#[test]
fn delegation_routes_to_expected_domain() {
    let mut commander = Commander::new(sample_budget());

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
    let mut commander = Commander::new(sample_budget());
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
fn worker_spawning_creates_unique_id_and_lead_reference() {
    let mut commander = Commander::new(sample_budget());
    let worker = commander
        .delegate(Task {
            description: "simple task".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
        })
        .expect("delegation should succeed");

    assert_ne!(worker.id(), uuid::Uuid::nil());
    assert!(!worker.lead().is_empty());
}

#[test]
fn commander_has_all_seven_domain_leads() {
    let commander = Commander::new(sample_budget());
    assert_eq!(commander.leads().len(), 7);

    let domains: Vec<Domain> = commander
        .leads()
        .iter()
        .map(|lead| lead.domain().clone())
        .collect();
    assert!(domains.contains(&Domain::Frontend));
    assert!(domains.contains(&Domain::Backend));
    assert!(domains.contains(&Domain::QA));
    assert!(domains.contains(&Domain::Research));
    assert!(domains.contains(&Domain::Debug));
    assert!(domains.contains(&Domain::Fullstack));
    assert!(domains.contains(&Domain::DevOps));
}

#[tokio::test]
async fn coordinate_runs_workers_and_merges_session_messages() {
    let mut commander = Commander::new(sample_budget());
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

    let session = commander
        .coordinate(vec![worker_a, worker_b])
        .await
        .expect("coordinate should succeed");

    assert!(!session.messages.is_empty());
}
