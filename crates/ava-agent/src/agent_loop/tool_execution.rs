use std::collections::BTreeSet;
use std::path::PathBuf;
use std::time::Instant;

use ava_tools::monitor::{hash_arguments, ToolExecution};
use ava_types::{Message, Role, Session, ToolCall, ToolResult};
use futures::StreamExt;
use serde_json::{json, Value};
use tokio::sync::mpsc;
use tracing::{debug, warn};

use ava_tools::registry::ToolRegistry;

use super::repetition::RepetitionDetector;
use super::{AgentEvent, AgentLoop, MAX_TOOLS_PER_TURN};
use crate::instructions::{
    contextual_instructions_for_file_once, matching_rule_instructions_for_file,
};
use crate::stuck::StuckDetector;
use crate::trace::RunEventKind;

const MAX_TOOL_RESULT_BYTES: usize = 50_000;
const MAX_CONCURRENT_READ_ONLY_TOOLS: usize = 8;
const POST_EDIT_VALIDATION_TOOLS: &[&str] = &["edit", "multiedit", "write", "apply_patch"];
const VALIDATION_FAILURE_MARKER: &str = "[post-edit validation status: failed]";
const MAX_TOUCHED_PATHS_FOR_RULES: usize = 20;

fn touched_file_path(tool_call: &ToolCall) -> Option<PathBuf> {
    match tool_call.name.as_str() {
        "read" | "write" | "edit" => tool_call
            .arguments
            .get("path")
            .and_then(|value| value.as_str())
            .map(PathBuf::from),
        _ => None,
    }
}

fn instruction_trigger_paths(tool_call: &ToolCall) -> Vec<PathBuf> {
    match tool_call.name.as_str() {
        "read" | "write" | "edit" => touched_file_path(tool_call).into_iter().collect(),
        _ => Vec::new(),
    }
}

fn normalize_touched_path(
    project_root: &std::path::Path,
    file_path: &std::path::Path,
) -> Option<PathBuf> {
    let candidate = if file_path.is_absolute() {
        file_path.to_path_buf()
    } else {
        project_root.join(file_path)
    };

    let canonical = std::fs::canonicalize(&candidate).ok()?;
    if canonical.starts_with(project_root) {
        Some(canonical)
    } else {
        None
    }
}

fn append_contextual_sections(result: &mut ToolResult, sections: &[String]) {
    if sections.is_empty() {
        return;
    }

    result.content.push_str("\n\n---\n");
    result.content.push_str(&sections.join("\n\n"));
}

fn estimate_tokens(text: &str) -> usize {
    ava_context::count_tokens_default(text)
}

/// Tools whose output may contain external/untrusted content that should be
/// scanned for leaked secrets before being sent to the LLM.
fn is_untrusted_tool_output(name: &str) -> bool {
    matches!(name, "bash" | "web_fetch" | "web_search") || name.starts_with("mcp_")
}

