use std::path::PathBuf;
use std::time::Instant;

use ava_tools::monitor::{hash_arguments, ToolExecution};
use ava_types::{Message, Role, Session, ToolCall, ToolResult};
use serde_json::{json, Value};
use tracing::debug;

use ava_tools::registry::ToolRegistry;

use super::AgentLoop;
use crate::instructions::contextual_instructions_for_file;

const MAX_TOOL_RESULT_BYTES: usize = 50_000;
const POST_EDIT_VALIDATION_TOOLS: &[&str] = &["edit", "multiedit", "write", "apply_patch"];
const VALIDATION_FAILURE_MARKER: &str = "[post-edit validation status: failed]";

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

/// Pre-validate a tool call's arguments against the tool's JSON Schema parameters.
///
/// Checks that the tool exists in the registry, all required parameters are present,
/// and parameter types match the schema (string, number/integer, boolean, array, object).
/// Returns `Some(error_message)` if validation fails, `None` if valid.
pub(super) fn validate_tool_call(tool_call: &ToolCall, registry: &ToolRegistry) -> Option<String> {
    // Check tool exists
    if !registry.has_tool(&tool_call.name) {
        let available = registry.tool_names().join(", ");
        return Some(format!(
            "Tool '{}' not found. Available tools: {available}",
            tool_call.name
        ));
    }

    let Some(schema) = registry.tool_parameters(&tool_call.name) else {
        return None; // No schema to validate against
    };

    // Arguments must be an object (or null/missing which we treat as empty object).
    // Some providers send arguments as a JSON string that needs parsing.
    let parsed_args;
    let args = match &tool_call.arguments {
        Value::Object(ref map) => map,
        Value::String(s) => {
            // Arguments arrived as a raw JSON string — parse to object
            match serde_json::from_str::<Value>(s) {
                Ok(Value::Object(map)) => {
                    parsed_args = map;
                    &parsed_args
                }
                _ => {
                    return Some(format!(
                        "Tool '{}': arguments string is not a valid JSON object",
                        tool_call.name,
                    ));
                }
            }
        }
        Value::Null => {
            // Check if there are required params — if so, fail
            if let Some(required) = schema.get("required").and_then(|v| v.as_array()) {
                if !required.is_empty() {
                    let names: Vec<&str> = required.iter().filter_map(|v| v.as_str()).collect();
                    return Some(format!(
                        "Tool '{}': arguments must be an object. Missing required parameter(s): {}",
                        tool_call.name,
                        names.join(", ")
                    ));
                }
            }
            return None;
        }
        _ => {
            return Some(format!(
                "Tool '{}': arguments must be a JSON object, got {}",
                tool_call.name,
                value_type_name(&tool_call.arguments)
            ));
        }
    };

    let mut errors = Vec::new();

    // Check required parameters
    if let Some(required) = schema.get("required").and_then(|v| v.as_array()) {
        for req in required {
            if let Some(name) = req.as_str() {
                if !args.contains_key(name) {
                    errors.push(format!("missing required parameter '{name}'"));
                }
            }
        }
    }

    // Check parameter types against schema properties
    if let Some(properties) = schema.get("properties").and_then(|v| v.as_object()) {
        for (key, value) in args {
            if let Some(prop_schema) = properties.get(key) {
                if let Some(expected_type) = prop_schema.get("type").and_then(|v| v.as_str()) {
                    if !value_matches_type(value, expected_type) {
                        errors.push(format!(
                            "parameter '{key}' expected type '{expected_type}', got {}",
                            value_type_name(value)
                        ));
                    }
                }
            }
        }
    }

    if errors.is_empty() {
        None
    } else {
        Some(format!(
            "Tool '{}' call invalid: {}",
            tool_call.name,
            errors.join("; ")
        ))
    }
}

/// Check if a JSON value matches an expected JSON Schema type.
fn value_matches_type(value: &Value, expected: &str) -> bool {
    match expected {
        "string" => value.is_string(),
        "number" => value.is_number(),
        "integer" => value.is_i64() || value.is_u64(),
        "boolean" => value.is_boolean(),
        "array" => value.is_array(),
        "object" => value.is_object(),
        "null" => value.is_null(),
        _ => true, // Unknown type — don't reject
    }
}

/// Return a human-readable type name for a JSON value.
fn value_type_name(value: &Value) -> &'static str {
    match value {
        Value::Null => "null",
        Value::Bool(_) => "boolean",
        Value::Number(_) => "number",
        Value::String(_) => "string",
        Value::Array(_) => "array",
        Value::Object(_) => "object",
    }
}

