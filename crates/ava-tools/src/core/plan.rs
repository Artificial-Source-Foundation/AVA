//! Plan tool for proposing structured execution plans to the user.
//!
//! The agent calls this tool to present a plan for user review. The plan is
//! sent through a [`PlanBridge`] to the TUI/frontend, which displays it and
//! collects the user's decision (approve, reject, or modify). The tool blocks
//! until the user responds, then returns the decision to the agent.
//!
//! Plans are also persisted to `.ava/plans/` for reuse.

use async_trait::async_trait;
use ava_types::{AvaError, Plan, PlanAction, PlanDecision, PlanState, PlanStep, ToolResult};
use serde_json::{json, Value};
use tokio::sync::{mpsc, oneshot};

use crate::registry::Tool;

/// A request from the agent to present a plan for user review.
#[derive(Debug)]
pub struct PlanRequest {
    /// The proposed plan.
    pub plan: Plan,
    /// Channel to send the user's decision back to the tool.
    pub reply: oneshot::Sender<PlanDecision>,
}

/// Bridge between the plan tool (agent side) and the TUI (UI side).
///
/// The tool sends a `PlanRequest` through this bridge; the TUI receives it,
/// displays the plan, collects the user's decision, and sends it back via the
/// oneshot channel embedded in the request.
#[derive(Clone)]
pub struct PlanBridge {
    tx: mpsc::UnboundedSender<PlanRequest>,
}

impl PlanBridge {
    /// Create a new bridge, returning the bridge handle and the receiving end.
    pub fn new() -> (Self, mpsc::UnboundedReceiver<PlanRequest>) {
        let (tx, rx) = mpsc::unbounded_channel();
        (Self { tx }, rx)
    }
}

/// Tool that proposes a structured execution plan and waits for user approval.
///
/// When the agent has analyzed a task and wants to present its approach, it
/// calls this tool with a structured plan. The plan is displayed to the user
/// who can approve, reject (with feedback), or modify it. The decision is
/// returned to the agent as the tool result.
///
/// Plans are persisted to `.ava/plans/{timestamp}-{slug}.json` for reuse.
pub struct PlanTool {
    bridge: PlanBridge,
    state: PlanState,
}

impl PlanTool {
    pub fn new(bridge: PlanBridge, state: PlanState) -> Self {
        Self { bridge, state }
    }
}

/// Parse a plan action from a string, with fallback.
fn parse_action(s: &str) -> Result<PlanAction, AvaError> {
    match s {
        "research" => Ok(PlanAction::Research),
        "implement" => Ok(PlanAction::Implement),
        "test" => Ok(PlanAction::Test),
        "review" => Ok(PlanAction::Review),
        other => Err(AvaError::ValidationError(format!(
            "invalid action '{other}', expected one of: research, implement, test, review"
        ))),
    }
}

/// Parse a plan from tool arguments.
fn parse_plan(args: &Value) -> ava_types::Result<Plan> {
    let summary = args
        .get("summary")
        .and_then(Value::as_str)
        .ok_or_else(|| AvaError::ValidationError("missing required field: summary".into()))?
        .to_string();

    let steps_val = args
        .get("steps")
        .ok_or_else(|| AvaError::ValidationError("missing required field: steps".into()))?;

    let steps_arr = steps_val
        .as_array()
        .ok_or_else(|| AvaError::ValidationError("steps must be an array".into()))?;

    if steps_arr.is_empty() {
        return Err(AvaError::ValidationError(
            "steps array must not be empty".into(),
        ));
    }

    let mut steps = Vec::with_capacity(steps_arr.len());
    for (i, entry) in steps_arr.iter().enumerate() {
        let id = entry
            .get("id")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError(format!("steps[{i}]: missing required field: id"))
            })?
            .to_string();

        let description = entry
            .get("description")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError(format!(
                    "steps[{i}]: missing required field: description"
                ))
            })?
            .to_string();

        let action_str = entry.get("action").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError(format!("steps[{i}]: missing required field: action"))
        })?;

        let action = parse_action(action_str)
            .map_err(|e| AvaError::ValidationError(format!("steps[{i}]: {e}")))?;

        let files: Vec<String> = entry
            .get("files")
            .and_then(Value::as_array)
            .map(|arr| {
                arr.iter()
                    .filter_map(Value::as_str)
                    .map(String::from)
                    .collect()
            })
            .unwrap_or_default();

        let depends_on: Vec<String> = entry
            .get("depends_on")
            .and_then(Value::as_array)
            .map(|arr| {
                arr.iter()
                    .filter_map(Value::as_str)
                    .map(String::from)
                    .collect()
            })
            .unwrap_or_default();

        steps.push(PlanStep {
            id,
            description,
            files,
            action,
            depends_on,
        });
    }

    let estimated_turns = args
        .get("estimated_turns")
        .and_then(Value::as_u64)
        .map(|v| v as u32);

    let codename = args
        .get("codename")
        .and_then(Value::as_str)
        .map(String::from)
        .or_else(|| Some(auto_codename(&summary)));

    Ok(Plan {
        steps,
        summary,
        estimated_turns,
        codename,
    })
}

