//! LLM-powered Director planning — replaces static `pick_domain()` routing.
//!
//! The Director analyzes the user's goal via its LLM provider and produces a
//! structured `PraxisPlan` with tasks, execution groups, budget allocation,
//! and recommended leads to spawn.

use crate::{Budget, Domain};
use ava_llm::provider::LLMProvider;
use ava_types::{AvaError, Message, Result, Role};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

/// A structured plan produced by the Director's LLM analysis of a user goal.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PraxisPlan {
    /// The original user goal.
    pub goal: String,
    /// Ordered list of tasks to execute.
    pub tasks: Vec<PraxisTask>,
    /// Sequential groups of parallel tasks (phase 1, phase 2, ...).
    pub execution_groups: Vec<ExecutionGroup>,
    /// Total budget allocated across all tasks.
    pub total_budget: Budget,
}

/// A single task within a plan, assigned to a specific domain.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PraxisTask {
    /// Unique task identifier (e.g., "t1", "t2").
    pub id: String,
    /// What the worker should accomplish.
    pub description: String,
    /// Which domain lead should own this task.
    pub domain: Domain,
    /// Estimated complexity level.
    pub complexity: TaskComplexity,
    /// IDs of tasks that must complete before this one can start.
    pub dependencies: Vec<String>,
    /// Budget allocation for this task.
    pub budget: Budget,
    /// Suggested files to work on (hints from the LLM).
    pub files_hint: Vec<String>,
}

/// How complex a task is, affecting resource allocation.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum TaskComplexity {
    /// One worker, few files.
    Simple,
    /// One lead + workers.
    Medium,
    /// Needs planning phase first.
    Complex,
}

/// A group of tasks that can run in parallel. Groups execute sequentially.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ExecutionGroup {
    /// Task IDs in this group (run in parallel).
    pub task_ids: Vec<String>,
    /// Human-readable label (e.g., "Phase 1: Research").
    pub label: String,
}

/// Configuration for the planner LLM call.
#[derive(Debug, Clone)]
pub struct PlannerConfig {
    /// Whether LLM planning is enabled (false = use static pick_domain fallback).
    pub enabled: bool,
    /// Maximum number of subtasks the planner can produce.
    pub max_subtasks: usize,
    /// Maximum number of history messages to include as context.
    pub max_history_messages: usize,
}

impl Default for PlannerConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            max_subtasks: 5,
            max_history_messages: 5,
        }
    }
}

const PLANNING_SYSTEM_PROMPT: &str = r#"You are the Praxis Director — an expert software architect who breaks down goals into concrete tasks for a team of domain-specific leads.

Analyze the user's goal and produce a structured execution plan as JSON. You MUST respond with valid JSON only, no markdown fences, no commentary.

Available domains: "frontend", "backend", "qa", "research", "debug", "devops", "fullstack"
Complexity levels: "simple", "medium", "complex"

Rules:
1. Produce 1-5 tasks. Use fewer when the goal is focused.
2. Only recommend leads that are actually needed — do NOT spawn all 7.
3. Identify dependencies: if task B needs task A's output, list A's id in B's dependencies.
4. Group tasks into execution phases: tasks within a phase run in parallel, phases run sequentially.
5. Allocate budget fractions (turns and cost) proportional to complexity.
6. Include file hints when you can infer which files will be touched.
7. For simple, single-domain goals, return exactly 1 task.

JSON schema:
{
  "tasks": [
    {
      "id": "t1",
      "description": "what to do",
      "domain": "backend",
      "complexity": "medium",
      "dependencies": [],
      "files_hint": ["src/main.rs"]
    }
  ],
  "execution_groups": [
    {
      "task_ids": ["t1"],
      "label": "Phase 1: Implementation"
    }
  ]
}"#;

/// Raw LLM response structure for deserialization.
#[derive(Deserialize)]
struct PlanResponse {
    tasks: Vec<PlanTaskResponse>,
    execution_groups: Vec<ExecutionGroupResponse>,
}

