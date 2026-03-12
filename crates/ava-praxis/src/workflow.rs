use std::sync::Arc;

use ava_agent::{AgentConfig, AgentEvent, AgentLoop};
use ava_context::ContextManager;
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_platform::StandardPlatform;
use ava_tools::core::register_core_tools;
use ava_tools::registry::ToolRegistry;
use ava_types::{Role, Session, Tool};
use futures::StreamExt;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use tracing::{info, warn};

use crate::events::PraxisEvent;
use crate::Budget;

/// Role a phase plays in a workflow pipeline.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum PhaseRole {
    Planner,
    Coder,
    Reviewer,
    Tester,
    Custom(String),
}

impl std::fmt::Display for PhaseRole {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Planner => write!(f, "Planner"),
            Self::Coder => write!(f, "Coder"),
            Self::Reviewer => write!(f, "Reviewer"),
            Self::Tester => write!(f, "Tester"),
            Self::Custom(name) => write!(f, "{name}"),
        }
    }
}

/// A single phase in a workflow pipeline.
#[derive(Debug, Clone)]
pub struct Phase {
    pub name: String,
    pub role: PhaseRole,
    pub system_prompt_override: Option<String>,
    pub max_turns: Option<usize>,
    pub receives_prior_output: bool,
}

/// A multi-phase workflow definition.
#[derive(Debug, Clone)]
pub struct Workflow {
    pub name: String,
    pub phases: Vec<Phase>,
    pub max_iterations: usize,
}

impl Workflow {
    /// Planner → Coder → Reviewer pipeline with feedback loop.
    pub fn plan_code_review() -> Self {
        Self {
            name: "plan-code-review".to_string(),
            phases: vec![
                Phase {
                    name: "Plan".to_string(),
                    role: PhaseRole::Planner,
                    system_prompt_override: None,
                    max_turns: None,
                    receives_prior_output: false,
                },
                Phase {
                    name: "Code".to_string(),
                    role: PhaseRole::Coder,
                    system_prompt_override: None,
                    max_turns: None,
                    receives_prior_output: true,
                },
                Phase {
                    name: "Review".to_string(),
                    role: PhaseRole::Reviewer,
                    system_prompt_override: None,
                    max_turns: None,
                    receives_prior_output: true,
                },
            ],
            max_iterations: 2,
        }
    }

    /// Coder → Reviewer pipeline with feedback loop.
    pub fn code_review() -> Self {
        Self {
            name: "code-review".to_string(),
            phases: vec![
                Phase {
                    name: "Code".to_string(),
                    role: PhaseRole::Coder,
                    system_prompt_override: None,
                    max_turns: None,
                    receives_prior_output: false,
                },
                Phase {
                    name: "Review".to_string(),
                    role: PhaseRole::Reviewer,
                    system_prompt_override: None,
                    max_turns: None,
                    receives_prior_output: true,
                },
            ],
            max_iterations: 2,
        }
    }

    /// Planner → Coder pipeline (no review).
    pub fn plan_code() -> Self {
        Self {
            name: "plan-code".to_string(),
            phases: vec![
                Phase {
                    name: "Plan".to_string(),
                    role: PhaseRole::Planner,
                    system_prompt_override: None,
                    max_turns: None,
                    receives_prior_output: false,
                },
                Phase {
                    name: "Code".to_string(),
                    role: PhaseRole::Coder,
                    system_prompt_override: None,
                    max_turns: None,
                    receives_prior_output: true,
                },
            ],
            max_iterations: 1,
        }
    }

    /// Look up a preset workflow by name.
    pub fn from_name(name: &str) -> Option<Self> {
        match name {
            "plan-code-review" => Some(Self::plan_code_review()),
            "code-review" => Some(Self::code_review()),
            "plan-code" => Some(Self::plan_code()),
            _ => None,
        }
    }
}