/// Generate a short codename from a plan summary.
fn auto_codename(summary: &str) -> String {
    const STOP_WORDS: &[&str] = &[
        "the", "a", "an", "and", "or", "to", "in", "for", "of", "with", "on", "at", "by", "from",
        "is", "it", "this", "that",
    ];
    let words: Vec<String> = summary
        .split_whitespace()
        .filter(|w| !STOP_WORDS.contains(&w.to_lowercase().as_str()))
        .take(3)
        .map(|w| {
            let mut c = w.chars();
            match c.next() {
                Some(first) => {
                    let mut s = first.to_uppercase().to_string();
                    for ch in c {
                        s.push(ch.to_lowercase().next().unwrap_or(ch));
                    }
                    s
                }
                None => String::new(),
            }
        })
        .collect();
    if words.is_empty() {
        "Plan".to_string()
    } else {
        words.join("-")
    }
}

/// Generate a filesystem-safe slug from a summary string.
fn slugify(summary: &str) -> String {
    summary
        .to_lowercase()
        .chars()
        .map(|c| if c.is_alphanumeric() { c } else { '-' })
        .collect::<String>()
        .split('-')
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join("-")
        .chars()
        .take(60)
        .collect()
}

/// Format a plan as human-readable Markdown.
fn format_plan_as_markdown(plan: &Plan) -> String {
    let mut md = String::new();
    md.push_str("---\n");
    if let Some(codename) = &plan.codename {
        md.push_str(&format!("codename: {codename}\n"));
    }
    md.push_str(&format!(
        "summary: \"{}\"\n",
        plan.summary.replace('"', "\\\"")
    ));
    if let Some(turns) = plan.estimated_turns {
        md.push_str(&format!("estimated_turns: {turns}\n"));
    }
    md.push_str(&format!("created: {}\n", chrono::Utc::now().to_rfc3339()));
    md.push_str("---\n\n");
    if let Some(codename) = &plan.codename {
        md.push_str(&format!("# {} — {}\n\n", codename, plan.summary));
    } else {
        md.push_str(&format!("# {}\n\n", plan.summary));
    }
    md.push_str("## Steps\n\n");
    for (i, step) in plan.steps.iter().enumerate() {
        md.push_str(&format!(
            "### {}. {} `[{}]`\n\n",
            i + 1,
            step.description,
            step.action
        ));
        if !step.files.is_empty() {
            md.push_str("**Files:**\n");
            for file in &step.files {
                md.push_str(&format!("- `{file}`\n"));
            }
            md.push('\n');
        }
        if !step.depends_on.is_empty() {
            md.push_str(&format!(
                "**Depends on:** {}\n\n",
                step.depends_on.join(", ")
            ));
        }
    }
    md
}

