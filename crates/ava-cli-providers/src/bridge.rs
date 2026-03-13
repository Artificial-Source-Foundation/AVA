use crate::config::{CLIAgentEvent, CLIAgentResult};
use crate::runner::{CLIAgentRunner, RunOptions};
use ava_types::Result;

/// Agent role for tier-specific configuration.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AgentRole {
    Engineer,
    Reviewer,
    Subagent,
}

/// Execute a task using a CLI agent with tier-appropriate settings.
pub async fn execute_with_cli_agent(
    runner: &CLIAgentRunner,
    task: &str,
    role: AgentRole,
    cwd: &str,
    files: Option<&[String]>,
    event_tx: Option<tokio::sync::mpsc::Sender<CLIAgentEvent>>,
) -> Result<CLIAgentResult> {
    let prompt = build_tier_prompt(task, role, files);

    let options = RunOptions {
        prompt,
        cwd: cwd.to_string(),
        yolo: matches!(role, AgentRole::Engineer),
        allowed_tools: get_tier_tools(runner, role),
        timeout_ms: get_tier_timeout(role),
        ..RunOptions::default()
    };

    if let Some(tx) = event_tx {
        runner.stream(options, tx).await
    } else {
        runner.run(options).await
    }
}

pub(crate) fn build_tier_prompt(task: &str, role: AgentRole, files: Option<&[String]>) -> String {
    let files_ctx = files
        .map(|f| format!("\nRelevant files: {}", f.join(", ")))
        .unwrap_or_default();

    match role {
        AgentRole::Engineer => format!(
            "You are an engineer. Implement the following task:\n\n{task}{files_ctx}\n\n\
             Write clean, tested code. Commit when done."
        ),
        AgentRole::Reviewer => format!(
            "Review these changes for correctness, style, and potential bugs. \
             Run lint and tests to verify.{files_ctx}\n\nTask context: {task}"
        ),
        AgentRole::Subagent => {
            format!("Research the following and report your findings:\n\n{task}{files_ctx}")
        }
    }
}

pub(crate) fn get_tier_tools(runner: &CLIAgentRunner, role: AgentRole) -> Option<Vec<String>> {
    let role_key = match role {
        AgentRole::Engineer => "engineer",
        AgentRole::Reviewer => "reviewer",
        AgentRole::Subagent => "subagent",
    };

    if !runner.config().supports_tool_scoping {
        return None;
    }

    runner
        .config()
        .tier_tool_scopes
        .as_ref()
        .and_then(|scopes| scopes.get(role_key).cloned())
}

pub(crate) fn get_tier_timeout(role: AgentRole) -> Option<u64> {
    match role {
        AgentRole::Engineer => Some(600_000),
        AgentRole::Reviewer => Some(300_000),
        AgentRole::Subagent => Some(120_000),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::{CLIAgentConfig, PromptMode};
    use std::collections::HashMap;

    fn test_runner() -> CLIAgentRunner {
        CLIAgentRunner::new(CLIAgentConfig {
            name: "claude-code".to_string(),
            binary: "claude".to_string(),
            prompt_flag: PromptMode::Flag("-p".to_string()),
            non_interactive_flags: vec![],
            yolo_flags: vec!["--dangerously-skip-permissions".to_string()],
            output_format_flag: Some("--output-format".to_string()),
            allowed_tools_flag: Some("--allowedTools".to_string()),
            cwd_flag: Some("--cwd".to_string()),
            model_flag: Some("--model".to_string()),
            session_flag: None,
            supports_stream_json: true,
            supports_tool_scoping: true,
            tier_tool_scopes: Some(HashMap::from([
                (
                    "engineer".to_string(),
                    vec!["Edit".to_string(), "Write".to_string()],
                ),
                (
                    "reviewer".to_string(),
                    vec!["Read".to_string(), "Grep".to_string()],
                ),
                ("subagent".to_string(), vec!["Read".to_string()]),
            ])),
            version_command: vec!["claude".to_string(), "--version".to_string()],
        })
    }

    #[test]
    fn engineer_prompt_includes_task_and_files() {
        let files = vec!["src/lib.rs".to_string(), "src/main.rs".to_string()];
        let prompt = build_tier_prompt("Add tests", AgentRole::Engineer, Some(&files));
        assert!(prompt.contains("Implement the following task"));
        assert!(prompt.contains("Add tests"));
        assert!(prompt.contains("Relevant files: src/lib.rs, src/main.rs"));
    }

    #[test]
    fn reviewer_prompt_focuses_on_review() {
        let prompt = build_tier_prompt("Refactor parser", AgentRole::Reviewer, None);
        assert!(prompt.contains("Review these changes"));
        assert!(prompt.contains("Run lint and tests"));
    }

    #[test]
    fn subagent_prompt_focuses_on_research() {
        let prompt = build_tier_prompt("How does routing work?", AgentRole::Subagent, None);
        assert!(prompt.contains("Research the following"));
    }

    #[test]
    fn yolo_enabled_only_for_engineer() {
        assert!(matches!(AgentRole::Engineer, AgentRole::Engineer));
        assert!(!matches!(AgentRole::Reviewer, AgentRole::Engineer));
        assert!(!matches!(AgentRole::Subagent, AgentRole::Engineer));
    }

    #[test]
    fn tier_tool_scoping_applies_per_role() {
        let runner = test_runner();
        assert_eq!(
            get_tier_tools(&runner, AgentRole::Engineer),
            Some(vec!["Edit".to_string(), "Write".to_string()])
        );
        assert_eq!(
            get_tier_tools(&runner, AgentRole::Reviewer),
            Some(vec!["Read".to_string(), "Grep".to_string()])
        );
    }

    #[test]
    fn tier_timeouts_are_role_specific() {
        assert_eq!(get_tier_timeout(AgentRole::Engineer), Some(600_000));
        assert_eq!(get_tier_timeout(AgentRole::Reviewer), Some(300_000));
        assert_eq!(get_tier_timeout(AgentRole::Subagent), Some(120_000));
    }
}
