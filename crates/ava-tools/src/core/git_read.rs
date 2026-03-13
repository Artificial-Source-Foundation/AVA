use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_permissions::classifier::is_safe_git_command;
use ava_platform::{ExecuteOptions, Platform};
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

const DEFAULT_TIMEOUT_MS: u64 = 120_000;

/// Restricted git tool that only allows safe, read-only git commands.
pub struct GitReadTool {
    platform: Arc<dyn Platform>,
}

impl GitReadTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for GitReadTool {
    fn name(&self) -> &str {
        "git"
    }

    fn description(&self) -> &str {
        "Run read-only git commands (status, log, diff, show, blame, etc.)"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["command"],
            "properties": {
                "command": {
                    "type": "string",
                    "description": "Git subcommand and arguments (e.g. 'diff --staged', 'log --oneline -10'). The 'git' prefix is added automatically."
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let subcommand = args.get("command").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: command".to_string())
        })?;

        let full_command = format!("git {subcommand}");
        let lower = full_command.to_ascii_lowercase();

        if contains_shell_metacharacters(subcommand) {
            return Err(AvaError::PermissionDenied(
                "git command contains disallowed shell metacharacters".to_string(),
            ));
        }

        if !is_safe_git_command(&lower) {
            return Err(AvaError::PermissionDenied(format!(
                "git command not allowed: '{subcommand}'. Only read-only git commands are permitted (status, log, diff, show, blame, branch, tag, remote, rev-parse, ls-files, describe, shortlog, stash list)."
            )));
        }

        if contains_mutating_git_flags(&lower) {
            return Err(AvaError::PermissionDenied(format!(
                "git command not allowed: '{subcommand}'. Mutating git flags/verbs are blocked in read-only mode."
            )));
        }

        let output = self
            .platform
            .execute_with_options(
                &full_command,
                ExecuteOptions {
                    timeout: Some(Duration::from_millis(DEFAULT_TIMEOUT_MS)),
                    working_dir: None,
                    env_vars: Vec::new(),
                },
            )
            .await?;

        let rendered = format!(
            "stdout:\n{}\n\nstderr:\n{}\n\nexit_code: {}",
            output.stdout, output.stderr, output.exit_code
        );

        Ok(ToolResult {
            call_id: String::new(),
            content: rendered,
            is_error: output.exit_code != 0,
        })
    }
}

fn contains_shell_metacharacters(command: &str) -> bool {
    const DISALLOWED: [char; 10] = [';', '|', '&', '$', '<', '>', '`', '(', ')', '\n'];
    command.chars().any(|ch| DISALLOWED.contains(&ch))
}

fn contains_mutating_git_flags(lower: &str) -> bool {
    const MUTATING_PATTERNS: [&str; 20] = [
        " branch -d",
        " branch --delete",
        " branch -m",
        " branch --move",
        " tag -d",
        " tag --delete",
        " tag -a",
        " tag -s",
        " tag -m",
        " tag -f",
        " tag --force",
        " remote add ",
        " remote remove ",
        " remote rename ",
        " remote set-url ",
        " remote set-head ",
        " remote prune ",
        " remote update ",
        " stash push",
        " stash pop",
    ];

    MUTATING_PATTERNS
        .iter()
        .any(|pattern| lower.contains(pattern))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_unsafe_git_commands() {
        // Verify the safety check works for common unsafe commands
        assert!(!is_safe_git_command("git push origin main"));
        assert!(!is_safe_git_command("git commit -m 'test'"));
        assert!(!is_safe_git_command("git reset --hard"));
        assert!(!is_safe_git_command("git checkout ."));
    }

    #[test]
    fn allows_safe_git_commands() {
        assert!(is_safe_git_command("git diff --staged"));
        assert!(is_safe_git_command("git log --oneline -10"));
        assert!(is_safe_git_command("git status"));
        assert!(is_safe_git_command("git show HEAD"));
        assert!(is_safe_git_command("git blame src/main.rs"));
        assert!(is_safe_git_command("git branch -a"));
    }

    #[test]
    fn rejects_shell_metacharacters() {
        assert!(contains_shell_metacharacters("status; rm -rf /"));
        assert!(contains_shell_metacharacters("log && echo hi"));
        assert!(!contains_shell_metacharacters("status --short"));
    }

    #[test]
    fn rejects_mutating_git_flags() {
        assert!(contains_mutating_git_flags("git branch -d main"));
        assert!(contains_mutating_git_flags("git remote add origin foo"));
        assert!(!contains_mutating_git_flags("git branch -a"));
    }
}
