use ava_types::{
    DelegationRecord, ExternalSessionLink, Message, Role, Session, StructuredContentBlock,
    ToolCall, ToolResult,
};

use crate::protocol::{AgentMessage, AgentResultDetails, ContentBlock};

#[derive(Debug, Clone, Default)]
pub struct ExternalRunDescriptor {
    pub provider: Option<String>,
    pub agent_name: Option<String>,
    pub model: Option<String>,
    pub cwd: Option<String>,
    pub resume_attempted: bool,
}

pub struct ExternalSessionMapper {
    descriptor: ExternalRunDescriptor,
    pub session: Session,
    saw_output: bool,
}

impl ExternalSessionMapper {
    pub fn new(descriptor: ExternalRunDescriptor) -> Self {
        let mut session = Session::new();
        session.metadata["externalLink"] = serde_json::to_value(ExternalSessionLink {
            provider: descriptor.provider.clone(),
            agent_name: descriptor.agent_name.clone(),
            external_session_id: None,
            resume_attempted: descriptor.resume_attempted,
            resumed: false,
            model: descriptor.model.clone(),
            cwd: descriptor.cwd.clone(),
        })
        .unwrap_or_default();

        Self {
            descriptor,
            session,
            saw_output: false,
        }
    }

    pub fn apply(&mut self, msg: AgentMessage) -> ava_types::Result<()> {
        match msg {
            AgentMessage::System {
                message,
                session_id,
            } => {
                self.record_session_id(session_id);
                if !message.trim().is_empty() {
                    let mut system_message = Message::new(Role::System, message);
                    system_message.user_visible = false;
                    self.session.add_message(system_message);
                }
            }
            AgentMessage::Assistant {
                content,
                session_id,
            } => {
                self.record_session_id(session_id);
                self.append_assistant_blocks(content);
            }
            AgentMessage::Result { result, details } => {
                self.apply_result_details(&details);
                if !result.trim().is_empty() {
                    self.saw_output = true;
                    self.session
                        .add_message(Message::new(Role::Assistant, result));
                }
            }
            AgentMessage::Error { message, .. } => {
                self.session.metadata["externalError"] = serde_json::json!(message.clone());
                return Err(ava_types::AvaError::ToolError(message));
            }
            AgentMessage::Unknown => {}
        }

        Ok(())
    }

    pub fn into_session(mut self) -> Session {
        if let Some(link) = self.external_link() {
            self.session.metadata["externalSessionId"] =
                serde_json::json!(link.external_session_id);
            if let Some(agent_name) = &link.agent_name {
                self.session.metadata["externalAgent"] = serde_json::json!(agent_name);
            }
        }
        self.session
    }

    fn append_assistant_blocks(&mut self, content: Vec<ContentBlock>) {
        let mut text_parts = Vec::new();
        let mut thinking_parts = Vec::new();
        let mut tool_calls = Vec::new();
        let mut tool_results = Vec::new();
        let mut structured = Vec::new();

        for block in content {
            match block {
                ContentBlock::Text { text } => {
                    if !text.trim().is_empty() {
                        self.saw_output = true;
                        text_parts.push(text.clone());
                    }
                    structured.push(StructuredContentBlock::Text { text });
                }
                ContentBlock::Thinking { thinking } => {
                    if !thinking.trim().is_empty() {
                        thinking_parts.push(thinking.clone());
                    }
                    structured.push(StructuredContentBlock::Thinking { thinking });
                }
                ContentBlock::ToolUse { id, name, input } => {
                    tool_calls.push(ToolCall {
                        id: id.clone(),
                        name: name.clone(),
                        arguments: input.clone(),
                    });
                    structured.push(StructuredContentBlock::ToolUse { id, name, input });
                }
                ContentBlock::ToolResult {
                    tool_use_id,
                    content,
                    is_error,
                } => {
                    tool_results.push(ToolResult {
                        call_id: tool_use_id.clone(),
                        content: content.clone(),
                        is_error,
                    });
                    structured.push(StructuredContentBlock::ToolResult {
                        tool_use_id,
                        content,
                        is_error,
                    });
                }
            }
        }

        if !thinking_parts.is_empty() {
            let mut thinking_message = Message::new(
                Role::Assistant,
                format!("[thinking]\n{}", thinking_parts.join("\n\n")),
            )
            .with_structured_content(
                thinking_parts
                    .into_iter()
                    .map(|thinking| StructuredContentBlock::Thinking { thinking })
                    .collect(),
            );
            thinking_message.user_visible = false;
            self.session.add_message(thinking_message);
        }

        if !text_parts.is_empty()
            || !tool_calls.is_empty()
            || !tool_results.is_empty()
            || !structured.is_empty()
        {
            let mut assistant_message = Message::new(Role::Assistant, text_parts.join("\n"));
            assistant_message.tool_calls = tool_calls;
            assistant_message.tool_results = tool_results;
            assistant_message.structured_content = structured;
            self.session.add_message(assistant_message);
        }
    }

    fn apply_result_details(&mut self, details: &AgentResultDetails) {
        self.record_session_id(details.session_id.clone());
        if let Some(cost_usd) = details.total_cost_usd {
            self.session.metadata["externalCostUsd"] = serde_json::json!(cost_usd);
        }
        if let Some(subtype) = &details.subtype {
            self.session.metadata["externalResultSubtype"] = serde_json::json!(subtype);
        }
        if let Some(usage) = &details.usage {
            self.session.token_usage.input_tokens += usage.input_tokens as usize;
            self.session.token_usage.output_tokens += usage.output_tokens as usize;
            self.session.token_usage.cache_read_tokens +=
                usage.cache_read_input_tokens.unwrap_or(0) as usize;
            self.session.token_usage.cache_creation_tokens +=
                usage.cache_creation_input_tokens.unwrap_or(0) as usize;
        }
    }

    fn record_session_id(&mut self, session_id: Option<String>) {
        if let Some(session_id) = session_id {
            let mut link = self.external_link().unwrap_or_else(|| ExternalSessionLink {
                provider: self.descriptor.provider.clone(),
                agent_name: self.descriptor.agent_name.clone(),
                external_session_id: None,
                resume_attempted: self.descriptor.resume_attempted,
                resumed: false,
                model: self.descriptor.model.clone(),
                cwd: self.descriptor.cwd.clone(),
            });
            link.resumed = self.descriptor.resume_attempted && self.saw_output;
            link.external_session_id = Some(session_id.clone());
            self.session.metadata["externalLink"] = serde_json::to_value(link).unwrap_or_default();
            self.session.metadata["externalSessionId"] = serde_json::json!(session_id);
        }
    }

    fn external_link(&self) -> Option<ExternalSessionLink> {
        self.session
            .metadata
            .get("externalLink")
            .and_then(|value| serde_json::from_value(value.clone()).ok())
    }
}

pub fn attach_delegation_record(session: &mut Session, record: &DelegationRecord) {
    session.metadata["delegation"] = serde_json::to_value(record).unwrap_or_default();
}
