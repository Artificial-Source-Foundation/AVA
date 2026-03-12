use std::path::PathBuf;
use std::time::Instant;

use ava_tools::monitor::{hash_arguments, ToolExecution};
use ava_types::{Message, Role, Session, ToolCall, ToolResult};

use super::AgentLoop;
use crate::instructions::contextual_instructions_for_file;
use crate::stuck::StuckDetector;

const MAX_TOOL_RESULT_BYTES: usize = 50_000;

/// Tools that are safe to execute concurrently (no side effects).
pub const READ_ONLY_TOOLS: &[&str] = &[
    "read",
    "glob",
    "grep",
    "hover",
    "references",
    "definition",
    "web_fetch",
    "todo_read",
];

pub(super) fn truncate_tool_result(result: &mut ToolResult) {
    if result.content.len() > MAX_TOOL_RESULT_BYTES {
        let original_len = result.content.len();
        let mut truncate_at = MAX_TOOL_RESULT_BYTES;
        while truncate_at > 0 && !result.content.is_char_boundary(truncate_at) {
            truncate_at -= 1;
        }
        result.content.truncate(truncate_at);
        result.content.push_str(&format!(
            "\n\n[truncated — showing first {} bytes of {} total]",
            truncate_at, original_len
        ));
    }
}

impl AgentLoop {
    /// Execute tool calls, record in detector's monitor, and collect results.
    /// Read-only tools are executed concurrently; write tools sequentially.
    pub(super) async fn execute_tool_calls_tracked(
        &self,
        tool_calls: &[ToolCall],
        detector: &mut StuckDetector,
    ) -> Vec<ToolResult> {
        // Separate indices into read-only and write groups
        let mut read_indices = Vec::new();
        let mut write_indices = Vec::new();
        for (i, tc) in tool_calls.iter().enumerate() {
            if tc.name == "attempt_completion" {
                continue;
            }
            if READ_ONLY_TOOLS.contains(&tc.name.as_str()) {
                read_indices.push(i);
            } else {
                write_indices.push(i);
            }
        }

        let mut all_results: Vec<Option<(ToolResult, ToolExecution)>> =
            vec![None; tool_calls.len()];

        // Execute read-only tools concurrently
        if !read_indices.is_empty() {
            let futs: Vec<_> = read_indices
                .iter()
                .map(|&i| self.execute_tool_call_timed(&tool_calls[i]))
                .collect();
            let read_results = futures::future::join_all(futs).await;
            for (idx_pos, &i) in read_indices.iter().enumerate() {
                all_results[i] = Some(read_results[idx_pos].clone());
            }
        }

        // Execute write tools sequentially
        for &i in &write_indices {
            let result = self.execute_tool_call_timed(&tool_calls[i]).await;
            all_results[i] = Some(result);
        }

        // Collect in original order, recording executions
        let mut results = Vec::new();
        for slot in all_results.into_iter().flatten() {
            let (result, execution) = slot;
            detector.tool_monitor_mut().record(execution);
            results.push(result);
        }
        results
    }

