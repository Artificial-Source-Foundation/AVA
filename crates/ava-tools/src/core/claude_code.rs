use std::path::{Path, PathBuf};
use std::time::Duration;

use async_trait::async_trait;
use ava_config::ClaudeCodeConfig;
use ava_types::{AvaError, ToolResult};
use serde::Deserialize;
use serde_json::{json, Value};

use crate::registry::Tool;

/// Default timeout for Claude Code subprocess (5 minutes).
const DEFAULT_TIMEOUT: Duration = Duration::from_secs(300);

pub struct ClaudeCodeTool {
    config: ClaudeCodeConfig,
}

impl ClaudeCodeTool {
    pub fn new(config: ClaudeCodeConfig) -> Self {
        Self { config }
    }

    /// Build the command-line arguments for the `claude` subprocess.
    fn build_args(&self, args: &Value) -> Result<(String, Vec<String>), AvaError> {
        let goal = args
            .get("goal")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: goal".into()))?;

        let mut cmd_args = vec![
            "-p".to_string(),
            goal.to_string(),
            "--output-format".to_string(),
            "json".to_string(),
        ];

        // Allowed tools
        let allowed_tools = args
            .get("allowed_tools")
            .and_then(Value::as_array)
            .map(|arr| {
                arr.iter()
                    .filter_map(Value::as_str)
                    .map(String::from)
                    .collect::<Vec<_>>()
            })
            .unwrap_or_else(|| self.config.default_allowed_tools.clone());

        if !allowed_tools.is_empty() {
            cmd_args.push("--allowedTools".to_string());
            cmd_args.push(allowed_tools.join(","));
        }

        // Max turns
        let max_turns = args
            .get("max_turns")
            .and_then(Value::as_u64)
            .map(|v| v as u32)
            .unwrap_or(self.config.default_max_turns);
        cmd_args.push("--max-turns".to_string());
        cmd_args.push(max_turns.to_string());

        // Max budget
        let max_budget = args
            .get("max_budget_usd")
            .and_then(Value::as_f64)
            .unwrap_or(self.config.default_max_budget_usd);
        cmd_args.push("--max-budget-usd".to_string());
        cmd_args.push(format!("{:.2}", max_budget));

        // Session persistence
        if !self.config.session_persistence {
            cmd_args.push("--no-session-persistence".to_string());
        }

        // System prompt
        if let Some(prompt) = args.get("system_prompt").and_then(Value::as_str) {
            if !prompt.is_empty() {
                cmd_args.push("--append-system-prompt".to_string());
                cmd_args.push(prompt.to_string());
            }
        }

        Ok((goal.to_string(), cmd_args))
    }
}

/// Attempt to locate the `claude` binary.
///
/// Resolution order:
/// 1. Config override (`binary_path`)
/// 2. PATH lookup via `which claude`
async fn find_claude_binary(config_path: Option<&Path>) -> Option<PathBuf> {
    // 1. Check config override
    if let Some(p) = config_path {
        if tokio::fs::metadata(p).await.is_ok() {
            return Some(p.to_path_buf());
        }
    }

    // 2. Check PATH via `which claude`
    let output = tokio::process::Command::new("which")
        .arg("claude")
        .output()
        .await
        .ok()?;

    if output.status.success() {
        let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if !path.is_empty() {
            return Some(PathBuf::from(path));
        }
    }

    None
}

/// Parse the JSON response from Claude Code.
fn parse_response(stdout: &str) -> Result<ClaudeCodeResponse, AvaError> {
    // CC may output multiple JSON objects (one per line in some modes).
    // In --output-format json mode, the last complete JSON object is the result.
    // Try parsing the whole output first, then fall back to last line.
    serde_json::from_str::<ClaudeCodeResponse>(stdout).or_else(|_| {
        // Try last non-empty line
        stdout
            .lines()
            .rev()
            .find(|line| !line.trim().is_empty())
            .ok_or_else(|| AvaError::ToolError("Claude Code returned empty output".into()))
            .and_then(|line| {
                serde_json::from_str::<ClaudeCodeResponse>(line).map_err(|e| {
                    AvaError::ToolError(format!("Failed to parse Claude Code response: {e}"))
                })
            })
    })
}