#[derive(Deserialize)]
struct PlanTaskResponse {
    id: String,
    description: String,
    domain: String,
    complexity: String,
    #[serde(default)]
    dependencies: Vec<String>,
    #[serde(default)]
    files_hint: Vec<String>,
}

#[derive(Deserialize)]
struct ExecutionGroupResponse {
    task_ids: Vec<String>,
    label: String,
}

/// Use the Director's LLM to analyze a goal and produce a structured plan.
pub async fn create_plan(
    provider: Arc<dyn LLMProvider>,
    goal: &str,
    context: Option<&str>,
    total_budget: &Budget,
    config: &PlannerConfig,
) -> Result<PraxisPlan> {
    let messages = build_plan_messages(goal, context, config);
    let raw = provider.generate(&messages).await?;
    parse_plan_response(&raw, goal, total_budget, config)
}

fn build_plan_messages(goal: &str, context: Option<&str>, _config: &PlannerConfig) -> Vec<Message> {
    let mut prompt = format!("Goal: {goal}\n");
    if let Some(ctx) = context {
        prompt.push_str(&format!("\nCodebase context:\n{ctx}\n"));
    }

    vec![
        Message::new(Role::System, PLANNING_SYSTEM_PROMPT.to_string()),
        Message::new(Role::User, prompt),
    ]
}

