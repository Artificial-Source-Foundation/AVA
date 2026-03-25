//! Legacy CLI agent adapter.
//!
//! Wraps agents that use the old stream-json format (Codex, OpenCode) or
//! plain text output (Aider, Gemini CLI) and maps to `AgentMessage`.

use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::Result;
use tokio::sync::Mutex;
use tracing::debug;

use crate::protocol::{AgentMessage, AgentQuery, AgentResultDetails, ContentBlock};
use crate::stdio::{StdioConfig, StdioProcess};
use crate::transport::{AgentMessageStream, AgentTransport};

use super::config::{AgentConfig, AgentProtocol};

/// Adapter for legacy CLI agents (stream-json or plain text).
pub struct LegacyCliAdapter {
    config: AgentConfig,
    process: Arc<Mutex<Option<StdioProcess>>>,
}

impl LegacyCliAdapter {
    pub fn new(config: AgentConfig) -> Self {
        Self {
            config,
            process: Arc::new(Mutex::new(None)),
        }
    }

    fn build_args(&self, query: &AgentQuery) -> Vec<String> {
        let mut args = Vec::new();

        // Headless args
        args.extend(self.config.headless_args.iter().cloned());

        // Prompt
        if let Some(flag) = &self.config.prompt_flag {
            args.push(flag.clone());
            args.push(query.prompt.clone());
        } else {
            // Subcommand style: binary exec "prompt"
            args.push(query.prompt.clone());
        }

        // Model
        if let (Some(flag), Some(model)) = (&self.config.model_flag, &query.model) {
            args.push(flag.clone());
            args.push(model.clone());
        }

        args
    }
}

#[async_trait]
impl AgentTransport for LegacyCliAdapter {
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
        let protocol = self.config.protocol;

        // For plain text agents, wrap lines as AssistantText messages
        if protocol == AgentProtocol::PlainText {
            let stdout = process.take_stdout().await;
            *self.process.lock().await = Some(process);

            if let Some(stdout) = stdout {
                let reader = tokio::io::BufReader::new(stdout);
                let stream = async_stream::stream! {
                    use tokio::io::AsyncBufReadExt;
                    let mut lines = reader.lines();
                    let mut output = String::new();
                    while let Ok(Some(line)) = lines.next_line().await {
                        output.push_str(&line);
                        output.push('\n');
                        yield AgentMessage::Assistant {
                            content: vec![ContentBlock::Text { text: line }],
                            session_id: None,
                        };
                    }
                    yield AgentMessage::Result {
                        result: output,
                        details: AgentResultDetails::default(),
                    };
                };
                return Ok(Box::pin(stream));
            }

            // Stdout already taken — fall through (shouldn't happen)
            return Err(ava_types::AvaError::PlatformError(
                "failed to take agent stdout".into(),
            ));
        }

        // For stream-json agents, use the standard NDJSON parsing
        let stream = process.message_stream().await?;
        *self.process.lock().await = Some(process);

        Ok(Box::pin(stream))
    }

    async fn cancel(&self) -> Result<()> {
        let guard = self.process.lock().await;
        if let Some(process) = guard.as_ref() {
            process.kill().await;
            debug!(agent = %self.config.name, "cancelled legacy agent");
        }
        Ok(())
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
    fn build_args_with_prompt_flag() {
        let config = builtin_agents()
            .into_iter()
            .find(|a| a.name == "aider")
            .unwrap();
        let adapter = LegacyCliAdapter::new(config);

        let query = AgentQuery::simple("fix bug");
        let args = adapter.build_args(&query);

        assert!(args.contains(&"--message".to_string()));
        assert!(args.contains(&"fix bug".to_string()));
    }

    #[tokio::test]
    async fn plain_text_agent_wraps_output() {
        use futures::StreamExt;
        let config = AgentConfig {
            name: "test-echo".into(),
            binary: "echo".into(),
            protocol: AgentProtocol::PlainText,
            headless_args: vec![],
            prompt_flag: None,
            model_flag: None,
            cwd_flag: None,
            max_turns_flag: None,
            permission_mode_flag: None,
            version_command: vec![],
        };

        let adapter = LegacyCliAdapter::new(config);
        let stream = adapter
            .query(AgentQuery::simple("hello world"))
            .await
            .unwrap();
        let messages: Vec<AgentMessage> = stream.collect().await;

        // Should have at least one assistant message + result
        assert!(messages.len() >= 2);
        assert!(messages.last().unwrap().is_result());
    }
}
