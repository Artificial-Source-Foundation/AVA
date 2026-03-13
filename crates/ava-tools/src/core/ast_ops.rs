use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_permissions::classifier::classify_bash_command;
use ava_platform::{ExecuteOptions, Platform};
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

const DEFAULT_TIMEOUT_MS: u64 = 30_000;

pub struct AstOpsTool {
    platform: Arc<dyn Platform>,
}

impl AstOpsTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
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
        ensure_ast_grep_available(&*self.platform).await?;

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

        validate_command(&command)?;
        let output = self
            .platform
            .execute_with_options(
                &command,
                ExecuteOptions {
                    timeout: Some(Duration::from_millis(DEFAULT_TIMEOUT_MS)),
                    working_dir: None,
                    env_vars: Vec::new(),
                },
            )
            .await?;

        let combined = format!("{}{}", output.stdout, output.stderr);
        Ok(ToolResult {
            call_id: String::new(),
            content: json!({
                "operation": operation,
                "command": command,
                "exit_code": output.exit_code,
                "output": combined,
            })
            .to_string(),
            is_error: output.exit_code != 0,
        })
    }
}

async fn ensure_ast_grep_available(platform: &dyn Platform) -> ava_types::Result<()> {
    let output = platform
        .execute_with_options(
            "sg --version",
            ExecuteOptions {
                timeout: Some(Duration::from_secs(5)),
                working_dir: None,
                env_vars: Vec::new(),
            },
        )
        .await;

    match output {
        Ok(result) if result.exit_code == 0 => Ok(()),
        _ => Err(AvaError::ToolError(
            "ast-grep binary (sg) is required for ast_ops but was not found in PATH".to_string(),
        )),
    }
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

fn validate_command(command: &str) -> ava_types::Result<()> {
    if !command.starts_with("sg scan ") {
        return Err(AvaError::PermissionDenied(
            "ast_ops only allows generated sg scan commands".to_string(),
        ));
    }

    let classification = classify_bash_command(command);
    if classification.blocked {
        return Err(AvaError::PermissionDenied(
            "ast_ops generated command failed safety validation".to_string(),
        ));
    }
    Ok(())
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
    fn ast_ops_requires_low_risk_commands() {
        assert!(validate_command("rm -rf /").is_err());
        assert!(
            validate_command("sg scan --json --pattern 'fn main()' --lang 'rust' 'src'").is_ok()
        );
    }
}
