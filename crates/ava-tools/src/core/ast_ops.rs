use std::time::Duration;

use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

const DEFAULT_TIMEOUT_MS: u64 = 30_000;

pub struct AstOpsTool;

impl AstOpsTool {
    pub fn new(_platform: std::sync::Arc<dyn ava_platform::Platform>) -> Self {
        Self
    }
}

#[async_trait]
impl Tool for AstOpsTool {
    fn name(&self) -> &str {
        "ast_ops"
    }

    fn description(&self) -> &str {
        "AST-aware structural search (and guarded replace preview) using ast-grep if installed"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["operation", "pattern"],
            "properties": {
                "operation": {
                    "type": "string",
                    "enum": ["search", "replace_preview"],
                    "description": "Operation to run"
                },
                "pattern": {
                    "type": "string",
                    "description": "ast-grep structural pattern"
                },
                "language": {
                    "type": "string",
                    "description": "Language hint for ast-grep (default: rust)"
                },
                "path": {
                    "type": "string",
                    "description": "Path to search (default: .)"
                },
                "rewrite": {
                    "type": "string",
                    "description": "Rewrite template for replace_preview"
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        ensure_ast_grep_available(self).await?;

        let operation = args
            .get("operation")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: operation".into()))?;
        let pattern = args
            .get("pattern")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: pattern".into()))?;

        let language = args
            .get("language")
            .and_then(Value::as_str)
            .unwrap_or("rust");
        let path = args.get("path").and_then(Value::as_str).unwrap_or(".");

        let command = match operation {
            "search" => build_search_command(pattern, language, path),
            "replace_preview" => {
                let rewrite = args.get("rewrite").and_then(Value::as_str).ok_or_else(|| {
                    AvaError::ValidationError(
                        "missing required field for replace_preview: rewrite".into(),
                    )
                })?;
                build_replace_preview_command(pattern, rewrite, language, path)
            }
            other => {
                return Err(AvaError::ValidationError(format!(
                    "unsupported ast_ops operation: {other}"
                )))
            }
        };

        let args = parse_sg_args(&command)?;
        let output = tokio::time::timeout(
            Duration::from_millis(DEFAULT_TIMEOUT_MS),
            tokio::process::Command::new("sg")
                .args(&args)
                .stdout(std::process::Stdio::piped())
                .stderr(std::process::Stdio::piped())
                .output(),
        )
        .await
        .map_err(|_| AvaError::ToolError("ast-grep timed out".to_string()))?
        .map_err(|e| AvaError::ToolError(format!("Failed to execute sg: {e}")))?;

        let exit_code = output.status.code().unwrap_or(-1);
        let combined = format!(
            "{}{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        );
        Ok(ToolResult {
            call_id: String::new(),
            content: json!({
                "operation": operation,
                "command": command,
                "exit_code": exit_code,
                "output": combined,
            })
            .to_string(),
            is_error: exit_code != 0,
        })
    }
}

async fn ensure_ast_grep_available(_platform: &AstOpsTool) -> ava_types::Result<()> {
    let output = tokio::process::Command::new("sg")
        .arg("--version")
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .await;

    match output {
        Ok(status) if status.success() => Ok(()),
        _ => Err(AvaError::ToolError(
            "ast-grep binary (sg) is required for ast_ops but was not found in PATH".to_string(),
        )),
    }
}

/// Parse the generated shell command string into arguments for direct execution.
/// The command is always of the form: `sg scan --json --pattern '...' [--rewrite '...'] --lang '...' '...'`
fn parse_sg_args(command: &str) -> ava_types::Result<Vec<String>> {
    if !command.starts_with("sg scan ") {
        return Err(AvaError::PermissionDenied(
            "ast_ops only allows generated sg scan commands".to_string(),
        ));
    }

    // Use shell_words to parse the shell-quoted command
    let all_args = shell_words::split(command)
        .map_err(|e| AvaError::ValidationError(format!("Failed to parse sg command: {e}")))?;

    // Skip the leading "sg" — we invoke sg directly
    Ok(all_args.into_iter().skip(1).collect())
}

fn build_search_command(pattern: &str, language: &str, path: &str) -> String {
    format!(
        "sg scan --json --pattern {} --lang {} {}",
        quote(pattern),
        quote(language),
        quote(path)
    )
}

fn build_replace_preview_command(
    pattern: &str,
    rewrite: &str,
    language: &str,
    path: &str,
) -> String {
    format!(
        "sg scan --json --pattern {} --rewrite {} --lang {} {}",
        quote(pattern),
        quote(rewrite),
        quote(language),
        quote(path)
    )
}

fn quote(value: &str) -> String {
    let sanitized = value.replace('\0', "");
    format!(
        "'{}'",
        sanitized.replace('\\', "\\\\").replace('\'', "'\\''")
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ast_ops_metadata_is_valid() {
        let params = AstOpsTool::new(Arc::new(ava_platform::StandardPlatform)).parameters();
        assert!(params["properties"]["operation"].is_object());
    }

    #[test]
    fn ast_ops_builds_search_command() {
        let cmd = build_search_command("fn $NAME($$$ARGS)", "rust", "src");
        assert!(cmd.contains("sg scan"));
        assert!(cmd.contains("--lang 'rust'"));
    }

    #[test]
    fn ast_ops_rejects_non_sg_commands() {
        assert!(parse_sg_args("rm -rf /").is_err());
        assert!(parse_sg_args("sg scan --json --pattern 'fn main()' --lang 'rust' 'src'").is_ok());
    }
}