/// Format the parsed response into a human-readable tool result.
fn format_result(response: &ClaudeCodeResponse) -> String {
    let mut parts = Vec::new();

    if let Some(ref result) = response.result {
        parts.push(result.clone());
    } else if let Some(ref error) = response.error {
        parts.push(format!("[Claude Code error] {error}"));
    }

    // Cost summary
    let mut summary = Vec::new();
    if let Some(cost) = response.cost_usd {
        summary.push(format!("${:.4}", cost));
    }
    if let Some(turns) = response.turns {
        summary.push(format!("{} turns", turns));
    }
    if let Some(duration) = response.duration_ms {
        let secs = duration as f64 / 1000.0;
        summary.push(format!("{:.1}s", secs));
    }
    if let Some(ref usage) = response.usage {
        let input = usage.input_tokens.unwrap_or(0);
        let output = usage.output_tokens.unwrap_or(0);
        summary.push(format!("{}in/{}out tokens", input, output));
    }

    if !summary.is_empty() {
        parts.push(format!("\n[CC cost: {}]", summary.join(", ")));
    }

    if parts.is_empty() {
        "Claude Code returned no result.".to_string()
    } else {
        parts.join("")
    }
}

#[async_trait]
impl Tool for ClaudeCodeTool {
    fn name(&self) -> &str {
        "claude_code"
    }

    fn description(&self) -> &str {
        "Delegate a task to Claude Code, which runs autonomously with its own tools. \
         Use for code review, analysis, refactoring, or any task that benefits from an \
         independent agent."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["goal"],
            "properties": {
                "goal": {
                    "type": "string",
                    "description": "What Claude Code should accomplish"
                },
                "allowed_tools": {
                    "type": "array",
                    "items": { "type": "string" },
                    "description": "CC tools to enable (default: Read,Grep,Glob)"
                },
                "max_turns": {
                    "type": "integer",
                    "minimum": 1,
                    "description": "Turn limit (default: 10)"
                },
                "max_budget_usd": {
                    "type": "number",
                    "minimum": 0,
                    "description": "Cost limit in USD (default: 5.00)"
                },
                "working_directory": {
                    "type": "string",
                    "description": "Working directory for Claude Code"
                },
                "system_prompt": {
                    "type": "string",
                    "description": "Additional instructions appended to CC's system prompt"
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        // Resolve binary path
        let binary = find_claude_binary(self.config.binary_path.as_deref())
            .await
            .ok_or_else(|| {
                AvaError::ToolError(
                    "Claude Code binary not found. Install it from https://docs.anthropic.com/en/docs/claude-code \
                     or set claude_code.binary_path in your config."
                        .to_string(),
                )
            })?;

        // Build arguments
        let (_goal, cmd_args) = self.build_args(&args)?;

        // Set up the subprocess
        let mut cmd = tokio::process::Command::new(&binary);
        cmd.args(&cmd_args)
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped())
            .kill_on_drop(true)
            // Clear CLAUDECODE env var to allow nested invocation.
            // CC blocks nested sessions by checking this var, but AVA spawns
            // CC as an independent subprocess, not a nested session.
            .env_remove("CLAUDECODE");

        // Working directory
        if let Some(cwd) = args.get("working_directory").and_then(Value::as_str) {
            let cwd_path = PathBuf::from(cwd);
            if tokio::fs::metadata(&cwd_path).await.is_ok() {
                cmd.current_dir(cwd_path);
            } else {
                return Err(AvaError::ToolError(format!(
                    "Working directory does not exist: {cwd}"
                )));
            }
        }

        // Spawn and wait with timeout
        let child = cmd.spawn().map_err(|e| {
            AvaError::ToolError(format!(
                "Failed to spawn Claude Code ({}): {e}",
                binary.display()
            ))
        })?;

        let output = tokio::time::timeout(DEFAULT_TIMEOUT, child.wait_with_output())
            .await
            .map_err(|_| {
                AvaError::ToolError(format!(
                    "Claude Code timed out after {}s",
                    DEFAULT_TIMEOUT.as_secs()
                ))
            })?
            .map_err(|e| AvaError::ToolError(format!("Claude Code process error: {e}")))?;

        let stdout = String::from_utf8_lossy(&output.stdout);
        let stderr = String::from_utf8_lossy(&output.stderr);

        // Check exit status
        if !output.status.success() {
            let code = output.status.code().unwrap_or(-1);
            let error_detail = if !stderr.is_empty() {
                stderr.to_string()
            } else if !stdout.is_empty() {
                // CC may put errors in stdout as JSON
                stdout.to_string()
            } else {
                format!("exit code {code}")
            };

            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("[Claude Code failed (exit {code})] {error_detail}"),
                is_error: true,
            });
        }