/// Persist a plan to `.ava/plans/` as Markdown.
fn persist_plan(plan: &Plan) -> Option<String> {
    let timestamp = chrono::Utc::now().format("%Y%m%d-%H%M%S");
    let slug = slugify(&plan.summary);
    let filename = format!("{timestamp}-{slug}.md");

    let plans_dir = std::path::PathBuf::from(".ava/plans");
    if let Err(e) = std::fs::create_dir_all(&plans_dir) {
        tracing::warn!("failed to create .ava/plans/ directory: {e}");
        return None;
    }

    let path = plans_dir.join(&filename);
    let md = format_plan_as_markdown(plan);
    if let Err(e) = std::fs::write(&path, &md) {
        tracing::warn!("failed to write plan to {}: {e}", path.display());
        return None;
    }
    tracing::info!("plan saved to {}", path.display());
    Some(path.display().to_string())
}

/// Format a plan decision into a human-readable tool result string.
fn format_decision(decision: &PlanDecision, saved_path: Option<&str>) -> String {
    let mut out = String::new();

    match decision {
        PlanDecision::Approved => {
            out.push_str("Plan APPROVED by user. Proceed with execution.\n");
        }
        PlanDecision::Rejected { feedback } => {
            out.push_str("Plan REJECTED by user.\n");
            if !feedback.is_empty() {
                out.push_str(&format!("Feedback: {feedback}\n"));
            }
            out.push_str(
                "You should revise your approach based on the feedback and propose a new plan.\n",
            );
        }
        PlanDecision::Modified { plan, feedback } => {
            out.push_str("Plan MODIFIED by user. Use the updated plan below.\n");
            if !feedback.is_empty() {
                out.push_str(&format!("Feedback: {feedback}\n"));
            }
            out.push_str(&format!(
                "Updated plan:\n{}",
                serde_json::to_string_pretty(plan).unwrap_or_else(|_| "{}".to_string())
            ));
        }
    }

    if let Some(path) = saved_path {
        out.push_str(&format!("\nPlan saved to: {path}"));
    }

    out
}

#[async_trait]
impl Tool for PlanTool {
    fn name(&self) -> &str {
        "plan"
    }

