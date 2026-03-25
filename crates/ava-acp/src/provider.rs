//! ACP agent as an LLM provider.
//!
//! Wraps an `AgentTransport` as an `LLMProvider` so external agents can be
//! used seamlessly through AVA's model router.

use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_llm::provider::{LLMProvider, ProviderCapabilities};
use ava_types::{AvaError, Message, Result, Role, StreamChunk};
use futures::{Stream, StreamExt};
use tokio::sync::Mutex;

use crate::protocol::{AgentMessage, AgentQuery, ContentBlock, PermissionMode};
use crate::transport::AgentTransport;

/// Wraps an ACP agent transport as an `LLMProvider`.
pub struct AcpAgentProvider {
    transport: Arc<Mutex<Box<dyn AgentTransport>>>,
    _agent_name: String,
    model_name: String,
    yolo: bool,
}

impl AcpAgentProvider {
    pub fn new(
        transport: Box<dyn AgentTransport>,
        agent_name: String,
        model: Option<String>,
        yolo: bool,
    ) -> Self {
        let model_name = model.unwrap_or_else(|| agent_name.clone());
        Self {
            transport: Arc::new(Mutex::new(transport)),
            _agent_name: agent_name,
            model_name,
            yolo,
        }
    }
}

/// Convert AVA messages to a single prompt string for external agents.
fn messages_to_prompt(messages: &[Message]) -> String {
    let mut parts = Vec::new();

    // System messages → context
    let system: Vec<&str> = messages
        .iter()
        .filter(|m| m.role == Role::System)
        .map(|m| m.content.as_str())
        .collect();
    if !system.is_empty() {
        parts.push(format!("Context:\n{}", system.join("\n\n")));
    }

    // Last user message → primary task
    if let Some(last_user) = messages.iter().rev().find(|m| m.role == Role::User) {
        parts.push(format!("Task:\n{}", last_user.content));
    }

    parts.join("\n\n")
}

#[async_trait]
impl LLMProvider for AcpAgentProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let prompt = messages_to_prompt(messages);
        let cwd = std::env::current_dir()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();

        let query = AgentQuery {
            prompt,
            working_directory: Some(cwd),
            permission_mode: if self.yolo {
                Some(PermissionMode::AcceptEdits)
            } else {
                None
            },
            ..AgentQuery::simple("")
        };

        let transport = self.transport.lock().await;
        let stream = transport.query(query).await?;
        let messages: Vec<AgentMessage> = stream.collect().await;

        // Extract text from all messages
        let mut output = String::new();
        for msg in &messages {
            if let Some(text) = msg.text() {
                if !output.is_empty() {
                    output.push('\n');
                }
                output.push_str(text);
            }
        }

        if output.is_empty() {
            // Check for errors
            for msg in &messages {
                if let AgentMessage::Error { message, .. } = msg {
                    return Err(AvaError::ToolError(format!("Agent error: {message}")));
                }
            }
        }

        Ok(output)
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let prompt = messages_to_prompt(messages);
        let cwd = std::env::current_dir()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();

        let query = AgentQuery {
            prompt,
            working_directory: Some(cwd),
            permission_mode: if self.yolo {
                Some(PermissionMode::AcceptEdits)
            } else {
                None
            },
            ..AgentQuery::simple("")
        };

        let transport = self.transport.lock().await;
        let agent_stream = transport.query(query).await?;

        // Map AgentMessage → StreamChunk
        let chunk_stream = agent_stream.filter_map(|msg| async move {
            match msg {
                AgentMessage::Assistant { content, .. } => {
                    // Extract text content
                    let text: String = content
                        .iter()
                        .filter_map(|b| match b {
                            ContentBlock::Text { text } => Some(text.as_str()),
                            _ => None,
                        })
                        .collect::<Vec<_>>()
                        .join("");

                    if text.is_empty() {
                        // Check for thinking
                        let thinking: Option<String> = content.iter().find_map(|b| match b {
                            ContentBlock::Thinking { thinking } => Some(thinking.clone()),
                            _ => None,
                        });
                        thinking.map(|t| StreamChunk {
                            thinking: Some(t),
                            ..StreamChunk::default()
                        })
                    } else {
                        Some(StreamChunk::text(&text))
                    }
                }
                AgentMessage::Result { result, details } => {
                    let mut chunk = StreamChunk::text(&result);
                    chunk.done = true;
                    if let Some(usage) = details.usage {
                        chunk.usage = Some(ava_types::TokenUsage {
                            input_tokens: usage.input_tokens as usize,
                            output_tokens: usage.output_tokens as usize,
                            cache_read_tokens: usage.cache_read_input_tokens.unwrap_or(0) as usize,
                            cache_creation_tokens: usage.cache_creation_input_tokens.unwrap_or(0)
                                as usize,
                        });
                    }
                    Some(chunk)
                }
                AgentMessage::Error { message, .. } => {
                    Some(StreamChunk::text(format!("Error: {message}")))
                }
                _ => None,
            }
        });

        Ok(Box::pin(chunk_stream))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        input.len() / 4
    }

    fn estimate_cost(&self, _input_tokens: usize, _output_tokens: usize) -> f64 {
        0.0 // External agents handle their own billing
    }

    fn model_name(&self) -> &str {
        &self.model_name
    }

    fn capabilities(&self) -> ProviderCapabilities {
        ProviderCapabilities {
            supports_streaming: true,
            supports_tool_use: false, // Agent handles tools internally
            supports_thinking: false,
            ..Default::default()
        }
    }
}