        // Parse JSON response
        match parse_response(&stdout) {
            Ok(response) => {
                // Check if CC reported an error in its response
                let is_error = response.error.is_some() && response.result.is_none();
                Ok(ToolResult {
                    call_id: String::new(),
                    content: format_result(&response),
                    is_error,
                })
            }
            Err(_) => {
                // If JSON parsing fails but we got output, return it as-is
                let content = if !stdout.is_empty() {
                    stdout.to_string()
                } else {
                    "Claude Code completed but returned no parseable output.".to_string()
                };
                Ok(ToolResult {
                    call_id: String::new(),
                    content,
                    is_error: false,
                })
            }
        }
    }
}

#[derive(Debug, Deserialize)]
struct ClaudeCodeResponse {
    #[allow(dead_code)]
    session_id: Option<String>,
    result: Option<String>,
    duration_ms: Option<u64>,
    cost_usd: Option<f64>,
    turns: Option<u32>,
    usage: Option<ClaudeCodeUsage>,
    error: Option<String>,
}

#[derive(Debug, Deserialize)]
struct ClaudeCodeUsage {
    input_tokens: Option<u64>,
    output_tokens: Option<u64>,
    #[allow(dead_code)]
    cache_read_input_tokens: Option<u64>,
    #[allow(dead_code)]
    cache_creation_input_tokens: Option<u64>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn default_config() -> ClaudeCodeConfig {
        ClaudeCodeConfig::default()
    }

    fn make_tool() -> ClaudeCodeTool {
        ClaudeCodeTool::new(default_config())
    }

    // --- Tool metadata tests ---

    #[test]
    fn tool_metadata() {
        let tool = make_tool();
        assert_eq!(tool.name(), "claude_code");
        assert!(!tool.description().is_empty());
        assert!(tool.description().contains("Claude Code"));
        let params = tool.parameters();
        assert_eq!(params["required"], json!(["goal"]));
        assert!(params["properties"]["goal"].is_object());
        assert!(params["properties"]["allowed_tools"].is_object());
        assert!(params["properties"]["max_turns"].is_object());
        assert!(params["properties"]["max_budget_usd"].is_object());
        assert!(params["properties"]["working_directory"].is_object());
        assert!(params["properties"]["system_prompt"].is_object());
    }

    // --- JSON response parsing tests ---

    #[test]
    fn parse_valid_response() {
        let json_str = r#"{
            "session_id": "abc-123",
            "result": "Found 3 security issues in auth.rs",
            "duration_ms": 8500,
            "cost_usd": 0.035,
            "turns": 4,
            "usage": {
                "input_tokens": 12000,
                "output_tokens": 2500,
                "cache_read_input_tokens": 8000
            }
        }"#;

