use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_permissions::classifier::classify_bash_command;
use ava_permissions::tags::RiskLevel;
use ava_platform::{ExecuteOptions, Platform};
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct LspOpsTool {
    platform: Arc<dyn Platform>,
}

impl LspOpsTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for LspOpsTool {
    fn name(&self) -> &str {
        "lsp_ops"
    }

    fn description(&self) -> &str {
        "Read-only LSP capability probe and server status checks for supported languages"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["operation"],
            "properties": {
                "operation": {
                    "type": "string",
                    "enum": ["server_status", "list_supported"],
                    "description": "LSP operation to run"
                },
                "language": {
                    "type": "string",
                    "description": "Language key for server_status (rust, typescript, python, go)"
                },
                "server_command": {
                    "type": "string",
                    "description": "Optional server binary override (binary name only)"
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let operation = args
            .get("operation")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: operation".into()))?;

        match operation {
            "list_supported" => Ok(ToolResult {
                call_id: String::new(),
                content: json!({
                    "supported_languages": ["rust", "typescript", "python", "go"],
                    "default_servers": {
                        "rust": "rust-analyzer",
                        "typescript": "typescript-language-server --stdio",
                        "python": "pylsp",
                        "go": "gopls"
                    }
                })
                .to_string(),
                is_error: false,
            }),
            "server_status" => {
                let language = args
                    .get("language")
                    .and_then(Value::as_str)
                    .ok_or_else(|| {
                        AvaError::ValidationError(
                            "missing required field for server_status: language".into(),
                        )
                    })?;

                let probe_cmd = args
                    .get("server_command")
                    .and_then(Value::as_str)
                    .map(parse_probe_override)
                    .transpose()?
                    .unwrap_or_else(|| default_probe_command(language));

                validate_probe_command(&probe_cmd)?;

                let result = self
                    .platform
                    .execute_with_options(
                        &probe_cmd,
                        ExecuteOptions {
                            timeout: Some(Duration::from_secs(5)),
                            working_dir: None,
                            env_vars: Vec::new(),
                        },
                    )
                    .await;

                let payload = match result {
                    Ok(output) => json!({
                        "language": language,
                        "probe_command": probe_cmd,
                        "available": output.exit_code == 0,
                        "exit_code": output.exit_code,
                        "stdout": output.stdout,
                        "stderr": output.stderr,
                    }),
                    Err(err) => json!({
                        "language": language,
                        "probe_command": probe_cmd,
                        "available": false,
                        "error": err.to_string(),
                    }),
                };

                let is_error = payload
                    .get("available")
                    .and_then(Value::as_bool)
                    .map(|available| !available)
                    .unwrap_or(true);

                Ok(ToolResult {
                    call_id: String::new(),
                    content: serde_json::to_string_pretty(&payload)
                        .map_err(|e| AvaError::SerializationError(e.to_string()))?,
                    is_error,
                })
            }
            other => Err(AvaError::ValidationError(format!(
                "unsupported lsp_ops operation: {other}"
            ))),
        }
    }
}

fn default_probe_command(language: &str) -> String {
    match language {
        "rust" => "rust-analyzer --version".to_string(),
        "typescript" => "typescript-language-server --version".to_string(),
        "python" => "pylsp --version".to_string(),
        "go" => "gopls version".to_string(),
        _ => "false".to_string(),
    }
}

fn parse_probe_override(server_command: &str) -> ava_types::Result<String> {
    let binary = server_command
        .split_whitespace()
        .next()
        .filter(|s| !s.is_empty())
        .ok_or_else(|| AvaError::ValidationError("server_command cannot be empty".to_string()))?;

    if !is_allowed_lsp_binary(binary) {
        return Err(AvaError::ValidationError(format!(
            "unsupported LSP server binary override: {binary}"
        )));
    }

    Ok(format!("{binary} --version"))
}

fn is_allowed_lsp_binary(binary: &str) -> bool {
    matches!(
        binary,
        "rust-analyzer" | "typescript-language-server" | "pylsp" | "gopls"
    )
}

fn validate_probe_command(command: &str) -> ava_types::Result<()> {
    let classification = classify_bash_command(command);
    if classification.blocked || classification.risk_level > RiskLevel::Low {
        return Err(AvaError::PermissionDenied(
            "lsp_ops probe command must be safe or low-risk".to_string(),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lsp_ops_default_probe_command_maps_languages() {
        assert!(default_probe_command("rust").contains("rust-analyzer"));
        assert!(default_probe_command("go").contains("gopls"));
    }

    #[test]
    fn lsp_ops_restricts_probe_overrides_to_known_binaries() {
        assert!(parse_probe_override("rust-analyzer").is_ok());
        assert!(parse_probe_override("curl").is_err());
    }

    #[test]
    fn lsp_ops_rejects_unsafe_probe_command() {
        assert!(validate_probe_command("rm -rf /").is_err());
    }

    #[tokio::test]
    async fn lsp_ops_supports_list_operation() {
        let tool = LspOpsTool::new(Arc::new(ava_platform::StandardPlatform));
        let result = tool.execute(json!({"operation": "list_supported"})).await;
        assert!(result.is_ok());
    }
}