/// Build a phase-specific system prompt based on role.
pub fn build_phase_system_prompt(role: &PhaseRole, tools: &[Tool], native_tools: bool) -> String {
    let tool_section = if native_tools {
        let mut s = String::from("## Available Tools\n");
        for tool in tools {
            s.push_str(&format!("- **{}**: {}\n", tool.name, tool.description));
        }
        s
    } else {
        let mut s = String::from("## Tools\n\n");
        s.push_str(
            "To call tools, respond with ONLY a JSON object:\n\
             ```json\n\
             {\"tool_calls\": [{\"name\": \"tool_name\", \"arguments\": {\"param\": \"value\"}}]}\n\
             ```\n\n",
        );
        for tool in tools {
            s.push_str(&format!(
                "### {}\n{}\nParameters: {}\n\n",
                tool.name,
                tool.description,
                serde_json::to_string(&tool.parameters).unwrap_or_else(|_| "{}".to_string()),
            ));
        }
        s
    };

    match role {
        PhaseRole::Planner => format!(
            "You are AVA in Planner mode. Analyze the goal and produce a detailed, actionable plan.\n\n\
             ## Rules\n\
             - Read files to understand the codebase before planning.\n\
             - Output a structured plan with numbered steps.\n\
             - Do NOT write or modify any code — only read and search.\n\
             - When done, call `attempt_completion` with your plan as the result.\n\n\
             {tool_section}"
        ),
        PhaseRole::Coder => format!(
            "You are AVA in Coder mode. Implement the plan or goal by writing code.\n\n\
             ## Rules\n\
             - Read files before modifying them.\n\
             - Follow the plan from the previous phase if provided.\n\
             - Run tests after making changes when possible.\n\
             - When done, call `attempt_completion` with a summary of changes.\n\n\
             {tool_section}"
        ),
        PhaseRole::Reviewer => format!(
            "You are AVA in Reviewer mode. Review the code changes from the previous phase.\n\n\
             ## Rules\n\
             - Read the changed files and examine the implementation.\n\
             - Check for bugs, style issues, missing edge cases, and test coverage.\n\
             - If the code looks good, respond with \"LGTM\" and call `attempt_completion`.\n\
             - If changes are needed, list specific issues and call `attempt_completion` with your feedback.\n\
             - Do NOT modify code — only read and analyze.\n\n\
             {tool_section}"
        ),
        PhaseRole::Tester => format!(
            "You are AVA in Tester mode. Run and fix tests for the codebase.\n\n\
             ## Rules\n\
             - Run the test suite to identify failures.\n\
             - Fix failing tests or the code causing failures.\n\
             - When all tests pass, call `attempt_completion` with the results.\n\n\
             {tool_section}"
        ),
        PhaseRole::Custom(name) => format!(
            "You are AVA in {name} mode. Complete the assigned task.\n\n\
             ## Rules\n\
             - Read files before modifying them.\n\
             - When done, call `attempt_completion` with a summary.\n\n\
             {tool_section}"
        ),
    }
}

/// Register tools appropriate for a phase role.
pub fn register_tools_for_role(role: &PhaseRole, platform: Arc<StandardPlatform>) -> ToolRegistry {
    let mut registry = ToolRegistry::new();
    match role {
        PhaseRole::Planner => {
            // Read-only tools
            let cache = ava_tools::core::hashline::new_cache();
            registry.register(ava_tools::core::read::ReadTool::new(
                platform.clone(),
                cache,
            ));
            registry.register(ava_tools::core::glob::GlobTool::new());
            registry.register(ava_tools::core::grep::GrepTool::new());
        }
        PhaseRole::Reviewer => {
            // Read-only tools
            let cache = ava_tools::core::hashline::new_cache();
            registry.register(ava_tools::core::read::ReadTool::new(
                platform.clone(),
                cache,
            ));
            registry.register(ava_tools::core::glob::GlobTool::new());
            registry.register(ava_tools::core::grep::GrepTool::new());
        }
        PhaseRole::Coder | PhaseRole::Tester | PhaseRole::Custom(_) => {
            register_core_tools(&mut registry, platform);
        }
    }
    registry
}

/// Extract the output of a phase from its session (last 2 assistant messages, capped at 4000 chars).
pub fn extract_phase_output(session: &Session) -> String {
    let assistant_msgs: Vec<&str> = session
        .messages
        .iter()
        .rev()
        .filter(|m| m.role == Role::Assistant && !m.content.trim().is_empty())
        .take(2)
        .map(|m| m.content.as_str())
        .collect();

    let output = assistant_msgs
        .into_iter()
        .rev()
        .collect::<Vec<_>>()
        .join("\n\n");

    if output.len() > 4000 {
        format!("{}...", &output[..4000])
    } else {
        output
    }
}

