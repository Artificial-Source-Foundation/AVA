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
use tracing::warn;

use crate::protocol::{AgentMessage, AgentQuery, ContentBlock, PermissionMode};
use crate::session_store;
use crate::transport::AgentTransport;

/// Wraps an ACP agent transport as an `LLMProvider`.
pub struct AcpAgentProvider {
    transport: Arc<Mutex<Box<dyn AgentTransport>>>,
    agent_name: String,
    model_name: String,
    yolo: bool,
    store_path_override: Option<std::path::PathBuf>,
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
            agent_name,
            model_name,
            yolo,
            store_path_override: None,
        }
    }

    #[cfg(test)]
    pub fn with_store_path_for_tests(mut self, path: std::path::PathBuf) -> Self {
        self.store_path_override = Some(path);
        self
    }

    fn prepare_query(
        &self,
        messages: &[Message],
        cwd: String,
        resume_session_id: Option<String>,
    ) -> AgentQuery {
        AgentQuery {
            prompt: messages_to_prompt(messages),
            working_directory: Some(cwd),
            permission_mode: if self.yolo {
                Some(PermissionMode::AcceptEdits)
            } else {
                None
            },
            session_id: resume_session_id.clone(),
            resume: resume_session_id.is_some(),
            ..AgentQuery::simple("")
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

fn message_session_id(message: &AgentMessage) -> Option<&str> {
    match message {
        AgentMessage::System { session_id, .. } | AgentMessage::Assistant { session_id, .. } => {
            session_id.as_deref()
        }
        AgentMessage::Result { details, .. } => details.session_id.as_deref(),
        AgentMessage::Error { .. } | AgentMessage::Unknown => None,
    }
}

fn lookup_session_id(
    store_path_override: Option<std::path::PathBuf>,
    agent_name: String,
    model_name: String,
    cwd: String,
    messages: Vec<Message>,
) -> Option<String> {
    #[cfg(test)]
    if let Some(path) = store_path_override.as_deref() {
        return session_store::lookup_session_for_path(
            path,
            &agent_name,
            &model_name,
            &cwd,
            &messages,
        );
    }

    #[cfg(not(test))]
    let _ = store_path_override;

    session_store::lookup_session(&agent_name, &model_name, &cwd, &messages)
}

fn store_session_id(
    store_path_override: Option<std::path::PathBuf>,
    agent_name: String,
    model_name: String,
    cwd: String,
    messages: Vec<Message>,
    session_id: String,
) {
    #[cfg(test)]
    if let Some(path) = store_path_override.as_deref() {
        session_store::store_session_for_path(
            path,
            &agent_name,
            &model_name,
            &cwd,
            &messages,
            &session_id,
        );
        return;
    }

    #[cfg(not(test))]
    let _ = store_path_override;

    session_store::store_session(&agent_name, &model_name, &cwd, &messages, &session_id);
}

#[async_trait]
impl LLMProvider for AcpAgentProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let cwd = std::env::current_dir()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();
        let store_path_override = self.store_path_override.clone();
        let agent_name = self.agent_name.clone();
        let model_name = self.model_name.clone();
        let lookup_cwd = cwd.clone();
        let lookup_messages = messages.to_vec();
        let resume_session_id = tokio::task::spawn_blocking(move || {
            lookup_session_id(
                store_path_override,
                agent_name,
                model_name,
                lookup_cwd,
                lookup_messages,
            )
        })
        .await
        .unwrap_or_else(|error| {
            warn!(%error, "failed to join ACP session lookup task");
            None
        });
        let query = self.prepare_query(messages, cwd.clone(), resume_session_id);

        let transport = self.transport.lock().await;
        let stream = transport.query(query).await?;
        let agent_messages: Vec<AgentMessage> = stream.collect().await;

        if let Some(session_id) = agent_messages.iter().find_map(message_session_id) {
            let store_path_override = self.store_path_override.clone();
            let agent_name = self.agent_name.clone();
            let model_name = self.model_name.clone();
            let store_cwd = cwd.clone();
            let request_messages = messages.to_vec();
            let session_id = session_id.to_string();
            if let Err(error) = tokio::task::spawn_blocking(move || {
                store_session_id(
                    store_path_override,
                    agent_name,
                    model_name,
                    store_cwd,
                    request_messages,
                    session_id,
                );
            })
            .await
            {
                warn!(%error, "failed to join ACP session-store task");
            }
        }

        let mut output = String::new();
        for msg in &agent_messages {
            if let Some(text) = msg.text() {
                if !output.is_empty() {
                    output.push('\n');
                }
                output.push_str(text);
            }
        }

        if output.is_empty() {
            for msg in &agent_messages {
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
        let cwd = std::env::current_dir()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();
        let store_path_override = self.store_path_override.clone();
        let lookup_agent_name = self.agent_name.clone();
        let lookup_model_name = self.model_name.clone();
        let lookup_cwd = cwd.clone();
        let request_messages = messages.to_vec();
        let lookup_messages = request_messages.clone();
        let stream_store_path_override = self.store_path_override.clone();
        let resume_session_id = tokio::task::spawn_blocking(move || {
            lookup_session_id(
                store_path_override,
                lookup_agent_name,
                lookup_model_name,
                lookup_cwd,
                lookup_messages,
            )
        })
        .await
        .unwrap_or_else(|error| {
            warn!(%error, "failed to join ACP session lookup task");
            None
        });
        let query = self.prepare_query(messages, cwd.clone(), resume_session_id);
        let agent_name = self.agent_name.clone();
        let model_name = self.model_name.clone();

        let transport = self.transport.lock().await;
        let agent_stream = transport.query(query).await?;
        drop(transport);

        let chunk_stream = async_stream::stream! {
            let mut stored_session = false;
            futures::pin_mut!(agent_stream);

            while let Some(msg) = agent_stream.next().await {
                if !stored_session {
                    if let Some(session_id) = message_session_id(&msg) {
                        let store_path_override = stream_store_path_override.clone();
                        let agent_name = agent_name.clone();
                        let model_name = model_name.clone();
                        let store_cwd = cwd.clone();
                        let request_messages = request_messages.clone();
                        let session_id = session_id.to_string();
                        if let Err(error) = tokio::task::spawn_blocking(move || {
                            store_session_id(
                                store_path_override,
                                agent_name,
                                model_name,
                                store_cwd,
                                request_messages,
                                session_id,
                            );
                        })
                        .await
                        {
                            warn!(%error, "failed to join ACP session-store task");
                        }
                        stored_session = true;
                    }
                }

                let chunk = match msg {
                    AgentMessage::Assistant { content, .. } => {
                        let text: String = content
                            .iter()
                            .filter_map(|b| match b {
                                ContentBlock::Text { text } => Some(text.as_str()),
                                _ => None,
                            })
                            .collect::<Vec<_>>()
                            .join("");

                        if text.is_empty() {
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
                                cache_read_tokens: usage.cache_read_input_tokens.unwrap_or(0)
                                    as usize,
                                cache_creation_tokens: usage
                                    .cache_creation_input_tokens
                                    .unwrap_or(0) as usize,
                            });
                        }
                        Some(chunk)
                    }
                    AgentMessage::Error { message, .. } => {
                        Some(StreamChunk::text(format!("Error: {message}")))
                    }
                    AgentMessage::System { .. } | AgentMessage::Unknown => None,
                };

                if let Some(chunk) = chunk {
                    yield chunk;
                }
            }
        };

        Ok(Box::pin(chunk_stream))
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

    fn capabilities(&self) -> ProviderCapabilities {
        ProviderCapabilities {
            supports_streaming: true,
            supports_tool_use: false,
            supports_thinking: false,
            ..Default::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    struct RecordingTransport {
        queries: Arc<std::sync::Mutex<Vec<AgentQuery>>>,
        responses: Arc<std::sync::Mutex<Vec<Vec<AgentMessage>>>>,
    }

    #[async_trait]
    impl AgentTransport for RecordingTransport {
        async fn query(&self, query: AgentQuery) -> Result<crate::transport::AgentMessageStream> {
            self.queries.lock().unwrap().push(query);
            let response = self.responses.lock().unwrap().remove(0);
            Ok(Box::pin(futures::stream::iter(response)))
        }

        fn name(&self) -> &str {
            "claude-code"
        }
    }

    #[tokio::test]
    async fn generate_reuses_persisted_session_for_extended_history() {
        let dir = tempfile::tempdir().unwrap();
        let store_path = dir.path().join("sessions.json");

        let queries = Arc::new(std::sync::Mutex::new(Vec::new()));
        let responses = Arc::new(std::sync::Mutex::new(vec![
            vec![
                AgentMessage::System {
                    message: String::new(),
                    session_id: Some("sess-1".to_string()),
                },
                AgentMessage::Result {
                    result: "done".to_string(),
                    details: Default::default(),
                },
            ],
            vec![AgentMessage::Result {
                result: "continued".to_string(),
                details: Default::default(),
            }],
        ]));
        let provider = AcpAgentProvider::new(
            Box::new(RecordingTransport {
                queries: Arc::clone(&queries),
                responses,
            }),
            "claude-code".to_string(),
            Some("sonnet".to_string()),
            false,
        )
        .with_store_path_for_tests(store_path);

        let turn_one = vec![
            Message::new(Role::System, "ctx"),
            Message::new(Role::User, "first"),
        ];
        let _ = provider.generate(&turn_one).await.unwrap();

        let turn_two = vec![
            Message::new(Role::System, "ctx"),
            Message::new(Role::User, "first"),
            Message::new(Role::Assistant, "done"),
            Message::new(Role::User, "second"),
        ];
        let _ = provider.generate(&turn_two).await.unwrap();

        let queries = queries.lock().unwrap();
        assert_eq!(queries.len(), 2);
        assert_eq!(queries[0].session_id, None);
        assert_eq!(queries[1].session_id.as_deref(), Some("sess-1"));
        assert!(queries[1].resume);
    }
}
