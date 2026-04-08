use serde::Serialize;
use serde_json::Value;
use tauri::{Emitter, Window};

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TodoItemPayload {
    pub content: String,
    pub status: String,
    pub priority: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CompactMessagePayload {
    pub role: String,
    pub content: String,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ContextCompactedPayload {
    pub auto: bool,
    pub tokens_before: usize,
    pub tokens_after: usize,
    pub tokens_saved: usize,
    pub messages_before: usize,
    pub messages_after: usize,
    pub usage_before_percent: f64,
    pub summary: String,
    pub context_summary: String,
    pub active_messages: Vec<CompactMessagePayload>,
}

#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum AgentEvent {
    #[serde(rename = "token")]
    Token { content: String },
    #[serde(rename = "thinking")]
    Thinking { content: String },
    #[serde(rename = "tool_call")]
    ToolCall {
        id: String,
        name: String,
        args: Value,
    },
    #[serde(rename = "tool_result")]
    ToolResult {
        call_id: String,
        content: String,
        is_error: bool,
    },
    #[serde(rename = "progress")]
    Progress { message: String },
    #[serde(rename = "complete")]
    Complete { session: Value },
    #[serde(rename = "error")]
    Error { message: String },
    #[serde(rename = "todo_update")]
    TodoUpdate { todos: Vec<TodoItemPayload> },
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
    #[serde(rename = "context_compacted")]
    ContextCompacted(ContextCompactedPayload),
    #[serde(rename = "approval_request")]
    ApprovalRequest {
        id: String,
        tool_call_id: String,
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

    #[serde(rename = "plan_created")]
    PlanCreated { plan: PlanPayload },
}

/// Flattened plan data for the desktop frontend.
/// We avoid sending the full plan runtime type (which includes budget/domain
/// enums, etc.) and instead project into a frontend-friendly shape.
#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanPayload {
    pub summary: String,
    pub steps: Vec<PlanStepPayload>,
    pub estimated_turns: usize,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanStepPayload {
    pub id: String,
    pub description: String,
    pub files: Vec<String>,
    pub action: String,
    pub depends_on: Vec<String>,
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

    pub fn emit_tool_call(&self, id: &str, name: &str, args: Value) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolCall {
                    id: id.to_string(),
                    name: name.to_string(),
                    args,
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_tool_result(
        &self,
        call_id: &str,
        content: &str,
        is_error: bool,
    ) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolResult {
                    call_id: call_id.to_string(),
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
pub fn from_backend_event(event: &ava_agent::agent_loop::AgentEvent) -> Option<AgentEvent> {
    use ava_agent::agent_loop::AgentEvent as BE;
    match event {
        BE::Token(content) => Some(AgentEvent::Token {
            content: content.clone(),
        }),
        BE::Thinking(content) => Some(AgentEvent::Thinking {
            content: content.clone(),
        }),
        BE::ToolCall(tc) => Some(AgentEvent::ToolCall {
            id: tc.id.clone(),
            name: tc.name.clone(),
            args: tc.arguments.clone(),
        }),
        BE::ToolResult(tr) => Some(AgentEvent::ToolResult {
            call_id: tr.call_id.clone(),
            content: tr.content.clone(),
            is_error: tr.is_error,
        }),
        BE::Progress(msg) => Some(AgentEvent::Progress {
            message: msg.clone(),
        }),
        BE::Complete(session) => {
            let session_json = match serde_json::to_value(session) {
                Ok(v) => v,
                Err(e) => {
                    tracing::error!(
                        "Failed to serialize session for Complete event: {e}. \
                         Sending error indicator instead of silently dropping data."
                    );
                    serde_json::json!({ "__serialization_error": e.to_string() })
                }
            };
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
        BE::ContextCompacted {
            auto,
            tokens_before,
            tokens_after,
            tokens_saved,
            messages_before,
            messages_after,
            usage_before_percent,
            summary,
            context_summary,
            active_messages,
        } => Some(AgentEvent::ContextCompacted(ContextCompactedPayload {
            auto: *auto,
            tokens_before: *tokens_before,
            tokens_after: *tokens_after,
            tokens_saved: *tokens_saved,
            messages_before: *messages_before,
            messages_after: *messages_after,
            usage_before_percent: *usage_before_percent,
            summary: summary.clone(),
            context_summary: context_summary.clone(),
            active_messages: active_messages
                .iter()
                .map(|message| CompactMessagePayload {
                    role: message.role.clone(),
                    content: message.content.clone(),
                })
                .collect(),
        })),
        // ToolStats, SubAgentComplete, SnapshotTaken etc. don't have a direct frontend
        // representation yet. Log at debug level so we can diagnose if important events
        // are being silently dropped.
        other => {
            tracing::debug!(
                "from_backend_event: no frontend mapping for event type {:?} — dropping",
                std::mem::discriminant(other)
            );
            None
        }
    }
}

/// Emit a backend `AgentEvent` to all Tauri windows via the app handle.
pub fn emit_backend_event<R: tauri::Runtime>(
    handle: &tauri::AppHandle<R>,
    event: &ava_agent::agent_loop::AgentEvent,
) {
    if let Some(payload) = from_backend_event(event) {
        if let Err(e) = handle.emit("agent-event", payload) {
            tracing::error!("Failed to emit backend agent-event to frontend: {e}");
        }
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
            tool_call_id: "call-1".to_string(),
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
    fn serializes_plan_created_event() {
        let event = AgentEvent::PlanCreated {
            plan: super::PlanPayload {
                summary: "Build auth system".to_string(),
                steps: vec![
                    super::PlanStepPayload {
                        id: "t1".to_string(),
                        description: "Research OAuth patterns".to_string(),
                        files: vec!["docs/auth.md".to_string()],
                        action: "research".to_string(),
                        depends_on: vec![],
                    },
                    super::PlanStepPayload {
                        id: "t2".to_string(),
                        description: "Implement login".to_string(),
                        files: vec!["src/auth.rs".to_string()],
                        action: "implement".to_string(),
                        depends_on: vec!["t1".to_string()],
                    },
                ],
                estimated_turns: 15,
            },
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "plan_created");
        assert_eq!(as_json["plan"]["summary"], "Build auth system");
        assert_eq!(as_json["plan"]["steps"].as_array().unwrap().len(), 2);
        assert_eq!(as_json["plan"]["estimatedTurns"], 15);
        assert_eq!(as_json["plan"]["steps"][0]["action"], "research");
        assert_eq!(as_json["plan"]["steps"][1]["dependsOn"][0], "t1");
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
