use serde::Serialize;
use serde_json::Value;
use tauri::{Emitter, Window};

#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum AgentEvent {
    #[serde(rename = "token")]
    Token { content: String },
    #[serde(rename = "tool_call")]
    ToolCall { name: String, args: Value },
    #[serde(rename = "tool_result")]
    ToolResult { content: String, is_error: bool },
    #[serde(rename = "progress")]
    Progress { message: String },
    #[serde(rename = "complete")]
    Complete { session: Value },
    #[serde(rename = "error")]
    Error { message: String },
}

pub struct EventEmitter {
    window: Window,
}

impl EventEmitter {
    pub fn new(window: Window) -> Self {
        Self { window }
    }

    pub fn emit_token(&self, token: &str) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Token {
                    content: token.to_string(),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_tool_call(&self, name: &str, args: Value) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolCall {
                    name: name.to_string(),
                    args,
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_tool_result(&self, content: &str, is_error: bool) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolResult {
                    content: content.to_string(),
                    is_error,
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_progress(&self, message: &str) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Progress {
                    message: message.to_string(),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_complete(&self, session: Value) -> Result<(), String> {
        self.window
            .emit("agent-event", AgentEvent::Complete { session })
            .map_err(|e| e.to_string())
    }

    pub fn emit_error(&self, message: &str) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Error {
                    message: message.to_string(),
                },
            )
            .map_err(|e| e.to_string())
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::AgentEvent;

    #[test]
    fn serializes_with_expected_event_type_tag() {
        let token_event = AgentEvent::Token {
            content: "abc".to_string(),
        };
        let as_json = serde_json::to_value(token_event).expect("event to serialize");

        assert_eq!(as_json["type"], "token");
        assert_eq!(as_json["content"], "abc");

        let complete_event = AgentEvent::Complete {
            session: json!({ "id": "mock-session" }),
        };
        let complete_json = serde_json::to_value(complete_event).expect("event to serialize");

        assert_eq!(complete_json["type"], "complete");
        assert_eq!(complete_json["session"]["id"], "mock-session");
    }
}
