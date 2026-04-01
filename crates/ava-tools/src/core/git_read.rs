use std::time::Duration;

use async_trait::async_trait;
use ava_permissions::classifier::is_safe_git_command;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};
use tokio::process::Command;

use crate::registry::Tool;

const DEFAULT_TIMEOUT_MS: u64 = 120_000;

/// Restricted git tool that only allows safe, read-only git commands.
///
/// SEC: Uses direct process execution (`Command::new("git")`) rather than
/// shell invocation (`sh -c "git ..."`) to prevent shell injection.
pub struct GitReadTool;

impl Default for GitReadTool {
    fn default() -> Self {
        Self
    }
}

impl GitReadTool {
    pub fn new() -> Self {
        Self
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

    fn search_hint(&self) -> &str {
        "git status log diff branch commit"
    }

    fn activity_description(&self, args: &serde_json::Value) -> Option<String> {
        let cmd = args.get("command").and_then(serde_json::Value::as_str)?;
        let subcommand = cmd.split_whitespace().next().unwrap_or(cmd);
        Some(format!("Reading git {subcommand}"))
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

        tracing::debug!(tool = "git", %subcommand, "executing git tool");

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

        // SEC: Use direct process execution to avoid shell injection.
        // The subcommand is split on whitespace and passed as args to `git` directly,
        // rather than going through `sh -c "git ..."`.
        let args: Vec<&str> = subcommand.split_whitespace().collect();
        let output = tokio::time::timeout(
            Duration::from_millis(DEFAULT_TIMEOUT_MS),
            Command::new("git").args(&args).output(),
        )
        .await
        .map_err(|_| AvaError::PlatformError("git command timed out".to_string()))?
        .map_err(|e| AvaError::PlatformError(format!("failed to execute git: {e}")))?;

        let stdout = String::from_utf8_lossy(&output.stdout);
        let stderr = String::from_utf8_lossy(&output.stderr);
        let exit_code = output.status.code().unwrap_or(-1);

        let rendered = format!("stdout:\n{stdout}\n\nstderr:\n{stderr}\n\nexit_code: {exit_code}");

        Ok(ToolResult {
            call_id: String::new(),
            content: rendered,
            is_error: exit_code != 0,
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
