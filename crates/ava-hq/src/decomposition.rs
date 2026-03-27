use crate::{Domain, PlannerConfig, Task, TaskType};
use ava_llm::provider::LLMProvider;
use ava_types::{AvaError, Message, Result, Role};
use std::collections::BTreeSet;

#[derive(Debug, Clone)]
pub struct DecompositionPlan {
    pub strategy: &'static str,
    pub tasks: Vec<Task>,
}

pub fn decompose_task(task: &Task) -> Option<DecompositionPlan> {
    let domains = detect_domains(task);
    if domains.len() < 2 {
        return None;
    }

    let tasks = domains
        .into_iter()
        .take(3)
        .map(|domain| Task {
            description: domain_prompt(&domain, &task.description, &task.files),
            task_type: domain_task_type(&domain),
            files: task.files.clone(),
            history: task.history.clone(),
            preferred_domain: Some(domain),
        })
        .collect::<Vec<_>>();

    if tasks.len() < 2 {
        return None;
    }

    Some(DecompositionPlan {
        strategy: "heuristic-domain-split",
        tasks,
    })
}

pub fn should_attempt_planner(task: &Task) -> bool {
    detect_domains(task).len() >= 2 || task.description.split_whitespace().count() > 12
}

pub async fn decompose_task_with_planner(
    provider: std::sync::Arc<dyn LLMProvider>,
    task: &Task,
    planner: &PlannerConfig,
) -> Result<Option<DecompositionPlan>> {
    let messages = build_planner_messages(task, planner);
    let raw = provider.generate(&messages).await?;
    parse_planner_response(&raw, task, planner)
}

fn build_planner_messages(task: &Task, planner: &PlannerConfig) -> Vec<Message> {
    let mut messages = vec![Message::new(
        Role::System,
        format!(
            "You are the HQ task planner. Return JSON only. Split the goal into 2 to {} independent subtasks only when that is clearly useful. Each subtask must use exactly one domain from [\"frontend\",\"backend\",\"qa\",\"research\",\"debug\",\"devops\",\"fullstack\"]. If the task should stay single-worker, return {{\"subtasks\":[]}}. Schema: {{\"subtasks\":[{{\"domain\":\"backend\",\"description\":\"...\"}}]}}",
            planner.max_subtasks
        ),
    )];

    let heuristic_domains = detect_domains(task)
        .into_iter()
        .map(|domain| format!("{domain:?}").to_lowercase())
        .collect::<Vec<_>>()
        .join(", ");

    let recent_history = task
        .history
        .iter()
        .rev()
        .take(planner.max_history_messages)
        .collect::<Vec<_>>();

    let mut prompt = format!(
        "Goal: {}\nTask type: {:?}\nFiles: {:?}\nCandidate domains: [{}]\n",
        task.description, task.task_type, task.files, heuristic_domains
    );
    if !recent_history.is_empty() {
        prompt.push_str("Recent constraints:\n");
        for message in recent_history.iter().rev() {
            let role = match message.role {
                Role::User => "User",
                Role::Assistant => "Assistant",
                Role::Tool => "Tool",
                Role::System => "System",
            };
            let content = message.content.chars().take(300).collect::<String>();
            prompt.push_str(&format!("- {role}: {content}\n"));
        }
    }

    messages.push(Message::new(Role::User, prompt));
    messages
}

fn parse_planner_response(
    raw: &str,
    task: &Task,
    planner: &PlannerConfig,
) -> Result<Option<DecompositionPlan>> {
    #[derive(serde::Deserialize)]
    struct PlannerResponse {
        subtasks: Vec<PlannerSubtask>,
    }

    #[derive(serde::Deserialize)]
    struct PlannerSubtask {
        domain: String,
        description: String,
    }

    let trimmed = raw.trim();
    let json = if trimmed.starts_with("```") {
        trimmed
            .trim_start_matches("```json")
            .trim_start_matches("```")
            .trim_end_matches("```")
            .trim()
    } else {
        trimmed
    };

    let parsed: PlannerResponse = serde_json::from_str(json)
        .map_err(|err| AvaError::ToolError(format!("invalid planner JSON: {err}")))?;

    if parsed.subtasks.len() < 2 || parsed.subtasks.len() > planner.max_subtasks {
        return Ok(None);
    }

    let mut seen = BTreeSet::new();
    let mut tasks = Vec::new();

    for subtask in parsed.subtasks {
        let domain = match parse_domain(&subtask.domain) {
            Some(domain) => domain,
            None => return Ok(None),
        };
        let description = subtask.description.trim();
        if description.is_empty() || description.len() > 300 {
            return Ok(None);
        }
        let key = format!("{:?}:{}", domain, description.to_lowercase());
        if !seen.insert(key) {
            return Ok(None);
        }
        tasks.push(Task {
            description: description.to_string(),
            task_type: domain_task_type(&domain),
            files: task.files.clone(),
            history: task.history.clone(),
            preferred_domain: Some(domain),
        });
    }

    Ok(Some(DecompositionPlan {
        strategy: "llm-planned-domain-split",
        tasks,
    }))
}

