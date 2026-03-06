use crate::config::{CLIAgentConfig, CLIAgentEvent, CLIAgentResult, PromptMode, TokenUsage};
use ava_types::{AvaError, Result};
use std::process::Stdio;
use std::time::Instant;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

#[derive(Clone)]
pub struct CLIAgentRunner {
    config: CLIAgentConfig,
    cancel: CancellationToken,
}

#[derive(Debug, Clone, Default)]
pub struct RunOptions {
    pub prompt: String,
    pub cwd: String,
    pub model: Option<String>,
    pub yolo: bool,
    pub allowed_tools: Option<Vec<String>>,
    pub session_id: Option<String>,
    pub timeout_ms: Option<u64>,
    pub env: Option<Vec<(String, String)>>,
}

impl CLIAgentRunner {
    pub fn new(config: CLIAgentConfig) -> Self {
        Self {
            config,
            cancel: CancellationToken::new(),
        }
    }

    pub fn config(&self) -> &CLIAgentConfig {
        &self.config
    }

    /// Check if the CLI binary is installed and accessible.
    pub async fn is_available(&self) -> bool {
        self.version().await.is_some()
    }

    /// Get the CLI version string.
    pub async fn version(&self) -> Option<String> {
        let (program, args) = self.version_parts();
        let output = Command::new(program)
            .args(args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .output()
            .await
            .ok()?;

        if !output.status.success() {
            return None;
        }

        let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if !stdout.is_empty() {
            return Some(stdout);
        }

        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        if !stderr.is_empty() {
            return Some(stderr);
        }

        Some("unknown".to_string())
    }

    /// Run the CLI agent with a prompt, return structured result.
    pub async fn run(&self, options: RunOptions) -> Result<CLIAgentResult> {
        self.run_internal(options, None).await
    }

    /// Run with streaming — send events through a channel.
    pub async fn stream(
        &self,
        options: RunOptions,
        tx: mpsc::Sender<CLIAgentEvent>,
    ) -> Result<CLIAgentResult> {
        self.run_internal(options, Some(tx)).await
    }

    /// Cancel a running agent.
    pub fn cancel(&self) {
        self.cancel.cancel();
    }

    fn version_parts(&self) -> (&str, Vec<String>) {
        if self.config.version_command.is_empty() {
            return (&self.config.binary, vec!["--version".to_string()]);
        }

        let mut parts = self.config.version_command.iter();
        let program = parts
            .next()
            .map(String::as_str)
            .unwrap_or(self.config.binary.as_str());
        let args = parts.cloned().collect();
        (program, args)
    }

    /// Build the command args from config + options.
    pub(crate) fn build_args(&self, options: &RunOptions) -> Vec<String> {
        let mut args = Vec::new();

        match &self.config.prompt_flag {
            PromptMode::Flag(flag) => {
                args.push(flag.clone());
                args.push(options.prompt.clone());
            }
            PromptMode::Subcommand(cmd) => {
                args.push(cmd.clone());
                args.push(options.prompt.clone());
            }
        }

        args.extend(self.config.non_interactive_flags.clone());

        if options.yolo {
            args.extend(self.config.yolo_flags.clone());
        }

        if self.config.supports_stream_json {
            if let Some(flag) = &self.config.output_format_flag {
                args.push(flag.clone());
                args.push("stream-json".to_string());
            }
        }

        if let (Some(tools), Some(flag)) = (&options.allowed_tools, &self.config.allowed_tools_flag) {
            args.push(flag.clone());
            args.push(tools.join(","));
        }

        if let Some(flag) = &self.config.cwd_flag {
            args.push(flag.clone());
            args.push(options.cwd.clone());
        }

        if let (Some(model), Some(flag)) = (&options.model, &self.config.model_flag) {
            args.push(flag.clone());
            args.push(model.clone());
        }

        if let (Some(session), Some(flag)) = (&options.session_id, &self.config.session_flag) {
            args.push(flag.clone());
            args.push(session.clone());
        }

        args
    }

    /// Parse a line of stream-json output into an event.
    pub(crate) fn parse_event(line: &str) -> Option<CLIAgentEvent> {
        serde_json::from_str(line).ok()
    }

    async fn run_internal(
        &self,
        options: RunOptions,
        tx: Option<mpsc::Sender<CLIAgentEvent>>,
    ) -> Result<CLIAgentResult> {
        let args = self.build_args(&options);
        let mut cmd = Command::new(&self.config.binary);
        cmd.args(&args).stdout(Stdio::piped()).stderr(Stdio::piped());
        if self.config.cwd_flag.is_none() {
            cmd.current_dir(&options.cwd);
        }

        if let Some(env) = &options.env {
            for (key, val) in env {
                cmd.env(key, val);
            }
        }

        let started = Instant::now();
        let mut child = cmd
            .spawn()
            .map_err(|e| AvaError::PlatformError(format!("failed to spawn {}: {e}", self.config.binary)))?;

        let stdout = child
            .stdout
            .take()
            .ok_or_else(|| AvaError::PlatformError("failed to capture stdout".to_string()))?;
        let stderr = child
            .stderr
            .take()
            .ok_or_else(|| AvaError::PlatformError("failed to capture stderr".to_string()))?;

        let supports_json = self.config.supports_stream_json;
        let tx_stdout = tx.clone();
        let stdout_task = tokio::spawn(async move {
            let mut lines = BufReader::new(stdout).lines();
            let mut output = String::new();
            let mut events = Vec::new();
            let mut tokens = None;

            while let Ok(Some(line)) = lines.next_line().await {
                if supports_json {
                    if let Some(event) = CLIAgentRunner::parse_event(&line) {
                        if let CLIAgentEvent::Usage {
                            input_tokens,
                            output_tokens,
                        } = &event
                        {
                            tokens = Some(TokenUsage {
                                input: *input_tokens,
                                output: *output_tokens,
                            });
                        }

                        if let CLIAgentEvent::Text { content } = &event {
                            output.push_str(content);
                            output.push('\n');
                        }

                        if let Some(sender) = &tx_stdout {
                            let _ = sender.send(event.clone()).await;
                        }

                        events.push(event);
                    }
                } else {
                    output.push_str(&line);
                    output.push('\n');
                    let event = CLIAgentEvent::Text { content: line };
                    if let Some(sender) = &tx_stdout {
                        let _ = sender.send(event.clone()).await;
                    }
                    events.push(event);
                }
            }

            (output, events, tokens)
        });

        let stderr_task = tokio::spawn(async move {
            let mut lines = BufReader::new(stderr).lines();
            let mut output = String::new();
            while let Ok(Some(line)) = lines.next_line().await {
                if !line.is_empty() {
                    output.push_str(&line);
                    output.push('\n');
                }
            }
            output
        });

        let status = {
            let mut timed_out = false;
            let mut cancelled = false;
            let wait_result = if let Some(timeout_ms) = options.timeout_ms {
                tokio::select! {
                    result = child.wait() => Some(result),
                    _ = tokio::time::sleep(std::time::Duration::from_millis(timeout_ms)) => {
                        timed_out = true;
                        None
                    }
                    _ = self.cancel.cancelled() => {
                        cancelled = true;
                        None
                    }
                }
            } else {
                tokio::select! {
                    result = child.wait() => Some(result),
                    _ = self.cancel.cancelled() => {
                        cancelled = true;
                        None
                    }
                }
            };

            if let Some(result) = wait_result {
                result.map_err(|e| {
                    AvaError::PlatformError(format!("failed waiting for child process: {e}"))
                })?
            } else {
                let _ = child.kill().await;
                if timed_out {
                    return Err(AvaError::TimeoutError(format!(
                        "CLI agent '{}' timed out after {}ms",
                        self.config.name,
                        options.timeout_ms.unwrap_or_default()
                    )));
                }

                if cancelled {
                    return Err(AvaError::TimeoutError(
                        "CLI agent execution cancelled".to_string(),
                    ));
                }

                return Err(AvaError::PlatformError(
                    "CLI agent stopped for unknown reason".to_string(),
                ));
            }
        };

        let (mut output, events, tokens_used) = stdout_task
            .await
            .map_err(|e| AvaError::PlatformError(format!("stdout task failed: {e}")))?;
        let stderr_output = stderr_task
            .await
            .map_err(|e| AvaError::PlatformError(format!("stderr task failed: {e}")))?;

        let success = status.success();
        if !success && !stderr_output.is_empty() {
            if !output.is_empty() {
                output.push('\n');
            }
            output.push_str(&stderr_output);
        }

        let exit_code = status.code().unwrap_or(-1);
        let duration_ms = started.elapsed().as_millis() as u64;
        Ok(CLIAgentResult {
            success,
            output: output.trim().to_string(),
            exit_code,
            events,
            tokens_used,
            duration_ms,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::PromptMode;

    fn claude_test_config() -> CLIAgentConfig {
        CLIAgentConfig {
            name: "claude-code".to_string(),
            binary: "claude".to_string(),
            prompt_flag: PromptMode::Flag("-p".to_string()),
            non_interactive_flags: vec!["--no-user-prompt".to_string()],
            yolo_flags: vec!["--dangerously-skip-permissions".to_string()],
            output_format_flag: Some("--output-format".to_string()),
            allowed_tools_flag: Some("--allowedTools".to_string()),
            cwd_flag: Some("--cwd".to_string()),
            model_flag: Some("--model".to_string()),
            session_flag: Some("--session-id".to_string()),
            supports_stream_json: true,
            supports_tool_scoping: true,
            tier_tool_scopes: None,
            version_command: vec!["claude".to_string(), "--version".to_string()],
        }
    }

    #[test]
    fn builds_flag_prompt_args_for_claude() {
        let runner = CLIAgentRunner::new(claude_test_config());
        let options = RunOptions {
            prompt: "implement feature".to_string(),
            cwd: "/tmp/project".to_string(),
            model: Some("sonnet".to_string()),
            yolo: false,
            allowed_tools: None,
            session_id: None,
            timeout_ms: None,
            env: None,
        };

        let args = runner.build_args(&options);
        assert_eq!(args[0], "-p");
        assert_eq!(args[1], "implement feature");
        assert!(args.contains(&"--no-user-prompt".to_string()));
        assert!(args.contains(&"--output-format".to_string()));
        assert!(args.contains(&"stream-json".to_string()));
        assert!(args.contains(&"--cwd".to_string()));
        assert!(args.contains(&"/tmp/project".to_string()));
        assert!(args.contains(&"--model".to_string()));
        assert!(args.contains(&"sonnet".to_string()));
    }

    #[test]
    fn builds_subcommand_prompt_args_for_codex() {
        let mut cfg = claude_test_config();
        cfg.binary = "codex".to_string();
        cfg.prompt_flag = PromptMode::Subcommand("exec".to_string());

        let runner = CLIAgentRunner::new(cfg);
        let args = runner.build_args(&RunOptions {
            prompt: "fix bug".to_string(),
            cwd: "/tmp/repo".to_string(),
            ..RunOptions::default()
        });

        assert_eq!(args[0], "exec");
        assert_eq!(args[1], "fix bug");
    }

    #[test]
    fn includes_yolo_flags_when_enabled() {
        let runner = CLIAgentRunner::new(claude_test_config());
        let args = runner.build_args(&RunOptions {
            prompt: "test".to_string(),
            cwd: "/tmp".to_string(),
            yolo: true,
            ..RunOptions::default()
        });

        assert!(args.contains(&"--dangerously-skip-permissions".to_string()));
    }

    #[test]
    fn includes_tool_scoping_when_supported() {
        let runner = CLIAgentRunner::new(claude_test_config());
        let args = runner.build_args(&RunOptions {
            prompt: "test".to_string(),
            cwd: "/tmp".to_string(),
            allowed_tools: Some(vec!["Read".to_string(), "Edit".to_string()]),
            ..RunOptions::default()
        });

        assert!(args.contains(&"--allowedTools".to_string()));
        assert!(args.contains(&"Read,Edit".to_string()));
    }

    #[test]
    fn parses_text_event() {
        let line = r#"{"type":"text","content":"hello"}"#;
        let event = CLIAgentRunner::parse_event(line);
        assert_eq!(
            event,
            Some(CLIAgentEvent::Text {
                content: "hello".to_string()
            })
        );
    }

    #[test]
    fn parses_tool_use_event() {
        let line = r#"{"type":"tool_use","tool_name":"Read","tool_args":{"path":"x"}}"#;
        let event = CLIAgentRunner::parse_event(line);
        assert!(matches!(
            event,
            Some(CLIAgentEvent::ToolUse { tool_name, .. }) if tool_name == "Read"
        ));
    }

    #[test]
    fn parses_usage_event() {
        let line = r#"{"type":"usage","input_tokens":9,"output_tokens":3}"#;
        let event = CLIAgentRunner::parse_event(line);
        assert_eq!(
            event,
            Some(CLIAgentEvent::Usage {
                input_tokens: 9,
                output_tokens: 3
            })
        );
    }

    #[test]
    fn invalid_json_returns_none() {
        assert_eq!(CLIAgentRunner::parse_event("not-json"), None);
    }

    #[tokio::test]
    async fn unavailable_binary_returns_false() {
        let mut cfg = claude_test_config();
        cfg.binary = "__definitely_missing_binary__".to_string();
        cfg.version_command = vec!["__definitely_missing_binary__".to_string(), "--version".to_string()];

        let runner = CLIAgentRunner::new(cfg);
        assert!(!runner.is_available().await);
    }
}
