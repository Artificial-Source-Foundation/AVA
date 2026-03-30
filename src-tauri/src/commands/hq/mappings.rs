use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use ava_agent::stack::AgentStack;
use ava_config::{HqAgentOverride, HqConfig as HqSettingsConfig};
use ava_db::models::{
    HqActivityRecord, HqAgentRecord, HqAgentTranscriptRecord, HqChatMessageRecord, HqCommentRecord,
    HqEpicRecord, HqIssueRecord,
};
use ava_db::HqRepository;
use ava_hq::{BoardResult, Budget, Domain, HqMemoryBootstrapResult, HqPlan, TaskComplexity};
use ava_llm::provider::LLMProvider;

use super::data::{
    parse_json_vec, HqActivityEventDto, HqAgentDto, HqAgentProgressDto, HqBoardOpinionDto,
    HqBoardReviewDto, HqCommentDto, HqDelegationCardDto, HqDirectorMessageDto, HqEpicDto,
    HqFileChangeDto, HqIssueDto, HqPhaseDto, HqPlanDto, HqPlanTaskDto, HqSettingsDto,
    HqTranscriptEntryDto, HqWorkspaceBootstrapDto, TeamConfigPayload,
};
use crate::commands::helpers::resolve_model_spec;

pub(super) fn director_settings_to_dto(settings: &HqSettingsConfig) -> HqSettingsDto {
    HqSettingsDto {
        director_model: settings.director_model.clone(),
        tone_preference: settings.tone_preference.clone(),
        auto_review: settings.auto_review,
        show_costs: settings.show_costs,
    }
}

pub(super) fn board_review_from_result(result: &BoardResult) -> HqBoardReviewDto {
    HqBoardReviewDto {
        consensus: result.consensus.clone(),
        vote_summary: result.vote_summary.clone(),
        opinions: result
            .opinions
            .iter()
            .map(|opinion| HqBoardOpinionDto {
                member_name: opinion.member_name.clone(),
                personality: opinion.personality.to_string(),
                recommendation: opinion.recommendation.clone(),
                vote: opinion.vote.to_string(),
            })
            .collect(),
    }
}

pub(super) fn hq_bootstrap_to_dto(result: HqMemoryBootstrapResult) -> HqWorkspaceBootstrapDto {
    HqWorkspaceBootstrapDto {
        project_root: result.project_root,
        hq_root: result.hq_root,
        project_name: result.project_name,
        stack_summary: result.stack_summary,
        created_files: result.created_files,
        reused_existing: result.reused_existing,
    }
}

fn phase_execution_label(tasks: &[HqPlanTaskDto]) -> &'static str {
    if tasks.len() > 1 {
        "parallel"
    } else {
        "sequential"
    }
}

fn complexity_label(complexity: &TaskComplexity) -> &'static str {
    match complexity {
        TaskComplexity::Simple => "simple",
        TaskComplexity::Medium => "medium",
        TaskComplexity::Complex => "complex",
    }
}

pub(super) fn status_color(status: &str) -> &'static str {
    match status {
        "done" | "completed" => "var(--success)",
        "review" | "planning" => "#eab308",
        "in-progress" | "running" | "active" => "#06b6d4",
        _ => "var(--text-muted)",
    }
}

pub(super) fn role_icon(tier: &str) -> &'static str {
    match tier {
        "director" => "crown",
        "lead" => "code",
        "scout" => "search",
        _ => "user",
    }
}

pub(super) fn issue_from_record(record: HqIssueRecord, comments: Vec<HqCommentDto>) -> HqIssueDto {
    HqIssueDto {
        id: record.id,
        identifier: record.identifier,
        title: record.title,
        description: record.description,
        status: record.status,
        priority: record.priority,
        assignee_id: record.assignee_id,
        assignee_name: record.assignee_name,
        epic_id: record.epic_id,
        phase_label: record.phase_label,
        comments,
        files_changed: parse_json_vec::<HqFileChangeDto>(record.files_changed_json.as_deref()),
        agent_progress: match (record.agent_turn, record.agent_max_turns) {
            (Some(turn), Some(max_turns)) => Some(HqAgentProgressDto { turn, max_turns }),
            _ => None,
        },
        agent_live_action: record.agent_live_action,
        is_live: record.is_live != 0,
        created_at: record.created_at,
    }
}

pub(super) fn comment_from_record(record: HqCommentRecord) -> HqCommentDto {
    HqCommentDto {
        id: record.id,
        author_name: record.author_name,
        author_role: record.author_role,
        author_icon: record.author_icon,
        content: record.content,
        timestamp: record.timestamp,
    }
}

