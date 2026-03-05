use std::pin::Pin;

use ava_context::ContextManager;
use ava_tools::registry::ToolRegistry;
use ava_types::{AvaError, Message, Result, Role, Session, ToolCall, ToolResult};
use futures::Stream;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use uuid::Uuid;

use crate::llm_trait::LLMProvider;

pub struct AgentLoop {
    pub llm: Box<dyn LLMProvider>,
    pub tools: ToolRegistry,
    pub context: ContextManager,
    pub config: AgentConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentConfig {
    pub max_turns: usize,
    pub token_limit: usize,
    pub model: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum AgentEvent {
    Token(String),
    ToolCall(ToolCall),
    ToolResult(ToolResult),
    Progress(String),
    Complete(Session),
    Error(String),
}

impl AgentLoop {
    pub fn new(
        llm: Box<dyn LLMProvider>,
        tools: ToolRegistry,
        context: ContextManager,
        config: AgentConfig,
    ) -> Self {
        Self {
            llm,
            tools,
            context,
            config,
        }
    }

    pub async fn run(&mut self, goal: &str) -> Result<Session> {
        let mut session = Session::new();

        let goal_message = Message::new(Role::User, goal.to_string());
        self.context.add_message(goal_message.clone());
        session.add_message(goal_message);

        for _ in 0..self.config.max_turns {
            let response = self.llm.generate(self.context.get_messages()).await?;

            let assistant_message = Message::new(Role::Assistant, response.clone());
            self.context.add_message(assistant_message.clone());
            session.add_message(assistant_message);

            let tool_calls = parse_tool_calls(&response)?;
            let completion_requested = tool_calls.iter().any(|call| call.name == "attempt_completion");

            self.execute_tool_calls(tool_calls, &mut session).await;

            if self.context.should_compact() {
                self.context.compact()?;
            }

            if completion_requested {
                return Ok(session);
            }
        }

        Ok(session)
    }

    pub async fn run_streaming(
        &mut self,
        goal: &str,
    ) -> Pin<Box<dyn Stream<Item = AgentEvent> + Send + '_>> {
        let goal = goal.to_string();
        Box::pin(async_stream::stream! {
            let mut session = Session::new();

            let goal_message = Message::new(Role::User, goal.clone());
            self.context.add_message(goal_message.clone());
            session.add_message(goal_message);

            for turn in 0..self.config.max_turns {
                yield AgentEvent::Progress(format!("turn {}", turn + 1));

                let response = match self.llm.generate(self.context.get_messages()).await {
                    Ok(response) => response,
                    Err(error) => {
                        yield AgentEvent::Error(error.to_string());
                        return;
                    }
                };
                yield AgentEvent::Token(response.clone());

                let assistant_message = Message::new(Role::Assistant, response.clone());
                self.context.add_message(assistant_message.clone());
                session.add_message(assistant_message);

                let tool_calls = match parse_tool_calls(&response) {
                    Ok(calls) => calls,
                    Err(error) => {
                        yield AgentEvent::Error(error.to_string());
                        return;
                    }
                };
                let completion_requested = tool_calls.iter().any(|call| call.name == "attempt_completion");

                for tool_call in tool_calls {
                    if tool_call.name == "attempt_completion" {
                        continue;
                    }

                    yield AgentEvent::ToolCall(tool_call.clone());

                    let result = self.execute_tool_call(tool_call).await;
                    self.context.add_tool_result(result.clone());
                    yield AgentEvent::ToolResult(result.clone());
                    session.add_message(
                        Message::new(Role::Tool, result.content.clone()).with_tool_results(vec![result]),
                    );
                }

                if self.context.should_compact() {
                    if let Err(error) = self.context.compact() {
                        yield AgentEvent::Error(error.to_string());
                        return;
                    }
                    yield AgentEvent::Progress("context compacted".to_string());
                }

                if completion_requested {
                    yield AgentEvent::Complete(session.clone());
                    return;
                }
            }

            yield AgentEvent::Progress("max turns reached".to_string());
            yield AgentEvent::Complete(session);
        })
    }

    async fn execute_tool_calls(&mut self, tool_calls: Vec<ToolCall>, session: &mut Session) {
        for tool_call in tool_calls {
            if tool_call.name == "attempt_completion" {
                continue;
            }

            let result = self.execute_tool_call(tool_call).await;
            self.context.add_tool_result(result.clone());
            session.add_message(
                Message::new(Role::Tool, result.content.clone()).with_tool_results(vec![result]),
            );
        }
    }

    async fn execute_tool_call(&self, tool_call: ToolCall) -> ToolResult {
        match self.tools.execute(tool_call.clone()).await {
            Ok(result) => result,
            Err(error) => ToolResult {
                call_id: tool_call.id,
                content: error.to_string(),
                is_error: true,
            },
        }
    }
}

#[derive(Debug, Deserialize)]
struct ToolCallEnvelope {
    name: String,
    #[serde(default)]
    arguments: Value,
    #[serde(default)]
    id: Option<String>,
}

fn parse_tool_calls(content: &str) -> Result<Vec<ToolCall>> {
    // TODO: Extend parser to support provider-native tool call payloads.
    let value = match serde_json::from_str::<Value>(content) {
        Ok(value) => value,
        Err(_) => return Ok(Vec::new()),
    };

    let calls = if let Some(raw_calls) = value.get("tool_calls") {
        serde_json::from_value::<Vec<ToolCallEnvelope>>(raw_calls.clone())
            .map_err(|error| AvaError::SerializationError(error.to_string()))?
    } else if let Some(raw_call) = value.get("tool_call") {
        vec![serde_json::from_value::<ToolCallEnvelope>(raw_call.clone())
            .map_err(|error| AvaError::SerializationError(error.to_string()))?]
    } else {
        Vec::new()
    };

    Ok(calls
        .into_iter()
        .map(|call| ToolCall {
            id: call.id.unwrap_or_else(|| Uuid::new_v4().to_string()),
            name: call.name,
            arguments: call.arguments,
        })
        .collect())
}