fn parse_plan_response(
    raw: &str,
    goal: &str,
    total_budget: &Budget,
    config: &PlannerConfig,
) -> Result<PraxisPlan> {
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

    let parsed: PlanResponse = serde_json::from_str(json).map_err(|err| {
        AvaError::ToolError(format!("invalid plan JSON from Director LLM: {err}"))
    })?;

    if parsed.tasks.is_empty() || parsed.tasks.len() > config.max_subtasks {
        return Err(AvaError::ToolError(format!(
            "plan must have 1-{} tasks, got {}",
            config.max_subtasks,
            parsed.tasks.len()
        )));
    }

    // Validate all task IDs referenced in execution_groups exist
    let task_ids: Vec<String> = parsed.tasks.iter().map(|t| t.id.clone()).collect();
    for group in &parsed.execution_groups {
        for tid in &group.task_ids {
            if !task_ids.iter().any(|id| id == tid) {
                return Err(AvaError::ToolError(format!(
                    "execution group references unknown task id: {tid}"
                )));
            }
        }
    }

    // Validate dependencies reference valid task IDs
    for task in &parsed.tasks {
        for dep in &task.dependencies {
            if !task_ids.iter().any(|id| id == dep) {
                return Err(AvaError::ToolError(format!(
                    "task '{}' depends on unknown task id: {dep}",
                    task.id
                )));
            }
        }
    }

    // Distribute budget proportionally by complexity
    let total_weight: f64 = parsed
        .tasks
        .iter()
        .map(|t| complexity_weight(&t.complexity))
        .sum();

    let num_tasks = parsed.tasks.len();
    let tasks = parsed
        .tasks
        .into_iter()
        .map(|t| {
            let weight = complexity_weight(&t.complexity);
            let fraction = if total_weight > 0.0 {
                weight / total_weight
            } else {
                1.0 / num_tasks as f64
            };

            PraxisTask {
                id: t.id,
                description: t.description,
                domain: parse_domain(&t.domain).unwrap_or(Domain::Fullstack),
                complexity: parse_complexity(&t.complexity),
                dependencies: t.dependencies,
                budget: Budget {
                    max_tokens: ((total_budget.max_tokens as f64) * fraction) as usize,
                    max_turns: ((total_budget.max_turns as f64) * fraction).ceil() as usize,
                    max_cost_usd: total_budget.max_cost_usd * fraction,
                },
                files_hint: t.files_hint,
            }
        })
        .collect();

    let execution_groups = if parsed.execution_groups.is_empty() {
        // If the LLM didn't provide groups, put all tasks in one group
        vec![ExecutionGroup {
            task_ids: task_ids.iter().map(|s| s.to_string()).collect(),
            label: "Phase 1: Execution".to_string(),
        }]
    } else {
        parsed
            .execution_groups
            .into_iter()
            .map(|g| ExecutionGroup {
                task_ids: g.task_ids,
                label: g.label,
            })
            .collect()
    };

    Ok(PraxisPlan {
        goal: goal.to_string(),
        tasks,
        execution_groups,
        total_budget: total_budget.clone(),
    })
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

fn parse_complexity(raw: &str) -> TaskComplexity {
    match raw.trim().to_ascii_lowercase().as_str() {
        "simple" => TaskComplexity::Simple,
        "medium" => TaskComplexity::Medium,
        "complex" => TaskComplexity::Complex,
        _ => TaskComplexity::Medium,
    }
}

fn complexity_weight(raw: &str) -> f64 {
    match raw.trim().to_ascii_lowercase().as_str() {
        "simple" => 1.0,
        "medium" => 2.0,
        "complex" => 4.0,
        _ => 2.0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_llm::providers::mock::MockProvider;

    fn sample_budget() -> Budget {
        Budget::new(128_000, 20, 5.0)
    }

    #[tokio::test]
    async fn plan_single_task_goal() {
        let response = r#"{"tasks":[{"id":"t1","description":"Fix the auth bug in login.rs","domain":"backend","complexity":"simple","dependencies":[],"files_hint":["src/login.rs"]}],"execution_groups":[{"task_ids":["t1"],"label":"Phase 1: Fix"}]}"#;
        let provider = Arc::new(MockProvider::new("mock", vec![response.to_string()]));

        let plan = create_plan(
            provider,
            "Fix the login bug",
            None,
            &sample_budget(),
            &PlannerConfig::default(),
        )
        .await
        .expect("should create plan");

        assert_eq!(plan.tasks.len(), 1);
        assert_eq!(plan.tasks[0].domain, Domain::Backend);
        assert_eq!(plan.tasks[0].complexity, TaskComplexity::Simple);
        assert_eq!(plan.execution_groups.len(), 1);
    }

    #[tokio::test]
    async fn plan_multi_phase_goal() {
        let response = r#"{
            "tasks": [
                {"id":"t1","description":"Research best auth patterns","domain":"research","complexity":"simple","dependencies":[],"files_hint":[]},
                {"id":"t2","description":"Implement OAuth backend","domain":"backend","complexity":"complex","dependencies":["t1"],"files_hint":["crates/ava-auth/src/lib.rs"]},
                {"id":"t3","description":"Add login UI","domain":"frontend","complexity":"medium","dependencies":["t2"],"files_hint":["src/components/Login.tsx"]},
                {"id":"t4","description":"Write integration tests","domain":"qa","complexity":"medium","dependencies":["t2","t3"],"files_hint":[]}
            ],
            "execution_groups": [
                {"task_ids":["t1"],"label":"Phase 1: Research"},
                {"task_ids":["t2"],"label":"Phase 2: Backend"},
                {"task_ids":["t3"],"label":"Phase 3: Frontend"},
                {"task_ids":["t4"],"label":"Phase 4: Testing"}
            ]
        }"#;
        let provider = Arc::new(MockProvider::new("mock", vec![response.to_string()]));

        let plan = create_plan(
            provider,
            "Add OAuth login to the app",
            None,
            &sample_budget(),
            &PlannerConfig::default(),
        )
        .await
        .expect("should create plan");

        assert_eq!(plan.tasks.len(), 4);
        assert_eq!(plan.execution_groups.len(), 4);

        // Budget should be allocated by complexity weights
        // simple=1, complex=4, medium=2, medium=2 → total=9
        let research_task = &plan.tasks[0];
        let backend_task = &plan.tasks[1];
        assert!(backend_task.budget.max_turns > research_task.budget.max_turns);
        assert_eq!(research_task.complexity, TaskComplexity::Simple);
        assert_eq!(backend_task.complexity, TaskComplexity::Complex);

        // Dependencies are preserved
        assert!(plan.tasks[1].dependencies.contains(&"t1".to_string()));
        assert!(plan.tasks[3].dependencies.contains(&"t2".to_string()));
        assert!(plan.tasks[3].dependencies.contains(&"t3".to_string()));
    }

    #[tokio::test]
    async fn plan_parallel_tasks() {
        let response = r#"{
            "tasks": [
                {"id":"t1","description":"Fix backend API","domain":"backend","complexity":"medium","dependencies":[],"files_hint":[]},
                {"id":"t2","description":"Fix frontend styles","domain":"frontend","complexity":"medium","dependencies":[],"files_hint":[]},
                {"id":"t3","description":"Run full test suite","domain":"qa","complexity":"simple","dependencies":["t1","t2"],"files_hint":[]}
            ],
            "execution_groups": [
                {"task_ids":["t1","t2"],"label":"Phase 1: Parallel fixes"},
                {"task_ids":["t3"],"label":"Phase 2: Verification"}
            ]
        }"#;
        let provider = Arc::new(MockProvider::new("mock", vec![response.to_string()]));

        let plan = create_plan(
            provider,
            "Fix bugs and verify",
            None,
            &sample_budget(),
            &PlannerConfig::default(),
        )
        .await
        .expect("should create plan");

        assert_eq!(plan.execution_groups.len(), 2);
        assert_eq!(plan.execution_groups[0].task_ids.len(), 2);
        assert_eq!(plan.execution_groups[1].task_ids.len(), 1);
    }

    #[tokio::test]
    async fn plan_rejects_invalid_references() {
        let response = r#"{"tasks":[{"id":"t1","description":"Do it","domain":"backend","complexity":"simple","dependencies":["t99"],"files_hint":[]}],"execution_groups":[{"task_ids":["t1"],"label":"Go"}]}"#;
        let provider = Arc::new(MockProvider::new("mock", vec![response.to_string()]));

        let result = create_plan(
            provider,
            "Do something",
            None,
            &sample_budget(),
            &PlannerConfig::default(),
        )
        .await;

        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("unknown task id"));
    }

    #[tokio::test]
    async fn plan_rejects_empty_tasks() {
        let response = r#"{"tasks":[],"execution_groups":[]}"#;
        let provider = Arc::new(MockProvider::new("mock", vec![response.to_string()]));

        let result = create_plan(
            provider,
            "Do something",
            None,
            &sample_budget(),
            &PlannerConfig::default(),
        )
        .await;

        assert!(result.is_err());
    }

    #[tokio::test]
    async fn plan_handles_markdown_fenced_json() {
        let response = "```json\n{\"tasks\":[{\"id\":\"t1\",\"description\":\"Fix it\",\"domain\":\"fullstack\",\"complexity\":\"simple\",\"dependencies\":[],\"files_hint\":[]}],\"execution_groups\":[{\"task_ids\":[\"t1\"],\"label\":\"Fix\"}]}\n```";
        let provider = Arc::new(MockProvider::new("mock", vec![response.to_string()]));

        let plan = create_plan(
            provider,
            "Fix it",
            None,
            &sample_budget(),
            &PlannerConfig::default(),
        )
        .await
        .expect("should handle fenced JSON");

        assert_eq!(plan.tasks.len(), 1);
    }

    #[tokio::test]
    async fn plan_defaults_missing_groups() {
        let response = r#"{"tasks":[{"id":"t1","description":"Do A","domain":"backend","complexity":"simple","dependencies":[],"files_hint":[]},{"id":"t2","description":"Do B","domain":"frontend","complexity":"simple","dependencies":[],"files_hint":[]}],"execution_groups":[]}"#;
        let provider = Arc::new(MockProvider::new("mock", vec![response.to_string()]));

        let plan = create_plan(
            provider,
            "Do stuff",
            None,
            &sample_budget(),
            &PlannerConfig::default(),
        )
        .await
        .expect("should default groups");

        // Empty execution_groups should default to one group with all tasks
        assert_eq!(plan.execution_groups.len(), 1);
        assert_eq!(plan.execution_groups[0].task_ids.len(), 2);
    }
}