pub(super) fn epic_from_record(record: HqEpicRecord, issue_ids: Vec<String>) -> HqEpicDto {
    HqEpicDto {
        id: record.id,
        title: record.title,
        description: record.description,
        status: record.status,
        progress: record.progress,
        issue_ids,
        plan_id: record.plan_id,
        created_at: record.created_at,
    }
}

fn transcript_from_record(record: HqAgentTranscriptRecord) -> HqTranscriptEntryDto {
    HqTranscriptEntryDto {
        id: record.id,
        kind: record.entry_type,
        tool_name: record.tool_name,
        tool_path: record.tool_path,
        tool_status: record.tool_status,
        content: record.content,
        timestamp: record.timestamp,
    }
}

pub(super) fn activity_from_record(record: HqActivityRecord) -> HqActivityEventDto {
    HqActivityEventDto {
        id: record.id,
        kind: record.event_type,
        agent_name: record.agent_name,
        message: record.message,
        color: record.color,
        timestamp: record.timestamp,
    }
}

pub(super) fn chat_from_record(record: HqChatMessageRecord) -> HqDirectorMessageDto {
    HqDirectorMessageDto {
        id: record.id,
        role: record.role,
        content: record.content,
        delegations: parse_json_vec::<HqDelegationCardDto>(record.delegations_json.as_deref()),
        timestamp: record.timestamp,
    }
}

pub(super) async fn agent_from_record(
    repo: &HqRepository,
    record: HqAgentRecord,
) -> Result<HqAgentDto, String> {
    let transcript = repo
        .list_agent_transcript(&record.id)
        .await
        .map_err(super::data::to_string_error)?
        .into_iter()
        .map(transcript_from_record)
        .collect();

    Ok(HqAgentDto {
        id: record.id,
        name: record.name,
        role: record.role,
        tier: record.tier,
        model: record.model,
        status: record.status,
        icon: record.icon,
        parent_id: record.parent_id,
        current_task: record.current_task,
        current_issue_id: record.current_issue_id,
        turn: record.turn,
        max_turns: record.max_turns,
        transcript,
        assigned_issue_ids: parse_json_vec(record.assigned_issue_ids_json.as_deref()),
        files_touched: parse_json_vec(record.files_touched_json.as_deref()),
        total_cost_usd: record.total_cost_usd,
    })
}

fn domain_assignee(domain: &Domain) -> (&'static str, &'static str, &'static str) {
    match domain {
        Domain::Frontend => ("frontend-lead", "Frontend Lead", "lead"),
        Domain::Backend => ("backend-lead", "Backend Lead", "lead"),
        Domain::QA => ("qa-lead", "QA Lead", "lead"),
        Domain::Research => ("research-lead", "Research Lead", "lead"),
        Domain::Debug => ("debug-lead", "Debug Lead", "lead"),
        Domain::DevOps => ("devops-lead", "DevOps Lead", "lead"),
        Domain::Fullstack => ("fullstack-lead", "Fullstack Lead", "lead"),
    }
}

fn build_director_description(goal: &str, plan: &HqPlan, settings: &HqSettingsConfig) -> String {
    let style = if settings.tone_preference == "simple" {
        "I split this into a small set of phases so the team can work in order without stepping on each other."
    } else {
        "I decomposed this goal into dependency-aware execution phases so leads can parallelize independent work and preserve a clean review path."
    };

    format!(
        "{style} Goal: {goal}. Planned {} task(s) across {} phase(s).",
        plan.tasks.len(),
        plan.execution_groups.len()
    )
}

pub(super) fn hq_override<'a>(
    settings: &'a HqSettingsConfig,
    id: &str,
) -> Option<&'a HqAgentOverride> {
    settings
        .agent_overrides
        .iter()
        .find(|override_item| override_item.id == id)
}

pub(super) fn commander_planning_context(
    settings: &HqSettingsConfig,
    context: Option<&str>,
) -> Option<String> {
    let commander_prompt = hq_override(settings, "commander")
        .filter(|override_item| override_item.enabled)
        .map(|override_item| override_item.system_prompt.trim())
        .filter(|prompt| !prompt.is_empty());

    match (
        commander_prompt,
        context.map(str::trim).filter(|value| !value.is_empty()),
    ) {
        (Some(prompt), Some(existing)) => {
            Some(format!("{existing}\n\n## Commander Instructions\n{prompt}"))
        }
        (Some(prompt), None) => Some(format!("## Commander Instructions\n{prompt}")),
        (None, Some(existing)) => Some(existing.to_string()),
        (None, None) => None,
    }
}

pub(super) async fn provider_from_override(
    stack: &AgentStack,
    override_item: Option<&HqAgentOverride>,
) -> Option<Arc<dyn ava_llm::provider::LLMProvider>> {
    let model_spec = override_item
        .map(|item| item.model_spec.trim())
        .unwrap_or("");
    resolve_model_spec(stack, model_spec).await
}

