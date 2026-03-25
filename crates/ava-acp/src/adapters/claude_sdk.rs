//! Claude Agent SDK adapter.
//!
//! Spawns Claude Code CLI with `--output-format stream-json` and maps the
//! Agent SDK streaming events to `AgentMessage`.

use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::Result;
use tokio::sync::Mutex;
use tracing::debug;

use crate::protocol::AgentQuery;
use crate::stdio::{StdioConfig, StdioProcess};
use crate::transport::{AgentMessageStream, AgentTransport};

use super::config::AgentConfig;

/// Adapter for the Anthropic Agent SDK (Claude Code CLI).
pub struct ClaudeSdkAdapter {
    config: AgentConfig,
    process: Arc<Mutex<Option<StdioProcess>>>,
}

impl ClaudeSdkAdapter {
    pub fn new(config: AgentConfig) -> Self {
        Self {
            config,
            process: Arc::new(Mutex::new(None)),
        }
    }

    /// Build CLI arguments from an `AgentQuery`.
    fn build_args(&self, query: &AgentQuery) -> Vec<String> {
        let mut args = Vec::new();

        // Prompt flag
        if let Some(flag) = &self.config.prompt_flag {
            args.push(flag.clone());
        }
        args.push(query.prompt.clone());

        // Headless args (e.g., --output-format stream-json --verbose)
        args.extend(self.config.headless_args.iter().cloned());

        // Model
        if let (Some(flag), Some(model)) = (&self.config.model_flag, &query.model) {
            args.push(flag.clone());
            args.push(model.clone());
        }

        // Max turns
        if let (Some(flag), Some(turns)) = (&self.config.max_turns_flag, query.max_turns) {
            args.push(flag.clone());
            args.push(turns.to_string());
        }

        // Permission mode
        if let (Some(flag), Some(mode)) =
            (&self.config.permission_mode_flag, &query.permission_mode)
        {
            args.push(flag.clone());
            args.push(mode.to_string());
        }

        // Allowed tools
        if let Some(tools) = &query.allowed_tools {
            args.push("--allowedTools".into());
            args.push(tools.join(","));
        }

        // Disallowed tools
        if let Some(tools) = &query.disallowed_tools {
            args.push("--disallowedTools".into());
            args.push(tools.join(","));
        }

        // Session resume
        if let Some(sid) = &query.session_id {
            if query.resume {
                args.push("--resume".into());
                args.push(sid.clone());
            }
        }

        // System prompt
        if let Some(sp) = &query.system_prompt {
            args.push("--system-prompt".into());
            args.push(sp.clone());
        }

        args
    }
}

#[async_trait]
impl AgentTransport for ClaudeSdkAdapter {
    async fn query(&self, query: AgentQuery) -> Result<AgentMessageStream> {
        let args = self.build_args(&query);
        let cwd = query.working_directory.clone();

        let stdio_config = StdioConfig {
            binary: self.config.binary.clone(),
            args,
            env: HashMap::new(),
            cwd,
            name: self.config.name.clone(),
        };

        let process = StdioProcess::spawn(&stdio_config)?;
        let stream = process.message_stream().await?;

        // Store process for interrupt/cancel
        *self.process.lock().await = Some(process);

        // The stream from StdioProcess already parses AgentMessage from NDJSON
        Ok(Box::pin(stream))
    }

    async fn interrupt(&self, message: String) -> Result<()> {
        let guard = self.process.lock().await;
        if let Some(process) = guard.as_ref() {
            // Send interrupt as JSON to stdin
            let interrupt_json = serde_json::json!({
                "type": "interrupt",
                "message": message
            });
            process.write_stdin(&interrupt_json.to_string()).await?;
            debug!(agent = %self.config.name, "sent interrupt");
            Ok(())
        } else {
            Err(ava_types::AvaError::ToolError(
                "no running agent to interrupt".into(),
            ))
        }
    }

    async fn cancel(&self) -> Result<()> {
        let guard = self.process.lock().await;
        if let Some(process) = guard.as_ref() {
            process.kill().await;
            debug!(agent = %self.config.name, "cancelled agent");
            Ok(())
        } else {
            Ok(()) // No process to cancel
        }
    }

    fn name(&self) -> &str {
        &self.config.name
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::adapters::config::builtin_agents;

    #[test]
    fn build_args_simple() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "claude-code")
            .unwrap();
        let adapter = ClaudeSdkAdapter::new(config);

        let query = AgentQuery::simple("fix the bug");
        let args = adapter.build_args(&query);

        assert!(args.contains(&"-p".to_string()));
        assert!(args.contains(&"fix the bug".to_string()));
        assert!(args.contains(&"--output-format".to_string()));
        assert!(args.contains(&"stream-json".to_string()));
    }

    #[test]
    fn build_args_with_options() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "claude-code")
            .unwrap();
        let adapter = ClaudeSdkAdapter::new(config);

        let query = AgentQuery {
            prompt: "test".into(),
            max_turns: Some(5),
            model: Some("opus".into()),
            permission_mode: Some(crate::protocol::PermissionMode::AcceptEdits),
            ..AgentQuery::simple("")
        };
        let args = adapter.build_args(&query);

        assert!(args.contains(&"--max-turns".to_string()));
        assert!(args.contains(&"5".to_string()));
        assert!(args.contains(&"--model".to_string()));
        assert!(args.contains(&"opus".to_string()));
        assert!(args.contains(&"--permission-mode".to_string()));
        assert!(args.contains(&"acceptEdits".to_string()));
    }
}