fn parse_domain(raw: &str) -> Option<Domain> {
    match raw.trim().to_ascii_lowercase().as_str() {
        "frontend" => Some(Domain::Frontend),
        "backend" => Some(Domain::Backend),
        "qa" => Some(Domain::QA),
        "research" => Some(Domain::Research),
        "debug" => Some(Domain::Debug),
        "devops" => Some(Domain::DevOps),
        "fullstack" => Some(Domain::Fullstack),
        _ => None,
    }
}

fn detect_domains(task: &Task) -> Vec<Domain> {
    let mut domains = BTreeSet::new();
    let haystack = format!(
        "{} {}",
        task.description.to_lowercase(),
        task.files.join(" ").to_lowercase()
    );

    if contains_any(
        &haystack,
        &[
            "frontend", "ui", "ux", "sidebar", "widget", "tui", "layout", "theme",
        ],
    ) {
        domains.insert(domain_rank(&Domain::Frontend));
    }
    if contains_any(
        &haystack,
        &[
            "backend", "api", "server", "auth", "database", "db", "query", "index", "storage",
        ],
    ) {
        domains.insert(domain_rank(&Domain::Backend));
    }
    if contains_any(
        &haystack,
        &["test", "tests", "qa", "coverage", "verify", "validation"],
    ) {
        domains.insert(domain_rank(&Domain::QA));
    }
    if contains_any(
        &haystack,
        &[
            "research",
            "investigate",
            "compare",
            "analyze",
            "analysis",
            "spike",
        ],
    ) {
        domains.insert(domain_rank(&Domain::Research));
    }
    if contains_any(
        &haystack,
        &["debug", "bug", "fix", "failure", "panic", "trace"],
    ) {
        domains.insert(domain_rank(&Domain::Debug));
    }
    if contains_any(
        &haystack,
        &[
            "deploy",
            "ci",
            "docker",
            "workflow",
            "release",
            "infra",
            "infrastructure",
        ],
    ) {
        domains.insert(domain_rank(&Domain::DevOps));
    }

    domains.into_iter().map(rank_to_domain).collect()
}

fn contains_any(haystack: &str, needles: &[&str]) -> bool {
    needles.iter().any(|needle| haystack.contains(needle))
}

pub fn infer_file_hints(goal: &str) -> Vec<String> {
    goal.split_whitespace()
        .map(|token| {
            token.trim_matches(|ch: char| {
                matches!(ch, ',' | '.' | ';' | ':' | '(' | ')' | '"' | '\'')
            })
        })
        .filter(|token| {
            token.contains('/')
                || token.ends_with(".rs")
                || token.ends_with(".ts")
                || token.ends_with(".tsx")
                || token.ends_with(".js")
                || token.ends_with(".md")
        })
        .map(str::to_string)
        .collect::<BTreeSet<_>>()
        .into_iter()
        .take(5)
        .collect()
}

fn domain_prompt(domain: &Domain, goal: &str, files: &[String]) -> String {
    let file_hint = if files.is_empty() {
        String::new()
    } else {
        format!(" Relevant paths: {}.", files.join(", "))
    };

    match domain {
        Domain::Frontend => format!("Focus on the frontend and UI aspects of: {goal}.{file_hint}"),
        Domain::Backend => {
            format!("Focus on the backend, API, and data aspects of: {goal}.{file_hint}")
        }
        Domain::QA => {
            format!("Focus on tests, validation, and verification for: {goal}.{file_hint}")
        }
        Domain::Research => {
            format!("Research and analyze the best approach for: {goal}.{file_hint}")
        }
        Domain::Debug => format!("Debug and identify the root cause for: {goal}.{file_hint}"),
        Domain::DevOps => {
            format!("Focus on CI, deployment, and infrastructure aspects of: {goal}.{file_hint}")
        }
        Domain::Fullstack => {
            if file_hint.is_empty() {
                goal.to_string()
            } else {
                format!("{goal}.{file_hint}")
            }
        }
    }
}