/// Check if reviewer output indicates revisions are needed.
pub fn needs_revision(output: &str) -> bool {
    let lower = output.to_lowercase();

    // Approval signals — if present, no revision needed
    let approval = ["lgtm", "looks good", "approved", "no issues", "well done"];
    if approval.iter().any(|s| lower.contains(s)) {
        return false;
    }

    // Revision signals
    let revision = [
        "fix",
        "bug",
        "issue",
        "error",
        "missing",
        "incorrect",
        "wrong",
        "should",
        "must",
        "needs",
        "change",
        "update",
        "improve",
    ];
    revision.iter().any(|s| lower.contains(s))
}

/// Executes a multi-phase workflow pipeline with output chaining and feedback loops.
pub struct WorkflowExecutor {
    workflow: Workflow,
    budget: Budget,
    provider: Arc<dyn LLMProvider>,
    platform: Arc<StandardPlatform>,
}

impl WorkflowExecutor {
    pub fn new(
        workflow: Workflow,
        budget: Budget,
        provider: Arc<dyn LLMProvider>,
        platform: Arc<StandardPlatform>,
    ) -> Self {
        Self {
            workflow,
            budget,
            provider,
            platform,
        }
    }

    pub async fn execute(
        &self,
        goal: &str,
        cancel: CancellationToken,
        event_tx: mpsc::UnboundedSender<PraxisEvent>,
    ) -> ava_types::Result<Session> {
        let phase_count = self.workflow.phases.len();
        let per_phase_turns = (self.budget.max_turns / phase_count).max(1);
        let per_phase_cost = self.budget.max_cost_usd / phase_count as f64;

        let mut combined_session = Session::new();
        let mut prior_output: Option<String> = None;
        let mut total_turns = 0;
        let mut iteration = 0;

        loop {
            iteration += 1;
            let _ = event_tx.send(PraxisEvent::IterationStarted {
                iteration,
                max_iterations: self.workflow.max_iterations,
            });
            info!(
                iteration,
                max = self.workflow.max_iterations,
                "Starting workflow iteration"
            );

            let mut phases_completed = 0;

            for (phase_idx, phase) in self.workflow.phases.iter().enumerate() {
                if cancel.is_cancelled() {
                    warn!("Workflow cancelled at phase {}", phase.name);
                    break;
                }

                let _ = event_tx.send(PraxisEvent::PhaseStarted {
                    phase_index: phase_idx,
                    phase_count,
                    phase_name: phase.name.clone(),
                    role: phase.role.to_string(),
                });

                // Build the phase goal
                let phase_goal = if phase.receives_prior_output {
                    if let Some(ref prev) = prior_output {
                        format!(
                            "Original goal: {goal}\n\n\
                             Output from previous phase:\n{prev}"
                        )
                    } else {
                        goal.to_string()
                    }
                } else {
                    goal.to_string()
                };

                let phase_turns = phase.max_turns.unwrap_or(per_phase_turns);
                let phase_budget = Budget {
                    max_tokens: self.budget.max_tokens,
                    max_turns: phase_turns,
                    max_cost_usd: per_phase_cost,
                };

                let session = run_phase_worker(PhaseWorkerParams {
                    role: &phase.role,
                    system_prompt_override: phase.system_prompt_override.as_deref(),
                    goal: &phase_goal,
                    budget: &phase_budget,
                    provider: self.provider.clone(),
                    platform: self.platform.clone(),
                    cancel: cancel.clone(),
                    event_tx: event_tx.clone(),
                })
                .await?;

                let output = extract_phase_output(&session);
                let turns = session
                    .messages
                    .iter()
                    .filter(|m| m.role == Role::Assistant)
                    .count();
                total_turns += turns;

                // Add phase marker to combined session
                combined_session.add_message(ava_types::Message::new(
                    Role::System,
                    format!(
                        "[phase-{}: {} ({})] — {} turns",
                        phase_idx, phase.name, phase.role, turns
                    ),
                ));
                for msg in &session.messages {
                    combined_session.add_message(msg.clone());
                }

                let preview = if output.len() > 200 {
                    format!("{}...", &output[..200])
                } else {
                    output.clone()
                };

                let _ = event_tx.send(PraxisEvent::PhaseCompleted {
                    phase_index: phase_idx,
                    phase_name: phase.name.clone(),
                    turns,
                    output_preview: preview,
                });

                prior_output = Some(output);
                phases_completed += 1;
            }

            // Check if we need a feedback loop
            if cancel.is_cancelled() {
                break;
            }

            // Only loop if there's a Reviewer phase and it requested revisions
            let has_reviewer = self
                .workflow
                .phases
                .iter()
                .any(|p| p.role == PhaseRole::Reviewer);
            if has_reviewer && iteration < self.workflow.max_iterations {
                if let Some(ref output) = prior_output {
                    if needs_revision(output) {
                        info!(iteration, "Reviewer requested revisions, looping back");
                        // Reset prior_output to the reviewer feedback for the next Coder phase
                        continue;
                    }
                }
            }

            // Done — no more iterations needed
            let _ = event_tx.send(PraxisEvent::WorkflowComplete {
                phases_completed,
                total_phases: phase_count,
                iterations: iteration,
                total_turns,
            });
            break;
        }

        Ok(combined_session)
    }
}

