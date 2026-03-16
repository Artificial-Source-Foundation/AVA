use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_platform::{ExecuteOptions, Platform};
use ava_sandbox::{execute_plan, select_backend, SandboxPolicy, SandboxRequest};
use ava_types::{AvaError, ToolResult};
use futures::StreamExt;
use serde_json::{json, Value};
use std::path::PathBuf;

use crate::registry::{Tool, ToolOutput};

const DEFAULT_TIMEOUT_MS: u64 = 120_000;
const MAX_OUTPUT_BYTES: usize = 100 * 1024;

pub struct BashTool {
    platform: Arc<dyn Platform>,
}

impl BashTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for BashTool {
    fn name(&self) -> &str {
        "bash"
    }

    fn description(&self) -> &str {
        "Execute shell command"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["command"],
            "properties": {
                "command": { "type": "string" },
                "timeout_ms": { "type": "integer", "minimum": 1 },
                "cwd": { "type": "string" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let command = args
            .get("command")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: command".to_string())
            })?
            .to_string();

        let timeout_ms = args
            .get("timeout_ms")
            .and_then(Value::as_u64)
            .unwrap_or(DEFAULT_TIMEOUT_MS);

        let working_dir = args.get("cwd").and_then(Value::as_str).map(PathBuf::from);

        tracing::debug!(tool = "bash", %command, "executing bash tool");

        // TODO: Invert sandbox model — sandbox ALL commands by default,
        // with narrowly approved exceptions for safe development commands.
        // Currently only install-class commands are sandboxed.
        if is_install_class(&command) {
            let cwd = args
                .get("cwd")
                .and_then(Value::as_str)
                .unwrap_or(".")
                .to_string();
            let backend = select_backend().map_err(|e| AvaError::PlatformError(e.to_string()))?;
            let policy = SandboxPolicy {
                read_only_paths: vec!["/usr".to_string(), "/bin".to_string(), "/lib".to_string()],
                writable_paths: vec![cwd.clone(), "/tmp".to_string()],
                allow_network: true,
                allow_process_spawn: true,
            };
            let request = SandboxRequest {
                command: "sh".to_string(),
                args: vec!["-c".to_string(), command.clone()],
                working_dir: Some(cwd),
                env: filtered_env(),
            };

            let plan = backend
                .build_plan(&request, &policy)
                .map_err(|e| AvaError::PlatformError(e.to_string()))?;
            let output = execute_plan(&plan, Duration::from_millis(timeout_ms))
                .await
                .map_err(|e| AvaError::PlatformError(e.to_string()))?;

            let mut rendered = format!(
                "stdout:\n{}\n\nstderr:\n{}\n\nexit_code: {}",
                output.stdout, output.stderr, output.exit_code
            );
            truncate_with_notice(&mut rendered, MAX_OUTPUT_BYTES);

            return Ok(ToolResult {
                call_id: String::new(),
                content: rendered,
                is_error: output.exit_code != 0,
            });
        }

        let output = self
            .platform
            .execute_with_options(
                &command,
                ExecuteOptions {
                    timeout: Some(Duration::from_millis(timeout_ms)),
                    working_dir,
                    env_vars: Vec::new(),
                    scrub_env: false,
                },
            )
            .await?;

        let mut rendered = format!(
            "stdout:\n{}\n\nstderr:\n{}\n\nexit_code: {}",
            output.stdout, output.stderr, output.exit_code
        );
        truncate_with_notice(&mut rendered, MAX_OUTPUT_BYTES);

        Ok(ToolResult {
            call_id: String::new(),
            content: rendered,
            is_error: output.exit_code != 0,
        })
    }

    async fn execute_streaming(&self, args: Value) -> ava_types::Result<ToolOutput> {
        let command = args
            .get("command")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: command".to_string())
            })?
            .to_string();

        // For install-class or complex commands, fall back to complete
        if is_install_class(&command) {
            return self.execute(args).await.map(ToolOutput::Complete);
        }

        let timeout_ms = args
            .get("timeout_ms")
            .and_then(Value::as_u64)
            .unwrap_or(DEFAULT_TIMEOUT_MS);
        let working_dir = args.get("cwd").and_then(Value::as_str).map(PathBuf::from);

        let stream = self
            .platform
            .execute_streaming_with_options(
                &command,
                ExecuteOptions {
                    timeout: Some(Duration::from_millis(timeout_ms)),
                    working_dir,
                    env_vars: Vec::new(),
                    scrub_env: false,
                },
            )
            .await?;

        Ok(ToolOutput::Streaming(Box::pin(stream.map(
            |item| match item {
                Ok(line) => line,
                Err(err) => format!("[error] {err}"),
            },
        ))))
    }
}

fn truncate_with_notice(content: &mut String, max_bytes: usize) {
    if content.len() <= max_bytes {
        return;
    }

    let mut idx = max_bytes;
    while !content.is_char_boundary(idx) {
        idx -= 1;
    }

    content.truncate(idx);
    content.push_str("\n[truncated]");
}

fn is_install_class(command: &str) -> bool {
    let normalized = command.trim().to_lowercase();
    let patterns = [
        "npm install",
        "yarn add",
        "pnpm add",
        "pip install",
        "pip3 install",
        "cargo install",
        "cargo add",
        "apt install",
        "apt-get install",
        "brew install",
    ];

    normalized == "npm i"
        || normalized.contains("npm i ")
        || patterns.iter().any(|pattern| normalized.contains(pattern))
}

fn filtered_env() -> Vec<(String, String)> {
    let allow = [
        "PATH",
        "HOME",
        "USER",
        "SHELL",
        "LANG",
        "LC_ALL",
        "LC_CTYPE",
        "TERM",
        "CARGO_HOME",
        "RUSTUP_HOME",
    ];

    std::env::vars()
        .filter(|(key, _)| allow.contains(&key.as_str()))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::is_install_class;

    #[test]
    fn detects_install_commands_for_sandbox_routing() {
        assert!(is_install_class("npm install lodash"));
        assert!(is_install_class("cargo add serde"));
        assert!(!is_install_class("echo hello"));
    }
}