fn domain_task_type(domain: &Domain) -> TaskType {
    match domain {
        Domain::Frontend | Domain::Backend | Domain::DevOps => TaskType::CodeGeneration,
        Domain::QA => TaskType::Testing,
        Domain::Research => TaskType::Research,
        Domain::Debug => TaskType::Debug,
        Domain::Fullstack => TaskType::Simple,
    }
}

fn domain_rank(domain: &Domain) -> u8 {
    match domain {
        Domain::Frontend => 0,
        Domain::Backend => 1,
        Domain::QA => 2,
        Domain::Research => 3,
        Domain::Debug => 4,
        Domain::DevOps => 5,
        Domain::Fullstack => 6,
    }
}

fn rank_to_domain(rank: u8) -> Domain {
    match rank {
        0 => Domain::Frontend,
        1 => Domain::Backend,
        2 => Domain::QA,
        3 => Domain::Research,
        4 => Domain::Debug,
        5 => Domain::DevOps,
        _ => Domain::Fullstack,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_llm::providers::mock::MockProvider;
    use ava_types::{Message, Role};

    #[test]
    fn decompose_multidomain_goal() {
        let plan = decompose_task(&Task {
            description: "Update the UI and add backend API tests".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
            history: vec![Message::new(Role::User, "Keep auth intact")],
            preferred_domain: None,
        })
        .expect("should decompose");

        assert_eq!(plan.tasks.len(), 3);
        assert!(plan
            .tasks
            .iter()
            .any(|task| task.preferred_domain == Some(Domain::Frontend)));
        assert!(plan
            .tasks
            .iter()
            .any(|task| task.preferred_domain == Some(Domain::Backend)));
        assert!(plan
            .tasks
            .iter()
            .any(|task| task.preferred_domain == Some(Domain::QA)));
    }

    #[test]
    fn single_domain_goal_stays_single() {
        let plan = decompose_task(&Task {
            description: "Refactor the backend auth middleware".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
            history: vec![],
            preferred_domain: None,
        });
        assert!(plan.is_none());
    }

    #[tokio::test]
    async fn planner_json_decomposition_works() {
        let provider = std::sync::Arc::new(MockProvider::new(
            "mock",
            vec![r#"{"subtasks":[{"domain":"frontend","description":"Polish the UI"},{"domain":"backend","description":"Update the API"}]}"#.to_string()],
        ));
        let task = Task {
            description: "Update UI and API".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
            history: vec![],
            preferred_domain: None,
        };

        let plan = decompose_task_with_planner(
            provider,
            &task,
            &PlannerConfig {
                enabled: true,
                ..PlannerConfig::default()
            },
        )
        .await
        .expect("planner should parse")
        .expect("planner should return plan");
        assert_eq!(plan.tasks.len(), 2);
    }

    #[tokio::test]
    async fn planner_can_return_domain_not_seen_by_heuristics() {
        let provider = std::sync::Arc::new(MockProvider::new(
            "mock",
            vec![r#"{"subtasks":[{"domain":"devops","description":"Update the CI workflow"},{"domain":"backend","description":"Adjust the backend release hooks"}]}"#.to_string()],
        ));
        let task = Task {
            description: "Prepare the release pipeline and backend rollout".to_string(),
            task_type: TaskType::Simple,
            files: vec![],
            history: vec![],
            preferred_domain: None,
        };

        let plan = decompose_task_with_planner(
            provider,
            &task,
            &PlannerConfig {
                enabled: true,
                ..PlannerConfig::default()
            },
        )
        .await
        .expect("planner should parse")
        .expect("planner should return plan");
        assert!(plan
            .tasks
            .iter()
            .any(|task| task.preferred_domain == Some(Domain::DevOps)));
    }

    #[test]
    fn infer_file_hints_extracts_repo_paths() {
        let hints = infer_file_hints(
            "Update crates/ava-tui/src/ui/sidebar.rs and docs/development/v3-plan.md",
        );
        assert!(hints.contains(&"crates/ava-tui/src/ui/sidebar.rs".to_string()));
        assert!(hints.contains(&"docs/development/v3-plan.md".to_string()));
    }
}