    fn description(&self) -> &str {
        "Propose a structured execution plan for user review before proceeding. \
         Use this when tackling multi-step tasks to align with the user on approach, \
         scope, and order of operations. The user can approve, reject (with feedback), \
         or modify the plan. Wait for approval before executing."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["steps", "summary"],
            "properties": {
                "steps": {
                    "type": "array",
                    "description": "Ordered list of plan steps",
                    "items": {
                        "type": "object",
                        "required": ["id", "description", "action"],
                        "properties": {
                            "id": {
                                "type": "string",
                                "description": "Unique step identifier (e.g. '1', '2a')"
                            },
                            "description": {
                                "type": "string",
                                "description": "What this step does"
                            },
                            "files": {
                                "type": "array",
                                "items": { "type": "string" },
                                "description": "Files this step will read or modify"
                            },
                            "action": {
                                "type": "string",
                                "enum": ["research", "implement", "test", "review"],
                                "description": "The kind of work: research (read/understand), implement (write/modify code), test (write/run tests), review (check correctness)"
                            },
                            "depends_on": {
                                "type": "array",
                                "items": { "type": "string" },
                                "description": "IDs of steps that must complete first"
                            }
                        }
                    }
                },
                "summary": {
                    "type": "string",
                    "description": "Short summary of the entire plan (1-2 sentences)"
                },
                "estimated_turns": {
                    "type": "integer",
                    "description": "Estimated number of agent turns to complete the plan"
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        tracing::debug!(tool = "plan", "executing plan tool");

        let plan = parse_plan(&args)?;

        // Store in shared state so the TUI can display it
        self.state.set(plan.clone());

        // Persist to disk
        let saved_path = persist_plan(&plan);

        let (reply_tx, reply_rx) = oneshot::channel();

        self.bridge
            .tx
            .send(PlanRequest {
                plan,
                reply: reply_tx,
            })
            .map_err(|_| {
                AvaError::ToolError(
                    "Failed to send plan to UI — the TUI may not be running".to_string(),
                )
            })?;

        // Wait for the user's decision with a 10-minute timeout (plans need review time)
        let decision = tokio::time::timeout(std::time::Duration::from_secs(600), reply_rx)
            .await
            .map_err(|_| {
                AvaError::TimeoutError("User did not respond to plan within 10 minutes".to_string())
            })?
            .map_err(|_| {
                AvaError::ToolError(
                    "Plan review was not completed — the UI channel was closed".to_string(),
                )
            })?;

        // If user modified the plan, update shared state and persist the modified version
        if let PlanDecision::Modified { ref plan, .. } = decision {
            self.state.set(plan.clone());
            persist_plan(plan);
        }

        let content = format_decision(&decision, saved_path.as_deref());

        Ok(ToolResult {
            call_id: String::new(),
            content,
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn plan_tool_metadata() {
        let (bridge, _rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state);
        assert_eq!(tool.name(), "plan");
        assert!(!tool.description().is_empty());
        let params = tool.parameters();
        let required = params["required"].as_array().unwrap();
        assert!(required.contains(&json!("steps")));
        assert!(required.contains(&json!("summary")));
    }

    #[tokio::test]
    async fn plan_tool_approved() {
        let (bridge, mut rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state.clone());

        let handle = tokio::spawn(async move {
            let req = rx.recv().await.unwrap();
            assert_eq!(req.plan.summary, "Refactor auth module");
            assert_eq!(req.plan.steps.len(), 2);
            assert_eq!(req.plan.steps[0].action, PlanAction::Research);
            assert_eq!(req.plan.steps[1].depends_on, vec!["1"]);
            req.reply.send(PlanDecision::Approved).unwrap();
        });

        let result = tool
            .execute(json!({
                "summary": "Refactor auth module",
                "estimated_turns": 15,
                "steps": [
                    {
                        "id": "1",
                        "description": "Read authentication module",
                        "files": ["src/auth/mod.rs"],
                        "action": "research",
                        "depends_on": []
                    },
                    {
                        "id": "2",
                        "description": "Refactor JWT middleware",
                        "files": ["src/auth/jwt.rs"],
                        "action": "implement",
                        "depends_on": ["1"]
                    }
                ]
            }))
            .await
            .unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("APPROVED"));

        // Verify state was updated
        let plan = state.get().unwrap();
        assert_eq!(plan.summary, "Refactor auth module");

        handle.await.unwrap();
    }

    #[tokio::test]
    async fn plan_tool_rejected() {
        let (bridge, mut rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state);

        let handle = tokio::spawn(async move {
            let req = rx.recv().await.unwrap();
            req.reply
                .send(PlanDecision::Rejected {
                    feedback: "Add tests first".into(),
                })
                .unwrap();
        });

        let result = tool
            .execute(json!({
                "summary": "Quick fix",
                "steps": [
                    {
                        "id": "1",
                        "description": "Fix the bug",
                        "action": "implement"
                    }
                ]
            }))
            .await
            .unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("REJECTED"));
        assert!(result.content.contains("Add tests first"));

        handle.await.unwrap();
    }

    #[tokio::test]
    async fn plan_tool_modified() {
        let (bridge, mut rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state.clone());

        let handle = tokio::spawn(async move {
            let req = rx.recv().await.unwrap();
            let modified_plan = Plan {
                steps: vec![
                    PlanStep {
                        id: "1".into(),
                        description: "Write tests first".into(),
                        files: vec!["tests/auth.rs".into()],
                        action: PlanAction::Test,
                        depends_on: vec![],
                    },
                    PlanStep {
                        id: "2".into(),
                        description: "Then implement".into(),
                        files: vec!["src/auth.rs".into()],
                        action: PlanAction::Implement,
                        depends_on: vec!["1".into()],
                    },
                ],
                summary: "Implement auth (test-first)".into(),
                estimated_turns: None,
                codename: None,
            };
            req.reply
                .send(PlanDecision::Modified {
                    plan: modified_plan,
                    feedback: "Tests first please".into(),
                })
                .unwrap();
        });

        let result = tool
            .execute(json!({
                "summary": "Implement auth",
                "steps": [
                    {
                        "id": "1",
                        "description": "Implement auth",
                        "action": "implement"
                    }
                ]
            }))
            .await
            .unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("MODIFIED"));
        assert!(result.content.contains("Tests first please"));

        // State should be updated to modified plan
        let plan = state.get().unwrap();
        assert_eq!(plan.steps.len(), 2);
        assert_eq!(plan.steps[0].description, "Write tests first");

        handle.await.unwrap();
    }

    #[tokio::test]
    async fn plan_tool_missing_summary_errors() {
        let (bridge, _rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state);
        let result = tool
            .execute(json!({
                "steps": [{"id": "1", "description": "x", "action": "research"}]
            }))
            .await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn plan_tool_missing_steps_errors() {
        let (bridge, _rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state);
        let result = tool.execute(json!({"summary": "No steps"})).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn plan_tool_empty_steps_errors() {
        let (bridge, _rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state);
        let result = tool.execute(json!({"summary": "Empty", "steps": []})).await;
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("must not be empty"));
    }

    #[tokio::test]
    async fn plan_tool_invalid_action_errors() {
        let (bridge, _rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state);
        let result = tool
            .execute(json!({
                "summary": "Bad action",
                "steps": [{"id": "1", "description": "x", "action": "deploy"}]
            }))
            .await;
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("invalid action"));
    }

    #[tokio::test]
    async fn plan_tool_missing_step_id_errors() {
        let (bridge, _rx) = PlanBridge::new();
        let state = PlanState::new();
        let tool = PlanTool::new(bridge, state);
        let result = tool
            .execute(json!({
                "summary": "No id",
                "steps": [{"description": "x", "action": "research"}]
            }))
            .await;
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("missing required field: id"));
    }

    #[test]
    fn slugify_basic() {
        assert_eq!(slugify("Refactor auth module"), "refactor-auth-module");
        assert_eq!(slugify("Fix: JWT   parsing"), "fix-jwt-parsing");
        assert_eq!(slugify(""), "");
    }

    #[test]
    fn slugify_truncates_long_strings() {
        let long = "a".repeat(100);
        let slug = slugify(&long);
        assert!(slug.len() <= 60);
    }

    #[test]
    fn parse_plan_full() {
        let args = json!({
            "summary": "Test plan",
            "estimated_turns": 10,
            "steps": [
                {
                    "id": "1",
                    "description": "Step one",
                    "files": ["a.rs", "b.rs"],
                    "action": "research",
                    "depends_on": []
                },
                {
                    "id": "2",
                    "description": "Step two",
                    "action": "implement",
                    "depends_on": ["1"]
                }
            ]
        });

        let plan = parse_plan(&args).unwrap();
        assert_eq!(plan.summary, "Test plan");
        assert_eq!(plan.estimated_turns, Some(10));
        assert_eq!(plan.steps.len(), 2);
        assert_eq!(plan.steps[0].files, vec!["a.rs", "b.rs"]);
        assert_eq!(plan.steps[1].depends_on, vec!["1"]);
        assert!(plan.steps[1].files.is_empty());
    }

    #[test]
    fn parse_plan_minimal() {
        let args = json!({
            "summary": "Minimal",
            "steps": [{"id": "1", "description": "Do it", "action": "implement"}]
        });

        let plan = parse_plan(&args).unwrap();
        assert_eq!(plan.estimated_turns, None);
        assert!(plan.steps[0].files.is_empty());
        assert!(plan.steps[0].depends_on.is_empty());
    }

    #[test]
    fn format_decision_approved() {
        let out = format_decision(&PlanDecision::Approved, Some(".ava/plans/test.json"));
        assert!(out.contains("APPROVED"));
        assert!(out.contains(".ava/plans/test.json"));
    }

    #[test]
    fn format_decision_rejected_with_feedback() {
        let out = format_decision(
            &PlanDecision::Rejected {
                feedback: "Too complex".into(),
            },
            None,
        );
        assert!(out.contains("REJECTED"));
        assert!(out.contains("Too complex"));
        assert!(out.contains("revise"));
    }

    #[test]
    fn format_decision_rejected_empty_feedback() {
        let out = format_decision(
            &PlanDecision::Rejected {
                feedback: String::new(),
            },
            None,
        );
        assert!(out.contains("REJECTED"));
        assert!(!out.contains("Feedback:"));
    }
}