    /// Execute a tool call and return both the result and a timed execution record.
    /// In plan mode, write/edit tools are restricted to `.ava/plans/*.md` paths.
    pub(super) async fn execute_tool_call_timed(
        &self,
        tool_call: &ToolCall,
    ) -> (ToolResult, ToolExecution) {
        // Plan mode: block write/edit/bash to non-plan paths
        if self.config.plan_mode {
            if let Some(rejection) = check_plan_mode_tool(tool_call) {
                let result = ToolResult {
                    call_id: tool_call.id.clone(),
                    content: rejection,
                    is_error: true,
                };
                let execution = ToolExecution {
                    tool_name: tool_call.name.clone(),
                    arguments_hash: hash_arguments(&tool_call.arguments),
                    success: false,
                    duration: std::time::Duration::ZERO,
                    timestamp: Instant::now(),
                };
                return (result, execution);
            }
        }

        let start = Instant::now();
        let mut result = match self.tools.execute(tool_call.clone()).await {
            Ok(result) => result,
            Err(error) => ToolResult {
                call_id: tool_call.id.clone(),
                content: error.to_string(),
                is_error: true,
            },
        };
        let duration = start.elapsed();
        let execution = ToolExecution {
            tool_name: tool_call.name.clone(),
            arguments_hash: hash_arguments(&tool_call.arguments),
            success: !result.is_error,
            duration,
            timestamp: start,
        };
        truncate_tool_result(&mut result);

        // Append contextual AGENTS.md instructions for read tool results
        if tool_call.name == "read" && !result.is_error {
            if let Some(path_str) = tool_call.arguments.get("path").and_then(|v| v.as_str()) {
                let file_path = PathBuf::from(path_str);
                let project_root = std::env::current_dir().unwrap_or_default();
                if let Some(instructions) = contextual_instructions_for_file(&file_path, &project_root) {
                    result.content.push_str("\n\n---\n");
                    result.content.push_str(&instructions);
                }
            }
        }

        (result, execution)
    }

    /// Add tool results to context and session.
    pub(super) fn add_tool_results(
        &mut self,
        tool_calls: &[ToolCall],
        results: &[ToolResult],
        session: &mut Session,
    ) {
        let mut ri = 0;
        for tool_call in tool_calls {
            if tool_call.name == "attempt_completion" {
                continue;
            }
            if let Some(result) = results.get(ri) {
                let tool_message = Message::new(Role::Tool, result.content.clone())
                    .with_tool_call_id(&tool_call.id)
                    .with_tool_results(vec![result.clone()]);
                self.context.add_message(tool_message.clone());
                session.add_message(tool_message);
            }
            ri += 1;
        }
    }
}

/// Tools that can modify files.
const WRITE_TOOLS: &[&str] = &["write", "edit", "multiedit", "apply_patch"];

/// Check if a tool call is allowed in Plan mode.
/// Returns `Some(error_message)` if the tool is blocked, `None` if allowed.
pub fn check_plan_mode_tool(tool_call: &ToolCall) -> Option<String> {
    let name = tool_call.name.as_str();

    // Block bash — it can modify anything
    if name == "bash" {
        return Some(
            "Plan mode: bash is not available. Switch to Code mode to run commands.".to_string(),
        );
    }

    // Allow read-only tools unconditionally
    if READ_ONLY_TOOLS.contains(&name) || name == "codebase_search" || name == "todo_read" || name == "todo_write" {
        return None;
    }

    // For write tools, check path
    if WRITE_TOOLS.contains(&name) {
        let path_str = tool_call
            .arguments
            .get("path")
            .or_else(|| tool_call.arguments.get("file_path"))
            .and_then(|v| v.as_str())
            .unwrap_or("");

        if is_plan_path(path_str) {
            // Auto-create .ava/plans/ directory if needed
            let plan_dir = if std::path::Path::new(path_str).is_absolute() {
                // Extract the .ava/plans/ portion from absolute path
                PathBuf::from(path_str).parent().map(|p| p.to_path_buf())
            } else {
                let cwd = std::env::current_dir().unwrap_or_default();
                Some(cwd.join(PathBuf::from(path_str).parent().unwrap_or(std::path::Path::new(""))))
            };
            if let Some(dir) = plan_dir {
                let _ = std::fs::create_dir_all(&dir);
            }
            return None; // Allowed
        }

        return Some(format!(
            "Plan mode: cannot write to '{path_str}'. In Plan mode, you can only write to .ava/plans/*.md files. \
             Switch to Code mode to modify source files."
        ));
    }

    // Allow attempt_completion and other non-write tools
    None
}

/// Check if a path is within .ava/plans/ and has a .md extension.
pub fn is_plan_path(path: &str) -> bool {
    let normalized = path.replace('\\', "/");
    // Must end with .md
    if !normalized.ends_with(".md") {
        return false;
    }
    // Must be under .ava/plans/
    normalized.contains(".ava/plans/")
}