/// Parameters for running a single workflow phase.
struct PhaseWorkerParams<'a> {
    role: &'a PhaseRole,
    system_prompt_override: Option<&'a str>,
    goal: &'a str,
    budget: &'a Budget,
    provider: Arc<dyn LLMProvider>,
    platform: Arc<StandardPlatform>,
    cancel: CancellationToken,
    event_tx: mpsc::UnboundedSender<PraxisEvent>,
}

/// Run a single phase as an AgentLoop.
async fn run_phase_worker(params: PhaseWorkerParams<'_>) -> ava_types::Result<Session> {
    let PhaseWorkerParams {
        role,
        system_prompt_override,
        goal,
        budget,
        provider,
        platform,
        cancel,
        event_tx,
    } = params;
    let registry = register_tools_for_role(role, platform);
    let tool_defs = registry.list_tools();
    let native_tools = provider.supports_tools();

    let system_prompt = system_prompt_override
        .map(String::from)
        .unwrap_or_else(|| build_phase_system_prompt(role, &tool_defs, native_tools));

    let config = AgentConfig {
        max_turns: budget.max_turns,
        max_budget_usd: 0.0,
        token_limit: budget.max_tokens,
        model: provider.model_name().to_string(),
        max_cost_usd: budget.max_cost_usd,
        loop_detection: true,
        custom_system_prompt: Some(system_prompt),
        thinking_level: ava_types::ThinkingLevel::Off,
        system_prompt_suffix: None,
        extended_tools: true,
        plan_mode: false,
        post_edit_validation: None,
    };

    let context = ContextManager::new(budget.max_tokens);
    let mut agent = AgentLoop::new(
        Box::new(SharedProvider::new(provider)),
        registry,
        context,
        config,
    );

    let mut stream = agent.run_streaming(goal).await;
    let mut session: Option<Session> = None;

    loop {
        tokio::select! {
            _ = cancel.cancelled() => {
                return Err(ava_types::AvaError::Cancelled);
            }
            maybe_event = stream.next() => {
                let Some(event) = maybe_event else { break; };
                // Forward relevant events
                match &event {
                    AgentEvent::Token(token) => {
                        let _ = event_tx.send(PraxisEvent::WorkerToken {
                            worker_id: uuid::Uuid::nil(),
                            token: token.clone(),
                        });
                    }
                    AgentEvent::Progress(msg) => {
                        if let Some(turn) = msg.strip_prefix("turn ").and_then(|s| s.parse::<usize>().ok()) {
                            let _ = event_tx.send(PraxisEvent::WorkerProgress {
                                worker_id: uuid::Uuid::nil(),
                                turn,
                                max_turns: 0,
                            });
                        }
                    }
                    AgentEvent::Complete(s) => {
                        session = Some(s.clone());
                        break;
                    }
                    AgentEvent::Error(e) => {
                        return Err(ava_types::AvaError::ToolError(e.clone()));
                    }
                    _ => {}
                }
            }
        }
    }

    session
        .ok_or_else(|| ava_types::AvaError::ToolError("Phase ended without completion".to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plan_code_review_has_three_phases() {
        let w = Workflow::plan_code_review();
        assert_eq!(w.phases.len(), 3);
        assert_eq!(w.phases[0].role, PhaseRole::Planner);
        assert_eq!(w.phases[1].role, PhaseRole::Coder);
        assert_eq!(w.phases[2].role, PhaseRole::Reviewer);
        assert_eq!(w.max_iterations, 2);
    }

    #[test]
    fn code_review_has_two_phases() {
        let w = Workflow::code_review();
        assert_eq!(w.phases.len(), 2);
        assert_eq!(w.phases[0].role, PhaseRole::Coder);
        assert_eq!(w.phases[1].role, PhaseRole::Reviewer);
    }

    #[test]
    fn plan_code_has_two_phases_one_iteration() {
        let w = Workflow::plan_code();
        assert_eq!(w.phases.len(), 2);
        assert_eq!(w.max_iterations, 1);
    }

    #[test]
    fn from_name_returns_presets() {
        assert!(Workflow::from_name("plan-code-review").is_some());
        assert!(Workflow::from_name("code-review").is_some());
        assert!(Workflow::from_name("plan-code").is_some());
        assert!(Workflow::from_name("nonexistent").is_none());
    }

    #[test]
    fn needs_revision_detects_issues() {
        assert!(needs_revision("There is a bug in the error handling"));
        assert!(needs_revision("You should fix the missing validation"));
        assert!(needs_revision(
            "Several issues found with the implementation"
        ));
        assert!(needs_revision("The error handling needs improvement"));
    }

    #[test]
    fn needs_revision_detects_approval() {
        assert!(!needs_revision("LGTM, the code looks good"));
        assert!(!needs_revision("Approved - no issues found"));
        assert!(!needs_revision("Looks good to me"));
    }

    #[test]
    fn needs_revision_defaults_to_false_for_neutral() {
        assert!(!needs_revision("The code does what it's supposed to do"));
        assert!(!needs_revision("Implementation complete"));
    }

    #[test]
    fn extract_phase_output_gets_last_assistant_messages() {
        let mut session = Session::new();
        session.add_message(ava_types::Message::new(Role::User, "goal".to_string()));
        session.add_message(ava_types::Message::new(
            Role::Assistant,
            "first response".to_string(),
        ));
        session.add_message(ava_types::Message::new(Role::User, "continue".to_string()));
        session.add_message(ava_types::Message::new(
            Role::Assistant,
            "second response".to_string(),
        ));
        session.add_message(ava_types::Message::new(Role::User, "more".to_string()));
        session.add_message(ava_types::Message::new(
            Role::Assistant,
            "third response".to_string(),
        ));

        let output = extract_phase_output(&session);
        assert!(output.contains("second response"));
        assert!(output.contains("third response"));
        assert!(!output.contains("first response"));
    }

    #[test]
    fn extract_phase_output_truncates_long_content() {
        let mut session = Session::new();
        let long = "x".repeat(5000);
        session.add_message(ava_types::Message::new(Role::Assistant, long));

        let output = extract_phase_output(&session);
        assert!(output.len() <= 4004); // 4000 + "..."
        assert!(output.ends_with("..."));
    }

    #[test]
    fn budget_divides_across_phases() {
        let budget = Budget {
            max_tokens: 128_000,
            max_turns: 30,
            max_cost_usd: 9.0,
        };
        let phase_count = 3;
        let per_phase_turns = budget.max_turns / phase_count;
        let per_phase_cost = budget.max_cost_usd / phase_count as f64;

        assert_eq!(per_phase_turns, 10);
        assert!((per_phase_cost - 3.0).abs() < f64::EPSILON);
    }

    #[test]
    fn phase_role_serialization_roundtrip() {
        let roles = vec![
            PhaseRole::Planner,
            PhaseRole::Coder,
            PhaseRole::Reviewer,
            PhaseRole::Tester,
            PhaseRole::Custom("Deployer".to_string()),
        ];

        for role in roles {
            let json = serde_json::to_string(&role).unwrap();
            let restored: PhaseRole = serde_json::from_str(&json).unwrap();
            assert_eq!(role, restored);
        }
    }

    #[test]
    fn phase_role_display() {
        assert_eq!(PhaseRole::Planner.to_string(), "Planner");
        assert_eq!(PhaseRole::Custom("X".to_string()).to_string(), "X");
    }
}
