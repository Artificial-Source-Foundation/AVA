use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_platform::{ExecuteOptions, Platform};
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

const DEFAULT_TIMEOUT_MS: u64 = 120_000;

pub struct DiagnosticsTool {
    platform: Arc<dyn Platform>,
    lsp_manager: Option<Arc<ava_lsp::LspManager>>,
}

impl DiagnosticsTool {
    pub fn new(platform: Arc<dyn Platform>, lsp_manager: Option<Arc<ava_lsp::LspManager>>) -> Self {
        Self {
            platform,
            lsp_manager,
        }
    }
}

#[async_trait]
impl Tool for DiagnosticsTool {
    fn name(&self) -> &str {
        "diagnostics"
    }

    fn description(&self) -> &str {
        "Get compiler/type-checker diagnostics for the project or a file"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "properties": {
                "path": { "type": "string", "description": "File path to check (optional, checks whole project if omitted)" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let scope_path = args.get("path").and_then(Value::as_str);

        if let (Some(lsp_manager), Some(path)) = (&self.lsp_manager, scope_path) {
            let path_buf = std::path::PathBuf::from(path);
            match lsp_manager.diagnostics(&path_buf).await {
                Ok(diagnostics) => {
                    let result_json = json!({ "diagnostics": diagnostics });
                    return Ok(ToolResult {
                        call_id: String::new(),
                        content: result_json.to_string(),
                        is_error: result_json["diagnostics"]
                            .as_array()
                            .map(|items| !items.is_empty())
                            .unwrap_or(false),
                    });
                }
                Err(err) => {
                    tracing::debug!(tool = "diagnostics", path, error = %err, "LSP diagnostics unavailable, falling back");
                }
            }
        }

        tracing::debug!(tool = "diagnostics", path = ?scope_path, "executing diagnostics tool");

        let command = detect_diagnostics_command(&*self.platform, scope_path).await?;
        let output = self
            .platform
            .execute_with_options(
                &command,
                ExecuteOptions {
                    timeout: Some(Duration::from_millis(DEFAULT_TIMEOUT_MS)),
                    working_dir: None,
                    env_vars: Vec::new(),
                    ..Default::default()
                },
            )
            .await?;
        let combined = format!("{}\n{}", output.stdout, output.stderr);

        let diagnostics = parse_diagnostics(&combined, &command);

        let result_json = json!({
            "diagnostics": diagnostics,
        });

        Ok(ToolResult {
            call_id: String::new(),
            content: result_json.to_string(),
            is_error: !diagnostics.is_empty(),
        })
    }
}

async fn detect_diagnostics_command(
    platform: &dyn Platform,
    scope_path: Option<&str>,
) -> ava_types::Result<String> {
    if platform.exists(Path::new("Cargo.toml")).await {
        return Ok("cargo check --message-format=json 2>&1".to_string());
    }
    if platform.exists(Path::new("package.json")).await {
        let path_suffix = scope_path
            .map(|p| format!(" {}", shell_single_quote(p)))
            .unwrap_or_default();
        return Ok(format!("npx tsc --noEmit{path_suffix} 2>&1"));
    }
    if platform.exists(Path::new("pyproject.toml")).await {
        if let Some(p) = scope_path {
            return Ok(format!(
                "python -m py_compile {} 2>&1",
                shell_single_quote(p)
            ));
        }
        return Ok("ruff check . 2>&1".to_string());
    }
    Err(AvaError::ToolError(
        "Could not detect project type for diagnostics".to_string(),
    ))
}

fn parse_diagnostics(output: &str, command: &str) -> Vec<Value> {
    let mut diagnostics = Vec::new();

    if command.contains("cargo check") {
        // Parse cargo JSON messages
        for line in output.lines() {
            if let Ok(msg) = serde_json::from_str::<Value>(line) {
                if msg.get("reason").and_then(Value::as_str) == Some("compiler-message") {
                    if let Some(message) = msg.get("message") {
                        let level = message
                            .get("level")
                            .and_then(Value::as_str)
                            .unwrap_or("unknown");
                        let text = message.get("message").and_then(Value::as_str).unwrap_or("");

                        // Extract primary span location
                        let spans = message.get("spans").and_then(Value::as_array);
                        let primary = spans.and_then(|s| {
                            s.iter().find(|span| {
                                span.get("is_primary")
                                    .and_then(Value::as_bool)
                                    .unwrap_or(false)
                            })
                        });

                        let (file, line_num) = if let Some(span) = primary {
                            (
                                span.get("file_name").and_then(Value::as_str).unwrap_or(""),
                                span.get("line_start").and_then(Value::as_u64).unwrap_or(0),
                            )
                        } else {
                            ("", 0)
                        };

                        if level == "error" || level == "warning" {
                            diagnostics.push(json!({
                                "file": file,
                                "line": line_num,
                                "severity": level,
                                "message": text,
                            }));
                        }
                    }
                }
            }
        }
    } else if command.contains("tsc") {
        // Parse TypeScript output: "src/file.ts(10,5): error TS2304: ..."
        let re = regex::Regex::new(r"^(.+?)\((\d+),\d+\):\s+(error|warning)\s+(.+)$").ok();
        if let Some(re) = re {
            for line in output.lines() {
                if let Some(caps) = re.captures(line) {
                    diagnostics.push(json!({
                        "file": caps.get(1).map(|m| m.as_str()).unwrap_or(""),
                        "line": caps.get(2).and_then(|m| m.as_str().parse::<u64>().ok()).unwrap_or(0),
                        "severity": caps.get(3).map(|m| m.as_str()).unwrap_or("error"),
                        "message": caps.get(4).map(|m| m.as_str()).unwrap_or(""),
                    }));
                }
            }
        }
    } else if command.contains("ruff check") {
        // Parse Ruff output: "path.py:line:col: CODE message"
        let re = regex::Regex::new(r"^(.+?):(\d+):(\d+):\s+([A-Z]\d+)\s+(.+)$").ok();
        if let Some(re) = re {
            for line in output.lines() {
                if let Some(caps) = re.captures(line) {
                    diagnostics.push(json!({
                        "file": caps.get(1).map(|m| m.as_str()).unwrap_or(""),
                        "line": caps.get(2).and_then(|m| m.as_str().parse::<u64>().ok()).unwrap_or(0),
                        "severity": "error",
                        "message": format!(
                            "{} {}",
                            caps.get(4).map(|m| m.as_str()).unwrap_or(""),
                            caps.get(5).map(|m| m.as_str()).unwrap_or("")
                        ).trim().to_string(),
                    }));
                }
            }
        }
    }

    diagnostics
}

fn shell_single_quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\\''"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_cargo_json_diagnostics() {
        let output = r#"{"reason":"compiler-message","message":{"level":"error","message":"mismatched types","spans":[{"file_name":"src/main.rs","line_start":10,"is_primary":true}]}}"#;
        let diagnostics = parse_diagnostics(output, "cargo check --message-format=json");
        assert_eq!(diagnostics.len(), 1);
        assert_eq!(diagnostics[0]["severity"], "error");
        assert_eq!(diagnostics[0]["line"], 10);
        assert_eq!(diagnostics[0]["file"], "src/main.rs");
    }

    #[test]
    fn parse_tsc_diagnostics() {
        let output = "src/app.ts(5,3): error TS2304: Cannot find name 'foo'.";
        let diagnostics = parse_diagnostics(output, "npx tsc --noEmit");
        assert_eq!(diagnostics.len(), 1);
        assert_eq!(diagnostics[0]["severity"], "error");
        assert_eq!(diagnostics[0]["line"], 5);
    }

    #[test]
    fn parse_ruff_diagnostics() {
        let output = "app/main.py:12:5: F821 undefined name 'x'";
        let diagnostics = parse_diagnostics(output, "ruff check .");
        assert_eq!(diagnostics.len(), 1);
        assert_eq!(diagnostics[0]["severity"], "error");
        assert_eq!(diagnostics[0]["line"], 12);
        assert_eq!(diagnostics[0]["file"], "app/main.py");
    }

    #[test]
    fn shell_quote_escapes_single_quotes() {
        assert_eq!(shell_single_quote("src/main.py"), "'src/main.py'");
        assert_eq!(shell_single_quote("a'b"), "'a'\\''b'");
    }
}