        let response = parse_response(json_str).unwrap();
        assert_eq!(
            response.result.as_deref(),
            Some("Found 3 security issues in auth.rs")
        );
        assert_eq!(response.cost_usd, Some(0.035));
        assert_eq!(response.turns, Some(4));
        assert_eq!(response.duration_ms, Some(8500));
        let usage = response.usage.unwrap();
        assert_eq!(usage.input_tokens, Some(12000));
        assert_eq!(usage.output_tokens, Some(2500));
    }

    #[test]
    fn parse_error_response() {
        let json_str = r#"{
            "error": "Rate limited, please try again",
            "session_id": null
        }"#;

        let response = parse_response(json_str).unwrap();
        assert!(response.result.is_none());
        assert_eq!(
            response.error.as_deref(),
            Some("Rate limited, please try again")
        );
    }

    #[test]
    fn parse_minimal_response() {
        let json_str = r#"{"result": "done"}"#;
        let response = parse_response(json_str).unwrap();
        assert_eq!(response.result.as_deref(), Some("done"));
        assert!(response.usage.is_none());
        assert!(response.cost_usd.is_none());
    }

    #[test]
    fn parse_empty_output_fails() {
        let result = parse_response("");
        assert!(result.is_err());
    }

    #[test]
    fn parse_multiline_output_uses_last_json() {
        let output = "some debug line\n{\"result\": \"final answer\"}";
        let response = parse_response(output).unwrap();
        assert_eq!(response.result.as_deref(), Some("final answer"));
    }

    // --- Result formatting tests ---

    #[test]
    fn format_result_with_all_fields() {
        let response = ClaudeCodeResponse {
            session_id: Some("abc".into()),
            result: Some("Found issues".into()),
            duration_ms: Some(5000),
            cost_usd: Some(0.042),
            turns: Some(3),
            usage: Some(ClaudeCodeUsage {
                input_tokens: Some(1000),
                output_tokens: Some(500),
                cache_read_input_tokens: None,
                cache_creation_input_tokens: None,
            }),
            error: None,
        };
        let formatted = format_result(&response);
        assert!(formatted.contains("Found issues"));
        assert!(formatted.contains("$0.0420"));
        assert!(formatted.contains("3 turns"));
        assert!(formatted.contains("5.0s"));
        assert!(formatted.contains("1000in/500out tokens"));
    }

    #[test]
    fn format_result_error_only() {
        let response = ClaudeCodeResponse {
            session_id: None,
            result: None,
            duration_ms: None,
            cost_usd: None,
            turns: None,
            usage: None,
            error: Some("something broke".into()),
        };
        let formatted = format_result(&response);
        assert!(formatted.contains("[Claude Code error]"));
        assert!(formatted.contains("something broke"));
    }

    #[test]
    fn format_result_empty() {
        let response = ClaudeCodeResponse {
            session_id: None,
            result: None,
            duration_ms: None,
            cost_usd: None,
            turns: None,
            usage: None,
            error: None,
        };
        let formatted = format_result(&response);
        assert!(formatted.contains("no result"));
    }

    // --- Argument building tests ---

    #[test]
    fn build_args_minimal() {
        let tool = make_tool();
        let args = json!({"goal": "review code"});
        let (_goal, cmd_args) = tool.build_args(&args).unwrap();

        assert!(cmd_args.contains(&"-p".to_string()));
        assert!(cmd_args.contains(&"review code".to_string()));
        assert!(cmd_args.contains(&"--output-format".to_string()));
        assert!(cmd_args.contains(&"json".to_string()));
        assert!(cmd_args.contains(&"--max-turns".to_string()));
        assert!(cmd_args.contains(&"10".to_string()));
        assert!(cmd_args.contains(&"--no-session-persistence".to_string()));
    }

    #[test]
    fn build_args_all_params() {
        let tool = make_tool();
        let args = json!({
            "goal": "refactor auth module",
            "allowed_tools": ["Read", "Edit", "Bash"],
            "max_turns": 20,
            "max_budget_usd": 2.5,
            "system_prompt": "Focus on error handling"
        });
        let (_goal, cmd_args) = tool.build_args(&args).unwrap();

        assert!(cmd_args.contains(&"refactor auth module".to_string()));
        assert!(cmd_args.contains(&"Read,Edit,Bash".to_string()));
        assert!(cmd_args.contains(&"20".to_string()));
        assert!(cmd_args.contains(&"2.50".to_string()));
        assert!(cmd_args.contains(&"--append-system-prompt".to_string()));
        assert!(cmd_args.contains(&"Focus on error handling".to_string()));
    }

    #[test]
    fn build_args_missing_goal_errors() {
        let tool = make_tool();
        let result = tool.build_args(&json!({}));
        assert!(result.is_err());
    }

    #[test]
    fn build_args_custom_config() {
        let config = ClaudeCodeConfig {
            binary_path: None,
            session_persistence: true,
            default_max_turns: 25,
            default_max_budget_usd: 10.0,
            default_allowed_tools: vec!["Read".into(), "Edit".into()],
        };
        let tool = ClaudeCodeTool::new(config);
        let args = json!({"goal": "test"});
        let (_goal, cmd_args) = tool.build_args(&args).unwrap();

        // Session persistence enabled -> no --no-session-persistence flag
        assert!(!cmd_args.contains(&"--no-session-persistence".to_string()));
        // Uses config defaults
        assert!(cmd_args.contains(&"25".to_string()));
        assert!(cmd_args.contains(&"10.00".to_string()));
        assert!(cmd_args.contains(&"Read,Edit".to_string()));
    }

    // --- Binary discovery tests ---

    #[tokio::test]
    async fn find_binary_with_nonexistent_config_path() {
        let result = find_claude_binary(Some(Path::new("/nonexistent/path/claude"))).await;
        // Config path doesn't exist, falls through to PATH lookup.
        // We can't guarantee `claude` is on PATH in CI, so just test the config path case.
        // The important thing is it doesn't panic.
        let _ = result;
    }

    #[tokio::test]
    async fn find_binary_with_no_config() {
        // Just verifying it doesn't panic — result depends on environment.
        let _ = find_claude_binary(None).await;
    }

    // --- Config default tests ---

    #[test]
    fn config_defaults() {
        let config = ClaudeCodeConfig::default();
        assert_eq!(config.default_max_turns, 10);
        assert!((config.default_max_budget_usd - 5.0).abs() < f64::EPSILON);
        assert_eq!(config.default_allowed_tools, vec!["Read", "Grep", "Glob"]);
        assert!(!config.session_persistence);
        assert!(config.binary_path.is_none());
    }

    #[test]
    fn config_deserialize_defaults() {
        let json_str = "{}";
        let config: ClaudeCodeConfig = serde_json::from_str(json_str).unwrap();
        assert_eq!(config.default_max_turns, 10);
        assert!((config.default_max_budget_usd - 5.0).abs() < f64::EPSILON);
        assert_eq!(config.default_allowed_tools, vec!["Read", "Grep", "Glob"]);
    }

    #[test]
    fn config_deserialize_custom() {
        let json_str = r#"{
            "binary_path": "/usr/local/bin/claude",
            "session_persistence": true,
            "default_max_turns": 20,
            "default_max_budget_usd": 2.0,
            "default_allowed_tools": ["Read", "Edit"]
        }"#;
        let config: ClaudeCodeConfig = serde_json::from_str(json_str).unwrap();
        assert_eq!(
            config.binary_path,
            Some(PathBuf::from("/usr/local/bin/claude"))
        );
        assert!(config.session_persistence);
        assert_eq!(config.default_max_turns, 20);
        assert!((config.default_max_budget_usd - 2.0).abs() < f64::EPSILON);
        assert_eq!(config.default_allowed_tools, vec!["Read", "Edit"]);
    }
}
