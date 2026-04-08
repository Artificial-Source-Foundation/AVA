//! Claude Agent SDK adapter.
//!
//! Spawns Claude Code CLI with `--output-format stream-json` and maps the
//! Agent SDK streaming events to `AgentMessage`.

use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::Result;
use futures::StreamExt;
use tokio::sync::Mutex;
use tracing::{debug, warn};

use crate::claude;
use crate::protocol::AgentQuery;
use crate::stdio::{StdioConfig, StdioProcess};
use crate::transport::{AgentMessageStream, AgentTransport};

use super::config::{AgentConfig, NESTING_GUARD_ENV_VARS};

/// Adapter for the Anthropic Agent SDK (Claude Code CLI).
#[derive(Clone)]
pub struct ClaudeSdkAdapter {
    config: AgentConfig,
    process: Arc<Mutex<Option<Arc<StdioProcess>>>>,
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

        // Max budget
        if let (Some(flag), Some(max_budget_usd)) =
            (&self.config.max_budget_flag, query.max_budget_usd)
        {
            args.push(flag.clone());
            args.push(format!("{max_budget_usd:.2}"));
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
        let adapter = self.clone();
        let stream_query = query.clone();

        let stream = async_stream::stream! {
            let mut refreshed_auth = false;

            'attempt: loop {
                let args = adapter.build_args(&stream_query);
                let cwd = stream_query.working_directory.clone();

                let stdio_config = StdioConfig {
                    binary: adapter.config.binary.clone(),
                    args,
                    env: HashMap::new(),
                    env_remove: NESTING_GUARD_ENV_VARS
                        .iter()
                        .map(|s| s.to_string())
                        .collect(),
                    cwd,
                    name: adapter.config.name.clone(),
                };

                let process = match StdioProcess::spawn(&stdio_config) {
                    Ok(process) => Arc::new(process),
                    Err(error) => {
                        yield crate::protocol::AgentMessage::Error { message: error.to_string(), code: None };
                        break;
                    }
                };
                let attempt_stream = match process.message_stream().await {
                    Ok(stream) => stream,
                    Err(error) => {
                        yield crate::protocol::AgentMessage::Error { message: error.to_string(), code: None };
                        break;
                    }
                };
                let mut attempt_stream = Box::pin(attempt_stream);

                *adapter.process.lock().await = Some(Arc::clone(&process));

                let mut buffered = Vec::new();
                let mut retry_after_refresh = false;
                let mut yielded_output = false;

                while let Some(message) = attempt_stream.next().await {
                    if !yielded_output {
                        let should_retry = matches!(&message, crate::protocol::AgentMessage::Error { message, .. } if !refreshed_auth && claude::is_auth_expiry_error(message));

                        if should_retry {
                            match claude::refresh_oauth_token().await {
                                Ok(true) => {
                                    refreshed_auth = true;
                                    retry_after_refresh = true;
                                    process.kill().await;
                                    break;
                                }
                                Ok(false) => {}
                                Err(error) => {
                                    warn!(%error, "Claude OAuth refresh failed, yielding original auth error");
                                }
                            }
                        }

                        buffered.push(message.clone());
                        if matches!(message, crate::protocol::AgentMessage::Assistant { .. } | crate::protocol::AgentMessage::Result { .. } | crate::protocol::AgentMessage::Error { .. }) {
                            yielded_output = true;
                            for buffered_message in buffered.drain(..) {
                                yield buffered_message;
                            }
                        }
                    } else {
                        yield message;
                    }
                }

                let mut guard = adapter.process.lock().await;
                if guard.as_ref().is_some_and(|active| Arc::ptr_eq(active, &process)) {
                    *guard = None;
                }
                drop(guard);

                if retry_after_refresh {
                    continue 'attempt;
                }

                for buffered_message in buffered.drain(..) {
                    yield buffered_message;
                }
                break;
            }
        };

        Ok(Box::pin(stream))
    }

    async fn interrupt(&self, message: String) -> Result<()> {
        let process = self.process.lock().await.clone();
        if let Some(process) = process {
            // Send interrupt as JSON to stdin
            let interrupt_json = serde_json::json!({
                "type": "interrupt",
                "message": message
            });
            if let Err(error) = process.write_stdin(&interrupt_json.to_string()).await {
                self.process.lock().await.take();
                return Err(error);
            }
            debug!(agent = %self.config.name, "sent interrupt");
            Ok(())
        } else {
            Err(ava_types::AvaError::ToolError(
                "no running agent to interrupt".into(),
            ))
        }
    }

    async fn cancel(&self) -> Result<()> {
        let mut guard = self.process.lock().await;
        if let Some(process) = guard.take() {
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
    use crate::stdio::StdioConfig;

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
            max_budget_usd: Some(2.0),
            ..AgentQuery::simple("")
        };
        let args = adapter.build_args(&query);

        assert!(args.contains(&"--max-turns".to_string()));
        assert!(args.contains(&"5".to_string()));
        assert!(args.contains(&"--model".to_string()));
        assert!(args.contains(&"opus".to_string()));
        assert!(args.contains(&"--permission-mode".to_string()));
        assert!(args.contains(&"acceptEdits".to_string()));
        assert!(args.contains(&"--max-budget-usd".to_string()));
        assert!(args.contains(&"2.00".to_string()));
    }

    #[tokio::test]
    async fn cancel_clears_stored_process() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "claude-code")
            .unwrap();
        let adapter = ClaudeSdkAdapter::new(config);
        let process = Arc::new(
            StdioProcess::spawn(&StdioConfig {
                binary: "sh".into(),
                args: vec!["-c".into(), "sleep 60".into()],
                env: HashMap::new(),
                env_remove: Vec::new(),
                cwd: None,
                name: "test-sleep".into(),
            })
            .unwrap(),
        );
        *adapter.process.lock().await = Some(process);

        adapter.cancel().await.unwrap();

        assert!(adapter.process.lock().await.is_none());
    }
}