/// Tools that are safe to execute concurrently (no side effects).
pub const READ_ONLY_TOOLS: &[&str] = &[
    "read",
    "glob",
    "grep",
    "git",
    "hover",
    "references",
    "definition",
    "web_fetch",
    "web_search",
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

    // Collect required parameter names for null-value skipping below.
    let required_params: std::collections::HashSet<&str> = schema
        .get("required")
        .and_then(|v| v.as_array())
        .map(|arr| arr.iter().filter_map(|v| v.as_str()).collect())
        .unwrap_or_default();

    // Check parameter types against schema properties
    if let Some(properties) = schema.get("properties").and_then(|v| v.as_object()) {
        for (key, value) in args {
            if let Some(prop_schema) = properties.get(key) {
                // Skip type validation for null values on optional parameters.
                // When strict mode widens optional types to ["T", "null"], the
                // model sends null to indicate "not specified". Validating null
                // against the base type would produce a spurious type error.
                if value.is_null() && !required_params.contains(key.as_str()) {
                    continue;
                }
                // Handle both single-type ("string") and union-type (["string", "null"]) schemas.
                let type_field = prop_schema.get("type");
                let type_mismatch = match type_field {
                    Some(Value::String(expected_type)) => {
                        !value_matches_type(value, expected_type.as_str())
                    }
                    Some(Value::Array(types)) => {
                        // Union type — value must match at least one type in the array.
                        !types.iter().any(|t| {
                            t.as_str()
                                .map(|ts| value_matches_type(value, ts))
                                .unwrap_or(false)
                        })
                    }
                    _ => false, // No type constraint — skip validation
                };
                if type_mismatch {
                    let type_display = match type_field {
                        Some(Value::String(t)) => t.clone(),
                        Some(Value::Array(types)) => types
                            .iter()
                            .filter_map(|t| t.as_str())
                            .collect::<Vec<_>>()
                            .join("|"),
                        _ => "unknown".to_string(),
                    };
                    errors.push(format!(
                        "parameter '{key}' expected type '{type_display}', got {}",
                        value_type_name(value)
                    ));
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
    fn check_tool_visibility(&self, tool_call: &ToolCall) -> Option<String> {
        match self.tool_visibility_profile {
            crate::routing::ToolVisibilityProfile::Full => None,
            crate::routing::ToolVisibilityProfile::ReadOnly => {
                if READ_ONLY_TOOLS.contains(&tool_call.name.as_str()) {
                    None
                } else {
                    Some(format!(
                        "Tool '{}' is hidden for this read-only task.",
                        tool_call.name
                    ))
                }
            }
            crate::routing::ToolVisibilityProfile::AnswerOnly => Some(format!(
                "Tool '{}' is hidden for this answer-only task.",
                tool_call.name
            )),
        }
    }

    /// Execute a tool call and return both the result and a timed execution record.
    /// In plan mode, write/edit tools are restricted to `.ava/plans/*.md` paths.
    pub(super) async fn execute_tool_call_timed(
        &self,
        tool_call: &ToolCall,
    ) -> (ToolResult, ToolExecution) {
        // Auto-repair misnamed tool calls (e.g. "Read" → "read").
        // Only allocate a new ToolCall when the name actually needs repairing.
        let repaired_name = repair_tool_name(&tool_call.name, &self.tools);
        let owned_tool_call;
        let tool_call: &ToolCall = if repaired_name != tool_call.name {
            owned_tool_call = ToolCall {
                name: repaired_name,
                id: tool_call.id.clone(),
                arguments: tool_call.arguments.clone(),
            };
            &owned_tool_call
        } else {
            tool_call
        };

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

        if let Some(rejection) = self.check_tool_visibility(tool_call) {
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

        // --- Fire ToolBefore plugin hook ---
        if self
            .has_plugin_hook_subscribers(ava_plugin::HookEvent::ToolBefore)
            .await
        {
            let responses = if let Some(pm) = self.plugin_manager.as_ref() {
                let mut pm = pm.lock().await;
                pm.trigger_hook(
                    ava_plugin::HookEvent::ToolBefore,
                    serde_json::json!({
                        "tool": tool_call.name,
                        "call_id": tool_call.id,
                        "args": tool_call.arguments,
                    }),
                )
                .await
            } else {
                Vec::new()
            };

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
        self.append_run_trace(RunEventKind::ToolInvoked {
            tool: tool_call.name.clone(),
            duration_ms: duration.as_millis() as u64,
            success: !tool_failed,
        });
        truncate_tool_result(&mut result);

        // F12 — Injection scanning: check untrusted tool results for prompt injection.
        if !result.is_error && ava_permissions::injection::should_scan_tool(&tool_call.name) {
            let scan = ava_permissions::injection::scan_for_injection(&result.content);
            if scan.suspicious {
                warn!(
                    tool = %tool_call.name,
                    patterns = ?scan.matched_patterns,
                    "prompt injection patterns detected in tool output"
                );
                result.content = ava_permissions::injection::wrap_suspicious_result(
                    &result.content,
                    &scan.matched_patterns,
                );
            }
        }

        if !result.is_error {
            if let Some(project_root) = self.project_root.clone() {
                if ava_config::is_project_trusted(&project_root) {
                    let mut sections = Vec::new();
                    let touched_paths = instruction_trigger_paths(tool_call);
                    if touched_paths.len() > MAX_TOUCHED_PATHS_FOR_RULES {
                        debug!(
                            tool = %tool_call.name,
                            seen_paths = touched_paths.len(),
                            capped_paths = MAX_TOUCHED_PATHS_FOR_RULES,
                            "capping touched paths for on-demand rule activation"
                        );
                    }
                    let unique_paths: BTreeSet<PathBuf> = touched_paths
                        .into_iter()
                        .take(MAX_TOUCHED_PATHS_FOR_RULES)
                        .collect();

                    for file_path in unique_paths
                        .iter()
                        .filter_map(|path| normalize_touched_path(&project_root, path))
                    {
                        {
                            let mut activated_context = self
                                .activated_context_paths
                                .lock()
                                .unwrap_or_else(|error| error.into_inner());
                            if let Some(instructions) = contextual_instructions_for_file_once(
                                &file_path,
                                &project_root,
                                &mut activated_context,
                            ) {
                                let instruction_tokens = estimate_tokens(&instructions);
                                debug!(
                                    file = %file_path.display(),
                                    instruction_tokens,
                                    tool = %tool_call.name,
                                    "activated contextual file guidance"
                                );
                                sections.push(instructions);
                            }
                        }

                        if self.enable_dynamic_rules {
                            let mut activated = self
                                .activated_rule_paths
                                .lock()
                                .unwrap_or_else(|error| error.into_inner());
                            let rule_sections = matching_rule_instructions_for_file(
                                &file_path,
                                &project_root,
                                &mut activated,
                            );
                            if !rule_sections.is_empty() {
                                let token_sum: usize = rule_sections
                                    .iter()
                                    .map(|section| estimate_tokens(section))
                                    .sum();
                                debug!(
                                    file = %file_path.display(),
                                    rule_count = rule_sections.len(),
                                    rule_tokens = token_sum,
                                    tool = %tool_call.name,
                                    "activated on-demand project rules"
                                );
                            }
                            sections.extend(rule_sections);
                        }
                    }

                    append_contextual_sections(&mut result, &sections);
                }
            }
        }

        // --- Fire ToolAfter plugin hook ---
        if self
            .has_plugin_hook_subscribers(ava_plugin::HookEvent::ToolAfter)
            .await
        {
            if let Some(pm) = self.plugin_manager.as_ref() {
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
        }

        // --- Secret scanning for untrusted tool output ---
        // Scan output from tools that may surface external content to prevent
        // secrets from leaking into the LLM context.
        if is_untrusted_tool_output(&tool_call.name) {
            let scan = ava_permissions::secret_scanner::scan_for_secrets(&result.content);
            if scan.has_secrets {
                warn!(
                    tool = %tool_call.name,
                    finding_count = scan.findings.len(),
                    "secrets detected in tool output — redacting"
                );
                result.content = ava_permissions::secret_scanner::redact_secrets(&result.content);
            }
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
    ) -> ava_types::Result<ToolResult> {
        self.tools
            .execute(ToolCall {
                id: format!("post-edit-validation-{name}"),
                name: name.to_string(),
                arguments,
            })
            .await
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
            // Every tool call MUST have a corresponding tool result message,
            // otherwise the OpenAI API rejects with "No tool output found".
            let tool_message = if let Some(result) = results.get(ri) {
                ri += 1;
                Message::new(Role::Tool, result.content.clone())
                    .with_tool_call_id(&tool_call.id)
                    .with_tool_results(vec![result.clone()])
            } else {
                // Missing result (should not happen) — send error placeholder
                // to keep the conversation valid.
                Message::new(
                    Role::Tool,
                    format!("Error: tool '{}' did not produce a result", tool_call.name),
                )
                .with_tool_call_id(&tool_call.id)
            };
            self.context.add_message(tool_message.clone());
            session.add_message(tool_message);
        }
    }

    /// Add tool results to context and session, marked as internal (not user-visible).
    ///
    /// Used by stuck-detector and repetition-detector nudge paths so that the
    /// raw tool output does not appear as regular messages on session reload.
    pub(super) fn add_tool_results_internal(
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
            let mut tool_message = if let Some(result) = results.get(ri) {
                ri += 1;
                Message::new(Role::Tool, result.content.clone())
                    .with_tool_call_id(&tool_call.id)
                    .with_tool_results(vec![result.clone()])
            } else {
                Message::new(
                    Role::Tool,
                    format!("Error: tool '{}' did not produce a result", tool_call.name),
                )
                .with_tool_call_id(&tool_call.id)
            };
            tool_message.user_visible = false;
            self.context.add_message(tool_message.clone());
            session.add_message(tool_message);
        }
    }

    /// Execute tool calls with steering support and event emission.
    ///
    /// Returns (tool_results, steering_triggered, repetition_warning).
    pub(super) async fn execute_tools_unified(
        &mut self,
        tool_calls: &[ToolCall],
        detector: &mut StuckDetector,
        repetition_detector: &mut RepetitionDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> (Vec<ToolResult>, bool, Option<String>) {
        let mut steering_triggered = false;

        // Guard against runaway tool invocations from a single LLM response
        if tool_calls.len() > MAX_TOOLS_PER_TURN {
            warn!(
                requested = tool_calls.len(),
                limit = MAX_TOOLS_PER_TURN,
                "Tool call limit reached, truncating to {MAX_TOOLS_PER_TURN}"
            );
        }
        let capped_calls = &tool_calls[..tool_calls.len().min(MAX_TOOLS_PER_TURN)];

        // Separate into read-only and write groups
        let mut read_calls: Vec<(usize, &ToolCall)> = Vec::new();
        let mut write_calls: Vec<(usize, &ToolCall)> = Vec::new();
        for (i, tc) in capped_calls.iter().enumerate() {
            if tc.name == "attempt_completion" {
                continue;
            }
            if READ_ONLY_TOOLS.contains(&tc.name.as_str()) {
                read_calls.push((i, tc));
            } else {
                write_calls.push((i, tc));
            }
        }

        // Emit all ToolCall events first (streaming mode)
        for tc in capped_calls {
            if tc.name == "attempt_completion" {
                continue;
            }
            Self::emit(event_tx, AgentEvent::ToolCall(tc.clone()));
        }

        let mut indexed_results: Vec<(usize, ToolResult, ToolExecution)> = Vec::new();

        // Read-only tools concurrently
        if !read_calls.is_empty() {
            // F1: Split into pre-dispatched (already executed during stream) and pending.
            let mut pending_reads: Vec<(usize, ToolCall)> = Vec::new();
            for (i, tc) in &read_calls {
                if let Some(pre_result) = self.pre_dispatched_results.remove(&tc.id) {
                    debug!(
                        tool = %tc.name,
                        "F1: using pre-dispatched result from streaming"
                    );
                    let execution = ava_tools::monitor::ToolExecution {
                        tool_name: tc.name.clone(),
                        arguments_hash: ava_tools::monitor::hash_arguments(&tc.arguments),
                        success: !pre_result.is_error,
                        duration: std::time::Duration::ZERO, // timing was during stream
                        timestamp: Instant::now(),
                    };
                    indexed_results.push((*i, pre_result, execution));
                } else {
                    pending_reads.push((*i, (*tc).clone()));
                }
            }

            if !pending_reads.is_empty() {
                let concurrency = pending_reads.len().min(MAX_CONCURRENT_READ_ONLY_TOOLS);
                let agent = &*self;
                debug!(
                    total = read_calls.len(),
                    pre_dispatched = read_calls.len() - pending_reads.len(),
                    pending = pending_reads.len(),
                    concurrency,
                    "executing read-only tool batch"
                );

                let results = futures::stream::iter(pending_reads)
                    .map(|(i, tc)| async move {
                        let (result, execution) = agent.execute_tool_call_timed(&tc).await;
                        (i, result, execution)
                    })
                    .buffer_unordered(MAX_CONCURRENT_READ_ONLY_TOOLS)
                    .collect::<Vec<_>>()
                    .await;

                indexed_results.extend(results);
            }
        }

        // Poll for steering after read-only batch
        if let Some(ref mut queue) = self.message_queue {
            queue.poll();
            if queue.has_steering() {
                steering_triggered = true;
            }
        }

        // Write tools sequentially — check steering between each
        if !steering_triggered && !write_calls.is_empty() {
            self.ensure_snapshot_manager_initialized().await;

            // Take a shadow git snapshot before any write tools execute.
            // This enables full project-state rollback via /rewind.
            {
                let manager_guard = self.snapshot_manager.read().await;
                if let Some(ref manager) = *manager_guard {
                    let tool_names: Vec<&str> =
                        write_calls.iter().map(|(_, tc)| tc.name.as_str()).collect();
                    let snap_msg = format!("before: {}", tool_names.join(", "));
                    match manager.take_snapshot(&snap_msg).await {
                        Ok(hash) => {
                            debug!(hash = %hash, "shadow snapshot taken before write tools");
                            Self::emit(
                                event_tx,
                                AgentEvent::SnapshotTaken {
                                    commit_hash: hash,
                                    message: snap_msg,
                                },
                            );
                        }
                        Err(e) => {
                            // Non-fatal — log but don't block tool execution
                            warn!(error = %e, "failed to take shadow snapshot");
                        }
                    }
                }
            }

            for (i, tc) in &write_calls {
                // Snapshot file before write/edit tools for diff tracking
                let diff_path =
                    if crate::streaming_diff::StreamingDiffTracker::is_tracked_tool(&tc.name) {
                        let maybe_path = extract_tool_path(tc);
                        if let Some(ref p) = maybe_path {
                            self.diff_tracker.snapshot_before_edit_async(p).await;
                        }
                        maybe_path
                    } else {
                        None
                    };

                let (result, execution) = self.execute_tool_call_timed(tc).await;

                // Record diff after successful write/edit
                if !result.is_error {
                    if let Some(ref path) = diff_path {
                        if let Some(crate::streaming_diff::DiffEvent::EditComplete {
                            ref file,
                            ref diff_text,
                            additions,
                            deletions,
                        }) = self.diff_tracker.record_edit_complete_async(path).await
                        {
                            Self::emit(
                                event_tx,
                                AgentEvent::DiffPreview {
                                    file: file.clone(),
                                    diff_text: diff_text.clone(),
                                    additions,
                                    deletions,
                                },
                            );
                        }
                    }
                }

                indexed_results.push((*i, result, execution));

                // Poll for steering after each write tool
                if let Some(ref mut queue) = self.message_queue {
                    queue.poll();
                    if queue.has_steering() {
                        steering_triggered = true;
                        break;
                    }
                }
            }
        }

        // If steering was triggered, add skip results for remaining write tools
        if steering_triggered {
            let executed_indices: std::collections::HashSet<usize> =
                indexed_results.iter().map(|(i, _, _)| *i).collect();
            for (i, tc) in &write_calls {
                if !executed_indices.contains(i) {
                    let skip_result = ToolResult {
                        call_id: tc.id.clone(),
                        content: "[Tool execution interrupted — the user has sent a new instruction. Do NOT retry this tool. Focus on the user's new message instead.]".to_string(),
                        is_error: false,
                    };
                    let execution = ToolExecution {
                        tool_name: tc.name.clone(),
                        arguments_hash: hash_arguments(&tc.arguments),
                        success: false,
                        duration: std::time::Duration::ZERO,
                        timestamp: Instant::now(),
                    };
                    indexed_results.push((*i, skip_result, execution));
                }
            }
        }

        // Sort by original index and collect results
        indexed_results.sort_by_key(|(i, _, _)| *i);
        let mut tool_results = Vec::new();
        let mut repetition_warning = None;
        for (idx, result, execution) in &indexed_results {
            detector.tool_monitor_mut().record(execution.clone());
            tool_results.push(result.clone());
            Self::emit(event_tx, AgentEvent::ToolResult(result.clone()));

            // Record each tool call in the repetition detector.
            // We match back to the original ToolCall by index.
            if let Some(tc) = capped_calls.get(*idx) {
                if let Some(warning) = repetition_detector.record(tc) {
                    warn!("{warning}");
                    repetition_warning = Some(warning);
                }
            }
        }

        (tool_results, steering_triggered, repetition_warning)
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

fn format_validation_result(label: &str, result: ava_types::Result<ToolResult>) -> ValidationLine {
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
///
/// In Plan mode, only read-only tools are allowed. Bash is permitted but only
/// for commands classified as Low risk by the CommandClassifier (read-only
/// operations like `ls`, `cat`, `git status`, `cargo test`, etc.). High-risk
/// or destructive bash commands are blocked.
pub fn check_plan_mode_tool(tool_call: &ToolCall) -> Option<String> {
    let name = tool_call.name.as_str();

    // Bash: allow read-only commands, block everything else
    if name == "bash" {
        let command = tool_call
            .arguments
            .get("command")
            .and_then(|v| v.as_str())
            .unwrap_or("");
        let classification = ava_permissions::classifier::classify_bash_command(command);
        if classification.blocked {
            return Some(format!(
                "Plan mode: this command is blocked. {}",
                classification.reason.unwrap_or_default()
            ));
        }
        if classification.risk_level > ava_permissions::tags::RiskLevel::Low {
            return Some(format!(
                "Plan mode: only read-only commands are allowed. '{}' is classified as {:?} risk. \
                 Switch to Code mode to run this command.",
                command, classification.risk_level
            ));
        }
        return None;
    }

    // Allow read-only tools unconditionally
    if READ_ONLY_TOOLS.contains(&name)
        || name == "codebase_search"
        || name == "todo_read"
        || name == "todo_write"
        || name == "web_fetch"
        || name == "git"
        || name == "plan"
        || name == "question"
        || name == "memory_read"
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
///
/// Also handles MCP namespace stripping: when the LLM calls a tool by its bare name
/// (e.g. `"browser_navigate"`) but it is registered under a namespaced name
/// (e.g. `"mcp_playwright_browser_navigate"`), we match the tool part after the server prefix.
/// If multiple MCP tools share the same bare name, the first match wins.
pub fn repair_tool_name(name: &str, registry: &ToolRegistry) -> String {
    // Exact match — no repair needed
    if registry.has_tool(name) {
        return name.to_string();
    }

    // Case-insensitive match against all registered tool names
    let lower = name.to_lowercase();
    let mut mcp_match: Option<String> = None;
    for tool_name in registry.tool_names() {
        if tool_name.to_lowercase() == lower {
            tracing::info!("Repaired tool name '{}' → '{}'", name, tool_name);
            return tool_name;
        }

        // MCP namespace repair: registered name is "mcp_{server}_{tool}", LLM called "{tool}".
        // Match when the tool name ends with the requested name after the mcp_ prefix.
        if mcp_match.is_none() && tool_name.starts_with("mcp_") {
            // Extract the last segment: for "mcp_playwright_browser_navigate" → "browser_navigate"
            // Try matching against the part after the second underscore (server name)
            if let Some(rest) = tool_name.strip_prefix("mcp_") {
                if let Some(pos) = rest.find('_') {
                    let tool_part = &rest[pos + 1..];
                    if tool_part.to_lowercase() == lower {
                        mcp_match = Some(tool_name);
                    }
                }
            }
        }
    }

    if let Some(repaired) = mcp_match {
        tracing::info!(
            "Repaired MCP tool name '{}' → '{}' (namespace expansion)",
            name,
            repaired
        );
        return repaired;
    }

    // No match — return original (will error in execute)
    name.to_string()
}

/// Extract the target file path from a tool call's arguments.
///
/// Looks for `path` or `file_path` string fields. Returns the path as a
/// `PathBuf`, resolving relative paths against the current directory.
fn extract_tool_path(tc: &ToolCall) -> Option<PathBuf> {
    let path_str = tc
        .arguments
        .get("path")
        .or_else(|| tc.arguments.get("file_path"))
        .and_then(|v| v.as_str())?;
    let path = PathBuf::from(path_str);
    if path.is_absolute() {
        Some(path)
    } else {
        std::env::current_dir().ok().map(|cwd| cwd.join(path))
    }
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

#[cfg(test)]
mod tests {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;
    use std::time::Duration;

    use async_trait::async_trait;
    use ava_context::ContextManager;
    use ava_llm::providers::mock::MockProvider;
    use ava_tools::registry::{Tool as ToolTrait, ToolRegistry};
    use ava_types::ThinkingLevel;

    use super::*;
    use crate::stuck::StuckDetector;

    struct SlowReadTool {
        active: Arc<AtomicUsize>,
        max_active: Arc<AtomicUsize>,
    }

    #[async_trait]
    impl ToolTrait for SlowReadTool {
        fn name(&self) -> &str {
            "read"
        }

        fn description(&self) -> &str {
            "Slow read tool"
        }

        fn parameters(&self) -> Value {
            serde_json::json!({
                "type": "object",
                "required": ["path"],
                "properties": {
                    "path": { "type": "string" }
                }
            })
        }

        async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
            let current = self.active.fetch_add(1, Ordering::SeqCst) + 1;
            self.max_active.fetch_max(current, Ordering::SeqCst);
            tokio::time::sleep(Duration::from_millis(20)).await;
            self.active.fetch_sub(1, Ordering::SeqCst);

            Ok(ToolResult {
                call_id: String::new(),
                content: args
                    .get("path")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .to_string(),
                is_error: false,
            })
        }
    }

    fn test_config() -> super::super::AgentConfig {
        super::super::AgentConfig {
            max_turns: 10,
            max_budget_usd: 0.0,
            token_limit: 128_000,
            provider: String::new(),
            model: "mock".to_string(),
            max_cost_usd: 1.0,
            loop_detection: true,
            custom_system_prompt: None,
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            auto_compact: true,
            post_edit_validation: None,
            stream_timeout_secs: 90,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
        }
    }

    #[tokio::test]
    async fn read_only_tools_respect_concurrency_cap() {
        let active = Arc::new(AtomicUsize::new(0));
        let max_active = Arc::new(AtomicUsize::new(0));

        let mut registry = ToolRegistry::new();
        registry.register(SlowReadTool {
            active: active.clone(),
            max_active: max_active.clone(),
        });

        let mut agent = AgentLoop::new(
            Box::new(MockProvider::new("mock", vec![])),
            registry,
            ContextManager::new(4_096),
            test_config(),
        );

        let tool_calls: Vec<ToolCall> = (0..(MAX_CONCURRENT_READ_ONLY_TOOLS * 2 + 3))
            .map(|index| ToolCall {
                id: format!("call-{index}"),
                name: "read".to_string(),
                arguments: serde_json::json!({ "path": format!("file-{index}.rs") }),
            })
            .collect();

        let mut detector = StuckDetector::new();
        let mut repetition_detector = RepetitionDetector::new(100);
        let (results, steering_triggered, repetition_warning) = agent
            .execute_tools_unified(&tool_calls, &mut detector, &mut repetition_detector, &None)
            .await;

        assert_eq!(results.len(), tool_calls.len());
        assert!(!steering_triggered);
        assert!(repetition_warning.is_none());
        assert!(
            max_active.load(Ordering::SeqCst) <= MAX_CONCURRENT_READ_ONLY_TOOLS,
            "expected at most {MAX_CONCURRENT_READ_ONLY_TOOLS} concurrent tools, saw {}",
            max_active.load(Ordering::SeqCst)
        );
    }
}
