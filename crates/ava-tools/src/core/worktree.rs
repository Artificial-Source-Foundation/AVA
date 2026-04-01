//! Git worktree tools for isolated branch workspaces.
//!
//! These tools allow the agent to create and manage git worktrees for
//! isolated work on branches without disturbing the main working directory.

use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

/// Tool to create a git worktree for isolated branch work.
pub struct EnterWorktreeTool;

impl EnterWorktreeTool {
    pub fn new() -> Self {
        Self
    }
}

impl Default for EnterWorktreeTool {
    fn default() -> Self {
        Self::new()
    }
}

#[async_trait]
impl Tool for EnterWorktreeTool {
    fn name(&self) -> &str {
        "enter_worktree"
    }

    fn description(&self) -> &str {
        "Create a git worktree for isolated branch work. Returns the worktree path. \
         search_hint: git worktree isolate branch workspace"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {
                "branch_name": {
                    "type": "string",
                    "description": "Branch name for the worktree. If omitted, a name is auto-generated."
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let branch_name = args
            .get("branch_name")
            .and_then(Value::as_str)
            .map(|s| s.to_string())
            .unwrap_or_else(|| format!("worktree-{}", chrono::Utc::now().format("%Y%m%d-%H%M%S")));

        // Validate branch name (no spaces, no special chars that break git)
        if branch_name.contains(' ') || branch_name.contains("..") || branch_name.starts_with('-') {
            return Err(AvaError::ValidationError(format!(
                "Invalid branch name: {branch_name}"
            )));
        }

        let worktree_path = format!(".ava/worktrees/{branch_name}");

        let output = tokio::process::Command::new("git")
            .args(["worktree", "add", &worktree_path, "-b", &branch_name])
            .output()
            .await
            .map_err(|e| AvaError::PlatformError(format!("failed to run git: {e}")))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("Failed to create worktree: {stderr}"),
                is_error: true,
            });
        }

        // Resolve absolute path
        let abs_path = std::env::current_dir()
            .map(|cwd| cwd.join(&worktree_path).display().to_string())
            .unwrap_or_else(|_| worktree_path.clone());

        Ok(ToolResult {
            call_id: String::new(),
            content: format!(
                "Created worktree at {abs_path} on branch '{branch_name}'. \
                 Use exit_worktree when done."
            ),
            is_error: false,
        })
    }
}

/// Tool to exit and optionally remove a git worktree.
pub struct ExitWorktreeTool;

impl ExitWorktreeTool {
    pub fn new() -> Self {
        Self
    }
}

impl Default for ExitWorktreeTool {
    fn default() -> Self {
        Self::new()
    }
}

#[async_trait]
impl Tool for ExitWorktreeTool {
    fn name(&self) -> &str {
        "exit_worktree"
    }

    fn description(&self) -> &str {
        "Exit a git worktree, optionally discarding changes. \
         search_hint: exit leave worktree return"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["worktree_path"],
            "properties": {
                "worktree_path": {
                    "type": "string",
                    "description": "Path to the worktree to exit"
                },
                "keep_changes": {
                    "type": "boolean",
                    "description": "If true, keep the branch for later merge. If false, remove worktree and delete branch.",
                    "default": false
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let worktree_path = args
            .get("worktree_path")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: worktree_path".to_string())
            })?;

        let keep_changes = args
            .get("keep_changes")
            .and_then(Value::as_bool)
            .unwrap_or(false);

        if keep_changes {
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!(
                    "Keeping worktree branch at {worktree_path} for later merge. \
                     The worktree remains on disk."
                ),
                is_error: false,
            });
        }

        // Remove worktree
        let output = tokio::process::Command::new("git")
            .args(["worktree", "remove", worktree_path, "--force"])
            .output()
            .await
            .map_err(|e| AvaError::PlatformError(format!("failed to run git: {e}")))?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("Failed to remove worktree: {stderr}"),
                is_error: true,
            });
        }

        // Try to extract branch name from path for cleanup
        let branch_name = std::path::Path::new(worktree_path)
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("");

        if !branch_name.is_empty() {
            // Best-effort branch deletion — ignore errors
            let _ = tokio::process::Command::new("git")
                .args(["branch", "-D", branch_name])
                .output()
                .await;
        }

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Removed worktree at {worktree_path} and deleted branch."),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn enter_worktree_tool_metadata() {
        let tool = EnterWorktreeTool::new();
        assert_eq!(tool.name(), "enter_worktree");
        assert!(tool.description().contains("worktree"));

        let params = tool.parameters();
        let props = params["properties"].as_object().unwrap();
        assert!(props.contains_key("branch_name"));
    }

    #[test]
    fn exit_worktree_tool_metadata() {
        let tool = ExitWorktreeTool::new();
        assert_eq!(tool.name(), "exit_worktree");
        assert!(tool.description().contains("worktree"));

        let params = tool.parameters();
        let required = params["required"].as_array().unwrap();
        assert!(required.iter().any(|v| v.as_str() == Some("worktree_path")));
        let props = params["properties"].as_object().unwrap();
        assert!(props.contains_key("worktree_path"));
        assert!(props.contains_key("keep_changes"));
    }

    #[test]
    fn enter_worktree_parameter_schema() {
        let tool = EnterWorktreeTool::new();
        let params = tool.parameters();
        assert_eq!(params["type"], "object");
        assert_eq!(params["properties"]["branch_name"]["type"], "string");
    }

    #[tokio::test]
    async fn enter_worktree_rejects_invalid_branch_name() {
        let tool = EnterWorktreeTool::new();
        let result = tool
            .execute(json!({"branch_name": "bad name with spaces"}))
            .await;
        assert!(result.is_err());

        let result = tool.execute(json!({"branch_name": "-leading-dash"})).await;
        assert!(result.is_err());

        let result = tool.execute(json!({"branch_name": "has..dots"})).await;
        assert!(result.is_err());
    }
}
