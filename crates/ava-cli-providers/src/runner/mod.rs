mod args;
mod execution;

use crate::config::{CLIAgentConfig, CLIAgentEvent, CLIAgentResult};
use ava_types::Result;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

#[derive(Clone)]
pub struct CLIAgentRunner {
    pub(crate) config: CLIAgentConfig,
    pub(crate) cancel: CancellationToken,
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
        use std::process::Stdio;
        use tokio::process::Command;

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
