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
    #[serde(rename = "run_id", skip_serializing_if = "Option::is_none")]
    pub run_id: Option<String>,
}

#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum AgentEvent {
    #[serde(rename = "token")]
    Token {
        content: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "thinking")]
    Thinking {
        content: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "tool_call")]
    ToolCall {
        id: String,
        name: String,
        args: Value,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "tool_result")]
    ToolResult {
        call_id: String,
        content: String,
        is_error: bool,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "progress")]
    Progress {
        message: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "complete")]
    Complete {
        session: Value,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "error")]
    Error {
        message: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "todo_update")]
    TodoUpdate {
        todos: Vec<TodoItemPayload>,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "token_usage")]
    TokenUsage {
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "budget_warning")]
    BudgetWarning {
        threshold_percent: u8,
        current_cost_usd: f64,
        max_budget_usd: f64,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
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
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "question_request")]
    QuestionRequest {
        id: String,
        question: String,
        options: Vec<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "interactive_request_cleared")]
    InteractiveRequestCleared {
        request_id: String,
        request_kind: String,
        timed_out: bool,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },

    #[serde(rename = "plan_created")]
    PlanCreated {
        id: String,
        plan: PlanPayload,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "plan_step_complete")]
    PlanStepComplete {
        step_id: String,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "subagent_complete")]
    SubagentComplete {
        call_id: String,
        session_id: String,
        description: String,
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
        agent_type: Option<String>,
        provider: Option<String>,
        resumed: bool,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "streaming_edit_progress")]
    StreamingEditProgress {
        call_id: String,
        tool_name: String,
        file_path: Option<String>,
        bytes_received: usize,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
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

    pub fn emit_token(&self, token: &str, run_id: Option<&str>) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Token {
                    content: token.to_string(),
                    run_id: run_id.map(str::to_string),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_tool_call(
        &self,
        id: &str,
        name: &str,
        args: Value,
        run_id: Option<&str>,
    ) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolCall {
                    id: id.to_string(),
                    name: name.to_string(),
                    args,
                    run_id: run_id.map(str::to_string),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_tool_result(
        &self,
        call_id: &str,
        content: &str,
        is_error: bool,
        run_id: Option<&str>,
    ) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolResult {
                    call_id: call_id.to_string(),
                    content: content.to_string(),
                    is_error,
                    run_id: run_id.map(str::to_string),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_progress(&self, message: &str, run_id: Option<&str>) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Progress {
                    message: message.to_string(),
                    run_id: run_id.map(str::to_string),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_complete(&self, session: Value, run_id: Option<&str>) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Complete {
                    session,
                    run_id: run_id.map(str::to_string),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_error(&self, message: &str, run_id: Option<&str>) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Error {
                    message: message.to_string(),
                    run_id: run_id.map(str::to_string),
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
    run_id: Option<&str>,
) -> Option<AgentEvent> {
    use ava_agent::agent_loop::AgentEvent as BE;
    match event {
        BE::Token(content) => Some(AgentEvent::Token {
            content: content.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::Thinking(content) => Some(AgentEvent::Thinking {
            content: content.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::ToolCall(tc) => Some(AgentEvent::ToolCall {
            id: tc.id.clone(),
            name: tc.name.clone(),
            args: tc.arguments.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::ToolResult(tr) => Some(AgentEvent::ToolResult {
            call_id: tr.call_id.clone(),
            content: tr.content.clone(),
            is_error: tr.is_error,
            run_id: run_id.map(str::to_string),
        }),
        BE::Progress(msg) => Some(AgentEvent::Progress {
            message: msg.clone(),
            run_id: run_id.map(str::to_string),
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
                run_id: run_id.map(str::to_string),
            })
        }
        BE::Error(msg) => Some(AgentEvent::Error {
            message: msg.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::TokenUsage {
            input_tokens,
            output_tokens,
            cost_usd,
        } => Some(AgentEvent::TokenUsage {
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
            run_id: run_id.map(str::to_string),
        }),
        BE::BudgetWarning {
            threshold_percent,
            current_cost_usd,
            max_budget_usd,
        } => Some(AgentEvent::BudgetWarning {
            threshold_percent: *threshold_percent,
            current_cost_usd: *current_cost_usd,
            max_budget_usd: *max_budget_usd,
            run_id: run_id.map(str::to_string),
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
            run_id: run_id.map(str::to_string),
        })),
        BE::PlanStepComplete { step_id } => Some(AgentEvent::PlanStepComplete {
            step_id: step_id.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::SubAgentComplete {
            call_id,
            session_id,
            description,
            input_tokens,
            output_tokens,
            cost_usd,
            agent_type,
            provider,
            resumed,
            ..
        } => Some(AgentEvent::SubagentComplete {
            call_id: call_id.clone(),
            session_id: session_id.clone(),
            description: description.clone(),
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
            agent_type: agent_type.clone(),
            provider: provider.clone(),
            resumed: *resumed,
            run_id: run_id.map(str::to_string),
        }),
        BE::StreamingEditProgress {
            call_id,
            tool_name,
            file_path,
            bytes_received,
        } => Some(AgentEvent::StreamingEditProgress {
            call_id: call_id.clone(),
            tool_name: tool_name.clone(),
            file_path: file_path.clone(),
            bytes_received: *bytes_received,
            run_id: run_id.map(str::to_string),
        }),
        // ToolStats, DiffPreview, SnapshotTaken etc. don't have a direct frontend
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
    run_id: Option<&str>,
) {
    if let Some(payload) = from_backend_event(event, run_id) {
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
            run_id: Some("desktop-run-1".to_string()),
        };
        let as_json = serde_json::to_value(token_event).expect("event to serialize");

        assert_eq!(as_json["type"], "token");
        assert_eq!(as_json["content"], "abc");
        assert_eq!(as_json["run_id"], "desktop-run-1");

        let complete_event = AgentEvent::Complete {
            session: json!({ "id": "mock-session" }),
            run_id: Some("desktop-run-1".to_string()),
        };
        let complete_json = serde_json::to_value(complete_event).expect("event to serialize");

        assert_eq!(complete_json["type"], "complete");
        assert_eq!(complete_json["session"]["id"], "mock-session");
        assert_eq!(complete_json["run_id"], "desktop-run-1");
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
            id: "plan-1".to_string(),
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
            run_id: Some("desktop-run-2".to_string()),
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "plan_created");
        assert_eq!(as_json["id"], "plan-1");
        assert_eq!(as_json["run_id"], "desktop-run-2");
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

    #[test]
    fn serializes_interactive_request_cleared_event() {
        let event = AgentEvent::InteractiveRequestCleared {
            request_id: "approval-1".to_string(),
            request_kind: "approval".to_string(),
            timed_out: true,
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "interactive_request_cleared");
        assert_eq!(as_json["request_id"], "approval-1");
        assert_eq!(as_json["request_kind"], "approval");
        assert_eq!(as_json["timed_out"], true);
    }

    #[test]
    fn serializes_streaming_edit_and_subagent_events() {
        let progress = AgentEvent::StreamingEditProgress {
            call_id: "call-1".to_string(),
            tool_name: "apply_patch".to_string(),
            file_path: Some("src/main.rs".to_string()),
            bytes_received: 512,
            run_id: Some("desktop-run-3".to_string()),
        };
        let progress_json = serde_json::to_value(progress).expect("event to serialize");
        assert_eq!(progress_json["type"], "streaming_edit_progress");
        assert_eq!(progress_json["call_id"], "call-1");
        assert_eq!(progress_json["file_path"], "src/main.rs");
        assert_eq!(progress_json["run_id"], "desktop-run-3");

        let subagent = AgentEvent::SubagentComplete {
            call_id: "call-2".to_string(),
            session_id: "child-session".to_string(),
            description: "Investigate parser bug".to_string(),
            input_tokens: 120,
            output_tokens: 80,
            cost_usd: 0.42,
            agent_type: Some("reviewer".to_string()),
            provider: Some("openai".to_string()),
            resumed: true,
            run_id: Some("desktop-run-4".to_string()),
        };
        let subagent_json = serde_json::to_value(subagent).expect("event to serialize");
        assert_eq!(subagent_json["type"], "subagent_complete");
        assert_eq!(subagent_json["call_id"], "call-2");
        assert_eq!(subagent_json["session_id"], "child-session");
        assert_eq!(subagent_json["run_id"], "desktop-run-4");
        assert_eq!(subagent_json["agent_type"], "reviewer");
    }
}
