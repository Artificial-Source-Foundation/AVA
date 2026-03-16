use serde::Serialize;
use serde_json::Value;
use tauri::{Emitter, Window};

#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum AgentEvent {
    #[serde(rename = "token")]
    Token { content: String },
    #[serde(rename = "thinking")]
    Thinking { content: String },
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
    #[serde(rename = "token_usage")]
    TokenUsage {
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
    },
    #[serde(rename = "budget_warning")]
    BudgetWarning {
        threshold_percent: u8,
        current_cost_usd: f64,
        max_budget_usd: f64,
    },
    #[serde(rename = "approval_request")]
    ApprovalRequest {
        id: String,
        tool_name: String,
        args: Value,
        risk_level: String,
        reason: String,
        warnings: Vec<String>,
    },
    #[serde(rename = "question_request")]
    QuestionRequest {
        id: String,
        question: String,
        options: Vec<String>,
    },
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

/// Convert an `ava_agent::AgentEvent` to a Tauri `AgentEvent` payload.
/// Returns `None` for events that have no direct desktop representation
/// (e.g. `ToolStats`, `SubAgentComplete`).
pub fn from_backend_event(
    event: &ava_agent::agent_loop::AgentEvent,
) -> Option<AgentEvent> {
    use ava_agent::agent_loop::AgentEvent as BE;
    match event {
        BE::Token(content) => Some(AgentEvent::Token {
            content: content.clone(),
        }),
        BE::Thinking(content) => Some(AgentEvent::Thinking {
            content: content.clone(),
        }),
        BE::ToolCall(tc) => Some(AgentEvent::ToolCall {
            name: tc.name.clone(),
            args: tc.arguments.clone(),
        }),
        BE::ToolResult(tr) => Some(AgentEvent::ToolResult {
            content: tr.content.clone(),
            is_error: tr.is_error,
        }),
        BE::Progress(msg) => Some(AgentEvent::Progress {
            message: msg.clone(),
        }),
        BE::Complete(session) => {
            let session_json = serde_json::to_value(session).unwrap_or_default();
            Some(AgentEvent::Complete {
                session: session_json,
            })
        }
        BE::Error(msg) => Some(AgentEvent::Error {
            message: msg.clone(),
        }),
        BE::TokenUsage {
            input_tokens,
            output_tokens,
            cost_usd,
        } => Some(AgentEvent::TokenUsage {
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
        }),
        BE::BudgetWarning {
            threshold_percent,
            current_cost_usd,
            max_budget_usd,
        } => Some(AgentEvent::BudgetWarning {
            threshold_percent: *threshold_percent,
            current_cost_usd: *current_cost_usd,
            max_budget_usd: *max_budget_usd,
        }),
        // ToolStats and SubAgentComplete don't have a direct frontend representation yet.
        _ => None,
    }
}

/// Emit a backend `AgentEvent` to all Tauri windows via the app handle.
pub fn emit_backend_event<R: tauri::Runtime>(
    handle: &tauri::AppHandle<R>,
    event: &ava_agent::agent_loop::AgentEvent,
) {
    if let Some(payload) = from_backend_event(event) {
        let _ = handle.emit("agent-event", payload);
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

    #[test]
    fn serializes_approval_request_event() {
        let event = AgentEvent::ApprovalRequest {
            id: "req-1".to_string(),
            tool_name: "bash".to_string(),
            args: json!({"command": "rm -rf /tmp/test"}),
            risk_level: "high".to_string(),
            reason: "destructive command".to_string(),
            warnings: vec!["uses rm -rf".to_string()],
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "approval_request");
        assert_eq!(as_json["tool_name"], "bash");
        assert_eq!(as_json["risk_level"], "high");
    }

    #[test]
    fn serializes_question_request_event() {
        let event = AgentEvent::QuestionRequest {
            id: "q-1".to_string(),
            question: "Which framework?".to_string(),
            options: vec!["React".to_string(), "SolidJS".to_string()],
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "question_request");
        assert_eq!(as_json["question"], "Which framework?");
    }
}