impl AgentLoop {
    /// Execute a tool call and return both the result and a timed execution record.
    /// In plan mode, write/edit tools are restricted to `.ava/plans/*.md` paths.
    pub(super) async fn execute_tool_call_timed(
        &self,
        tool_call: &ToolCall,
    ) -> (ToolResult, ToolExecution) {
        // Auto-repair misnamed tool calls (e.g. "Read" → "read")
        let mut tool_call = tool_call.clone();
        let repaired = repair_tool_name(&tool_call.name, &self.tools);
        if repaired != tool_call.name {
            tool_call.name = repaired;
        }
        let tool_call = &tool_call;

        // Pre-validate tool call schema (required params, types) before execution
        if let Some(validation_error) = validate_tool_call(tool_call, &self.tools) {
            debug!(tool = %tool_call.name, error = %validation_error, "tool call pre-validation failed");
            let result = ToolResult {
                call_id: tool_call.id.clone(),
                content: validation_error,
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

        // --- Fire ToolBefore plugin hook ---
        if let Some(ref pm) = self.plugin_manager {
            let mut pm = pm.lock().await;
            let responses = pm
                .trigger_hook(
                    ava_plugin::HookEvent::ToolBefore,
                    serde_json::json!({
                        "tool": tool_call.name,
                        "call_id": tool_call.id,
                        "args": tool_call.arguments,
                    }),
                )
                .await;

            // Check for blocks — if any plugin returns an error, skip execution
            for resp in &responses {
                if let Some(err) = &resp.error {
                    let result = ToolResult {
                        call_id: tool_call.id.clone(),
                        content: format!("Blocked by plugin '{}': {}", resp.plugin_name, err),
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
        let tool_failed = result.is_error;
        self.append_post_edit_validation(tool_call, &mut result)
            .await;
        let duration = start.elapsed();
        let execution = ToolExecution {
            tool_name: tool_call.name.clone(),
            arguments_hash: hash_arguments(&tool_call.arguments),
            success: !tool_failed,
            duration,
            timestamp: start,
        };
        truncate_tool_result(&mut result);

        // Append contextual AGENTS.md instructions for read tool results.
        // Only inject if the project is trusted — untrusted projects must not
        // have their AGENTS.md injected as contextual instructions.
        if tool_call.name == "read" && !result.is_error {
            if let Some(path_str) = tool_call.arguments.get("path").and_then(|v| v.as_str()) {
                let file_path = PathBuf::from(path_str);
                let project_root = std::env::current_dir().unwrap_or_default();
                if ava_config::is_project_trusted(&project_root) {
                    if let Some(instructions) =
                        contextual_instructions_for_file(&file_path, &project_root)
                    {
                        result.content.push_str("\n\n---\n");
                        result.content.push_str(&instructions);
                    }
                }
            }
        }

        // --- Fire ToolAfter plugin hook ---
        if let Some(ref pm) = self.plugin_manager {
            let mut pm = pm.lock().await;
            pm.trigger_hook(
                ava_plugin::HookEvent::ToolAfter,
                serde_json::json!({
                    "tool": tool_call.name,
                    "call_id": tool_call.id,
                    "is_error": result.is_error,
                    "duration_ms": duration.as_millis() as u64,
                }),
            )
            .await;
        }

        (result, execution)
    }

    async fn append_post_edit_validation(&self, tool_call: &ToolCall, result: &mut ToolResult) {
        if result.is_error || !POST_EDIT_VALIDATION_TOOLS.contains(&tool_call.name.as_str()) {
            return;
        }

        let Some(config) = self
            .config
            .post_edit_validation
            .as_ref()
            .filter(|config| config.enabled())
        else {
            return;
        };

        let summary = self.run_post_edit_validation(tool_call, config).await;
        if summary.lines.is_empty() {
            return;
        }

        result.content.push_str("\n\n[post-edit validation]\n");
        result.content.push_str(&summary.lines.join("\n"));
        if summary.failed {
            result.content.push('\n');
            result.content.push_str(VALIDATION_FAILURE_MARKER);
        }
    }

    async fn run_post_edit_validation(
        &self,
        tool_call: &ToolCall,
        config: &super::PostEditValidationConfig,
    ) -> ValidationSummary {
        let mut summary = ValidationSummary::default();
        let validation_paths = validation_paths(tool_call);

        if config.lint {
            let lint_paths = if validation_paths.is_empty() {
                vec![None]
            } else {
                validation_paths.iter().cloned().map(Some).collect()
            };

            for path in lint_paths {
                let mut args = serde_json::Map::new();
                if let Some(command) = &config.lint_command {
                    args.insert("command".to_string(), Value::String(command.clone()));
                }
                if let Some(path) = path {
                    args.insert("path".to_string(), Value::String(path));
                }
                summary.push(format_validation_result(
                    "lint",
                    self.run_validation_tool("lint", Value::Object(args)).await,
                ));
            }
        }

        if config.tests {
            let mut args = serde_json::Map::new();
            if let Some(command) = &config.test_command {
                args.insert("command".to_string(), Value::String(command.clone()));
            }
            args.insert("timeout".to_string(), json!(config.test_timeout_secs));
            summary.push(format_validation_result(
                "tests",
                self.run_validation_tool("test_runner", Value::Object(args))
                    .await,
            ));
        }

        summary
    }

    async fn run_validation_tool(
        &self,
        name: &str,
        arguments: Value,
    ) -> Result<ToolResult, String> {
        self.tools
            .execute(ToolCall {
                id: format!("post-edit-validation-{name}"),
                name: name.to_string(),
                arguments,
            })
            .await
            .map_err(|error| error.to_string())
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

#[derive(Default)]
struct ValidationSummary {
    lines: Vec<String>,
    failed: bool,
}

impl ValidationSummary {
    fn push(&mut self, line: ValidationLine) {
        self.failed |= line.failed;
        self.lines.push(line.text);
    }
}

struct ValidationLine {
    text: String,
    failed: bool,
}

pub(super) fn has_validation_failure(result: &ToolResult) -> bool {
    result.content.contains(VALIDATION_FAILURE_MARKER)
}

fn validation_paths(tool_call: &ToolCall) -> Vec<String> {
    if let Some(path) = tool_call
        .arguments
        .get("path")
        .and_then(|value| value.as_str())
        .map(ToOwned::to_owned)
    {
        return vec![path];
    }

    if tool_call.name == "apply_patch" {
        let patch = tool_call
            .arguments
            .get("patch")
            .and_then(|value| value.as_str())
            .unwrap_or_default();
        let strip = tool_call
            .arguments
            .get("strip")
            .and_then(Value::as_u64)
            .unwrap_or(1) as usize;
        return extract_apply_patch_paths(patch, strip);
    }

    Vec::new()
}

fn extract_apply_patch_paths(patch: &str, strip: usize) -> Vec<String> {
    let mut paths = Vec::new();
    let lines: Vec<&str> = patch.lines().collect();

    for window in lines.windows(2) {
        if let [old_line, new_line] = window {
            if old_line.starts_with("--- ") {
                if let Some(raw_path) = new_line.strip_prefix("+++ ") {
                    let path = strip_patch_path(raw_path, strip);
                    if !paths.contains(&path) {
                        paths.push(path);
                    }
                }
            }
        }
    }

    paths
}

fn strip_patch_path(path: &str, strip: usize) -> String {
    if strip == 0 {
        return path.to_string();
    }

    let parts: Vec<&str> = path.splitn(strip + 1, '/').collect();
    if parts.len() > strip {
        parts[strip].to_string()
    } else {
        path.to_string()
    }
}

fn format_validation_result(label: &str, result: Result<ToolResult, String>) -> ValidationLine {
    match result {
        Ok(result) => {
            let text = match label {
                "lint" => format_lint_validation_line(&result),
                "tests" => format_test_validation_line(&result),
                _ => format!("- {label}: completed"),
            };
            ValidationLine {
                text,
                failed: result.is_error,
            }
        }
        Err(error) => ValidationLine {
            text: format!("- {label}: unavailable ({error})"),
            failed: false,
        },
    }
}

fn format_lint_validation_line(result: &ToolResult) -> String {
    let payload = serde_json::from_str::<Value>(&result.content).ok();
    let errors = payload
        .as_ref()
        .and_then(|value| value.get("errors"))
        .and_then(Value::as_u64)
        .unwrap_or(0);
    let warnings = payload
        .as_ref()
        .and_then(|value| value.get("warnings"))
        .and_then(Value::as_u64)
        .unwrap_or(0);

    if result.is_error {
        format!("- lint: failed ({errors} errors, {warnings} warnings)")
    } else {
        format!("- lint: passed ({errors} errors, {warnings} warnings)")
    }
}

fn format_test_validation_line(result: &ToolResult) -> String {
    let payload = serde_json::from_str::<Value>(&result.content).ok();
    let exit_code = payload
        .as_ref()
        .and_then(|value| value.get("exit_code"))
        .and_then(Value::as_i64)
        .unwrap_or_default();

    if result.is_error {
        format!("- tests: failed (exit code {exit_code})")
    } else {
        "- tests: passed".to_string()
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
    if READ_ONLY_TOOLS.contains(&name)
        || name == "codebase_search"
        || name == "todo_read"
        || name == "todo_write"
    {
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
                Some(
                    cwd.join(
                        PathBuf::from(path_str)
                            .parent()
                            .unwrap_or(std::path::Path::new("")),
                    ),
                )
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

/// Attempt to repair a misnamed tool call (e.g. "Read" → "read", "Bash" → "bash").
///
/// Returns the corrected name if a match is found, or the original name if not.
pub fn repair_tool_name(name: &str, registry: &ToolRegistry) -> String {
    // Exact match — no repair needed
    if registry.has_tool(name) {
        return name.to_string();
    }

    // Case-insensitive match against all registered tool names
    let lower = name.to_lowercase();
    for tool_name in registry.tool_names() {
        if tool_name.to_lowercase() == lower {
            tracing::info!("Repaired tool name '{}' → '{}'", name, tool_name);
            return tool_name;
        }
    }

    // No match — return original (will error in execute)
    name.to_string()
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
