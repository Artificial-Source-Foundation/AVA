use async_trait::async_trait;
use ava_llm::provider::LLMProvider;
use ava_types::{AvaError, Message, Result, Role};
use futures::{Stream, StreamExt};
use std::pin::Pin;

use crate::config::{CLIAgentConfig, CLIAgentEvent};
use crate::runner::{CLIAgentRunner, RunOptions};

/// Wraps a CLI agent as an LLMProvider.
pub struct CLIAgentLLMProvider {
    runner: CLIAgentRunner,
    model_name: String,
    yolo: bool,
}

impl CLIAgentLLMProvider {
    #[must_use]
    pub fn new(config: CLIAgentConfig, model: Option<String>, yolo: bool) -> Self {
        let model_name = model.unwrap_or_else(|| config.name.clone());
        Self {
            runner: CLIAgentRunner::new(config),
            model_name,
            yolo,
        }
    }

    pub fn cancel(&self) {
        self.runner.cancel();
    }
}

#[async_trait]
impl LLMProvider for CLIAgentLLMProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let prompt = messages_to_prompt(messages);
        let cwd = std::env::current_dir()
            .map_err(|e| AvaError::IoError(format!("failed to resolve cwd: {e}")))?
            .to_string_lossy()
            .to_string();

        let result = self
            .runner
            .run(RunOptions {
                prompt,
                cwd,
                model: Some(self.model_name.clone()),
                yolo: self.yolo,
                ..RunOptions::default()
            })
            .await?;

        if result.success {
            Ok(result.output)
        } else {
            Err(AvaError::ToolError(format!(
                "CLI agent exited with code {}: {}",
                result.exit_code, result.output
            )))
        }
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = String> + Send>>> {
        let (tx, rx) = tokio::sync::mpsc::channel(256);
        let prompt = messages_to_prompt(messages);
        let runner = self.runner.clone();
        let model = self.model_name.clone();
        let yolo = self.yolo;

        tokio::spawn(async move {
            let error_tx = tx.clone();
            let cwd = std::env::current_dir()
                .map(|dir| dir.to_string_lossy().to_string())
                .unwrap_or_else(|_| ".".to_string());

            if let Err(err) = runner
                .stream(
                    RunOptions {
                        prompt,
                        cwd,
                        model: Some(model),
                        yolo,
                        ..RunOptions::default()
                    },
                    tx,
                )
                .await
            {
                let _ = error_tx
                    .send(CLIAgentEvent::Text {
                        content: format!("CLI stream failure: {err}"),
                    })
                    .await;
            }
        });

        let stream = tokio_stream::wrappers::ReceiverStream::new(rx).filter_map(|event| {
            futures::future::ready(match event {
                CLIAgentEvent::Text { content } => Some(content),
                _ => None,
            })
        });

        Ok(Box::pin(stream))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0
    }

    fn model_name(&self) -> &str {
        &self.model_name
    }
}

/// Convert AVA messages to a single prompt string for CLI agents.
pub fn messages_to_prompt(messages: &[Message]) -> String {
    if messages.is_empty() {
        return String::new();
    }

    let mut parts = Vec::new();

    let system_messages: Vec<&str> = messages
        .iter()
        .filter(|m| m.role == Role::System)
        .map(|m| m.content.trim())
        .filter(|content| !content.is_empty())
        .collect();

    if !system_messages.is_empty() {
        parts.push(format!("Context:\n{}", system_messages.join("\n\n")));
    }

    let primary_message_id = messages
        .iter()
        .rev()
        .find(|m| m.role == Role::User)
        .map(|m| m.id);

    let mut transcript = Vec::new();
    for message in messages {
        if Some(message.id) == primary_message_id {
            continue;
        }

        match message.role {
            Role::System => {}
            Role::User => transcript.push(format!("User: {}", message.content.trim())),
            Role::Assistant => {
                transcript.push(format!("Assistant: {}", message.content.trim()))
            }
            Role::Tool => transcript.push(format!("Tool: {}", message.content.trim())),
        }
    }

    if !transcript.is_empty() {
        parts.push(format!("Conversation so far:\n{}", transcript.join("\n")));
    }

    let primary_prompt = messages
        .iter()
        .rev()
        .find(|m| m.role == Role::User)
        .map(|m| m.content.trim().to_string())
        .unwrap_or_else(|| messages.last().map(|m| m.content.clone()).unwrap_or_default());

    parts.push(format!("Primary task:\n{primary_prompt}"));
    parts.join("\n\n")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn user_message(content: &str) -> Message {
        Message::new(Role::User, content)
    }

    #[test]
    fn prompt_from_single_user_message() {
        let prompt = messages_to_prompt(&[user_message("Implement search")]);
        assert!(prompt.contains("Primary task:\nImplement search"));
    }

    #[test]
    fn prompt_from_system_and_user_messages() {
        let messages = vec![
            Message::new(Role::System, "Always write tests"),
            Message::new(Role::User, "Fix parser"),
        ];

        let prompt = messages_to_prompt(&messages);
        assert!(prompt.contains("Context:\nAlways write tests"));
        assert!(prompt.contains("Primary task:\nFix parser"));
    }

    #[test]
    fn estimate_cost_is_zero() {
        let provider = CLIAgentLLMProvider::new(
            CLIAgentConfig {
                name: "opencode".to_string(),
                binary: "opencode".to_string(),
                prompt_flag: crate::config::PromptMode::Subcommand("run".to_string()),
                non_interactive_flags: vec![],
                yolo_flags: vec![],
                output_format_flag: None,
                allowed_tools_flag: None,
                cwd_flag: None,
                model_flag: None,
                session_flag: None,
                supports_stream_json: false,
                supports_tool_scoping: false,
                tier_tool_scopes: None,
                version_command: vec!["opencode".to_string(), "--version".to_string()],
            },
            Some("test-model".to_string()),
            false,
        );

        assert_eq!(provider.estimate_cost(200, 100), 0.0);
    }

    #[test]
    fn model_name_returns_configured_name() {
        let provider = CLIAgentLLMProvider::new(
            CLIAgentConfig {
                name: "aider".to_string(),
                binary: "aider".to_string(),
                prompt_flag: crate::config::PromptMode::Flag("--message".to_string()),
                non_interactive_flags: vec![],
                yolo_flags: vec![],
                output_format_flag: None,
                allowed_tools_flag: None,
                cwd_flag: None,
                model_flag: None,
                session_flag: None,
                supports_stream_json: false,
                supports_tool_scoping: false,
                tier_tool_scopes: None,
                version_command: vec!["aider".to_string(), "--version".to_string()],
            },
            Some("my-model".to_string()),
            false,
        );

        assert_eq!(provider.model_name(), "my-model");
    }

    #[test]
    fn provider_creation_from_config() {
        let provider = CLIAgentLLMProvider::new(
            CLIAgentConfig {
                name: "codex".to_string(),
                binary: "codex".to_string(),
                prompt_flag: crate::config::PromptMode::Subcommand("exec".to_string()),
                non_interactive_flags: vec![],
                yolo_flags: vec!["--full-auto".to_string()],
                output_format_flag: Some("--json".to_string()),
                allowed_tools_flag: None,
                cwd_flag: Some("--cwd".to_string()),
                model_flag: Some("--model".to_string()),
                session_flag: None,
                supports_stream_json: true,
                supports_tool_scoping: false,
                tier_tool_scopes: None,
                version_command: vec!["codex".to_string(), "--version".to_string()],
            },
            None,
            true,
        );

        assert_eq!(provider.model_name(), "codex");
    }
}