pub(super) async fn collect_board_providers(
    stack: &AgentStack,
    settings: &HqSettingsConfig,
    team_config: Option<&TeamConfigPayload>,
    current_provider: &str,
    current_model: &str,
) -> Vec<Arc<dyn LLMProvider>> {
    let mut specs = Vec::new();
    if !settings.director_model.trim().is_empty() {
        specs.push(settings.director_model.trim().to_string());
    }
    if let Some(team) = team_config {
        if !team.default_director_model.trim().is_empty() {
            specs.push(team.default_director_model.trim().to_string());
        }
    }
    specs.push(format!("{current_provider}/{current_model}"));
    specs.extend([
        "anthropic/claude-opus-4-6".to_string(),
        "openai/gpt-5.4".to_string(),
        "google/gemini-2.5-pro".to_string(),
        "copilot/claude-opus-4.6".to_string(),
        "copilot/gpt-5.4".to_string(),
        "copilot/gemini-2.5-pro".to_string(),
    ]);

    let mut providers = Vec::new();
    let mut seen_models = HashSet::new();
    for spec in specs {
        if let Some(provider) = resolve_model_spec(stack, &spec).await {
            let model_name = provider.model_name().to_string();
            if seen_models.insert(model_name) {
                providers.push(provider);
            }
            if providers.len() >= 3 {
                break;
            }
        }
    }
    providers
}

pub(super) fn should_consult_board(plan: &HqPlan) -> bool {
    plan.tasks.len() >= 4
        || plan
            .tasks
            .iter()
            .any(|task| task.complexity == TaskComplexity::Complex)
}

pub(super) fn fallback_hq_plan(goal: &str) -> HqPlan {
    HqPlan {
        goal: goal.to_string(),
        tasks: vec![ava_hq::HqTask {
            id: "t1".to_string(),
            description: goal.lines().next().unwrap_or(goal).trim().to_string(),
            domain: Domain::Fullstack,
            complexity: TaskComplexity::Medium,
            dependencies: vec![],
            budget: Budget::interactive(20, 2.0),
            files_hint: vec![],
        }],
        execution_groups: vec![ava_hq::ExecutionGroup {
            task_ids: vec!["t1".to_string()],
            label: "Phase 1: Implementation".to_string(),
        }],
        total_budget: Budget::interactive(20, 2.0),
    }
}

pub(super) fn convert_plan(
    epic_id: &str,
    plan_id: &str,
    status: &str,
    plan: &HqPlan,
    settings: &HqSettingsConfig,
) -> HqPlanDto {
    let mut task_lookup = HashMap::new();
    for task in &plan.tasks {
        task_lookup.insert(task.id.clone(), task.clone());
    }

    let phases = plan
        .execution_groups
        .iter()
        .enumerate()
        .map(|(index, group)| {
            let tasks: Vec<HqPlanTaskDto> = group
                .task_ids
                .iter()
                .filter_map(|task_id| task_lookup.get(task_id))
                .map(|task| {
                    let (assignee_id, assignee_name, _) = domain_assignee(&task.domain);
                    HqPlanTaskDto {
                        id: task.id.clone(),
                        title: task.description.clone(),
                        domain: format!("{:?}", task.domain).to_ascii_lowercase(),
                        complexity: complexity_label(&task.complexity).to_string(),
                        dependencies: task.dependencies.clone(),
                        assignee_id: Some(assignee_id.to_string()),
                        assignee_name: Some(assignee_name.to_string()),
                        assignee_model: None,
                        steps: vec![task.description.clone()],
                        file_hints: task.files_hint.clone(),
                        budget_max_tokens: task.budget.max_tokens,
                        budget_max_turns: task.budget.max_turns,
                        budget_max_cost_usd: task.budget.max_cost_usd,
                        expanded: matches!(task.complexity, TaskComplexity::Complex),
                    }
                })
                .collect();

            HqPhaseDto {
                id: format!("phase-{}", index + 1),
                number: (index + 1) as i64,
                name: group.label.clone(),
                description: format!("Execute {} planned task(s).", tasks.len()),
                execution: phase_execution_label(&tasks).to_string(),
                depends_on: if index == 0 {
                    vec![]
                } else {
                    vec![format!("phase-{index}")]
                },
                tasks,
                review_enabled: settings.auto_review,
                review_assignee: settings.auto_review.then(|| "QA Lead".to_string()),
            }
        })
        .collect();

    HqPlanDto {
        id: plan_id.to_string(),
        epic_id: epic_id.to_string(),
        title: plan.goal.clone(),
        status: status.to_string(),
        director_description: build_director_description(&plan.goal, plan, settings),
        board_review: None,
        phases,
    }
}
