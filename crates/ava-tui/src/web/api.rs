//! HTTP API handlers for the AVA web server.
//!
//! Each handler maps to a Tauri command equivalent, operating on the shared
//! `WebState` instead of `tauri::State<DesktopBridge>`.
//!
//! Handlers are split across domain-specific sub-modules:
//! - `api_agent`       — agent lifecycle (submit, cancel, status, retry, mid-stream)
//! - `api_sessions`    — session CRUD, messages
//! - `api_config`      — models, providers, MCP, plugins, permissions, logging
//! - `api_interactive` — approval/question/plan resolution, undo

use ava_agent::control_plane::events::backend_event_requires_interactive_projection;
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::Serialize;
use serde_json::Value;

use super::state::WebEvent;

// Re-export sub-module handlers so the router in mod.rs can reference them
// via `api::submit_goal`, `api::list_sessions`, etc.
pub(crate) use super::api_agent::{
    agent_status,
    cancel_agent,
    clear_message_queue,
    compact_context,
    // Retry / edit-resend / regenerate
    edit_and_resend,
    follow_up_agent,
    get_message_queue,
    post_complete_agent,
    regenerate_response,
    retry_last_message,
    steer_agent,
    submit_goal,
};
pub(crate) use super::api_config::{
    disable_mcp_server, enable_mcp_server, get_config, get_current_model, get_permission_level,
    ingest_frontend_log, list_cli_agents, list_mcp_servers, list_models, list_plugins,
    list_providers, reload_mcp, set_permission_level, switch_model, toggle_permission_level,
};
pub(crate) use super::api_interactive::{
    resolve_approval, resolve_plan, resolve_question, undo_last_edit,
};
pub(crate) use super::api_plugin_host::{
    get_plugin_route, invoke_plugin_command, list_plugin_mounts, post_plugin_route,
};
pub(crate) use super::api_sessions::{
    add_message, create_session, delete_session, delete_session_body, duplicate_session,
    get_session, get_session_messages, list_session_agents, list_session_checkpoints,
    list_session_files, list_session_memory, list_session_terminal, list_sessions,
    load_session_body, rename_session, rename_session_body, search_sessions, update_message,
};
pub(crate) use super::api_tools::list_agent_tools;

// ============================================================================
// Health
// ============================================================================

pub async fn health() -> impl IntoResponse {
    let cwd = std::env::current_dir()
        .ok()
        .and_then(|p| p.to_str().map(String::from))
        .unwrap_or_default();
    Json(serde_json::json!({ "status": "ok", "version": env!("CARGO_PKG_VERSION"), "cwd": cwd }))
}

// ============================================================================
// WebAgentEvent — frontend-compatible serialization
// ============================================================================

/// Agent events serialized in the format the SolidJS frontend expects.
///
/// The frontend expects `{ "type": "token", "content": "..." }` (tagged enum),
/// while the backend `ava_agent::AgentEvent` serializes as Rust default
/// `{ "Token": "hello" }`. This type mirrors `src-tauri/src/events.rs`.
#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum WebAgentEvent {
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
    #[serde(rename = "todo_update")]
    TodoUpdate {
        todos: Vec<TodoItemFrontend>,
        #[serde(skip_serializing_if = "Option::is_none")]
        run_id: Option<String>,
    },
    #[serde(rename = "plan_step_complete")]
    PlanStepComplete { step_id: String, run_id: String },
    #[serde(rename = "plugin_event")]
    PluginEvent {
        plugin: String,
        event: String,
        payload: Value,
    },
    #[serde(rename = "subagent_complete")]
    SubAgentComplete {
        call_id: String,
        session_id: String,
        description: String,
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
        agent_type: Option<String>,
        provider: Option<String>,
        resumed: bool,
        run_id: String,
    },
    #[serde(rename = "streaming_edit_progress")]
    StreamingEditProgress {
        call_id: String,
        tool_name: String,
        file_path: Option<String>,
        bytes_received: usize,
        run_id: String,
    },
}

/// A single todo item for the frontend.
#[derive(Clone, Serialize)]
pub struct TodoItemFrontend {
    pub content: String,
    pub status: String,
    pub priority: String,
}

/// Plan payload for the frontend (matches `PlanPayload` in `src-tauri/src/events.rs`).
#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanPayload {
    pub summary: String,
    pub steps: Vec<PlanStepFrontend>,
    pub estimated_turns: usize,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanStepFrontend {
    pub id: String,
    pub description: String,
    pub files: Vec<String>,
    pub action: String,
    pub depends_on: Vec<String>,
}

/// Convert a `WebEvent` to a frontend-compatible `WebAgentEvent`.
/// Returns `None` for events that have no direct frontend representation.
pub fn convert_web_event(event: &WebEvent) -> Option<WebAgentEvent> {
    match event {
        WebEvent::Agent { event, run_id } => convert_agent_event(event, run_id.as_deref()),
        WebEvent::Plugin {
            plugin,
            event,
            payload,
        } => Some(WebAgentEvent::PluginEvent {
            plugin: plugin.clone(),
            event: event.clone(),
            payload: payload.clone(),
        }),
        WebEvent::ApprovalRequest {
            id,
            tool_call_id,
            tool_name,
            args,
            risk_level,
            reason,
            warnings,
            run_id,
        } => Some(WebAgentEvent::ApprovalRequest {
            id: id.clone(),
            tool_call_id: tool_call_id.clone(),
            tool_name: tool_name.clone(),
            args: args.clone(),
            risk_level: risk_level.clone(),
            reason: reason.clone(),
            warnings: warnings.clone(),
            run_id: run_id.clone(),
        }),
        WebEvent::QuestionRequest {
            id,
            question,
            options,
            run_id,
        } => Some(WebAgentEvent::QuestionRequest {
            id: id.clone(),
            question: question.clone(),
            options: options.clone(),
            run_id: run_id.clone(),
        }),
        WebEvent::InteractiveRequestCleared {
            request_id,
            request_kind,
            timed_out,
            run_id,
        } => Some(WebAgentEvent::InteractiveRequestCleared {
            request_id: request_id.clone(),
            request_kind: request_kind.clone(),
            timed_out: *timed_out,
            run_id: run_id.clone(),
        }),
        WebEvent::PlanCreated {
            id,
            summary,
            steps,
            estimated_turns,
            run_id,
        } => Some(WebAgentEvent::PlanCreated {
            id: id.clone(),
            plan: PlanPayload {
                summary: summary.clone(),
                steps: steps
                    .iter()
                    .map(|s| PlanStepFrontend {
                        id: s.id.clone(),
                        description: s.description.clone(),
                        files: s.files.clone(),
                        action: s.action.clone(),
                        depends_on: s.depends_on.clone(),
                    })
                    .collect(),
                estimated_turns: *estimated_turns,
            },
            run_id: run_id.clone(),
        }),
        WebEvent::TodoUpdate { todos, run_id } => Some(WebAgentEvent::TodoUpdate {
            todos: todos
                .iter()
                .map(|t| TodoItemFrontend {
                    content: t.content.clone(),
                    status: t.status.clone(),
                    priority: t.priority.clone(),
                })
                .collect(),
            run_id: run_id.clone(),
        }),
        WebEvent::PlanStepComplete { step_id, run_id } => Some(WebAgentEvent::PlanStepComplete {
            step_id: step_id.clone(),
            run_id: run_id.clone()?,
        }),
    }
}

/// Convert a backend `AgentEvent` to a frontend-compatible `WebAgentEvent`.
/// Returns `None` for events that have no direct frontend representation.
pub fn convert_agent_event(
    event: &ava_agent::agent_loop::AgentEvent,
    run_id: Option<&str>,
) -> Option<WebAgentEvent> {
    use ava_agent::agent_loop::AgentEvent as BE;
    match event {
        BE::Token(content) => Some(WebAgentEvent::Token {
            content: content.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::Thinking(content) => Some(WebAgentEvent::Thinking {
            content: content.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::ToolCall(tc) => Some(WebAgentEvent::ToolCall {
            id: tc.id.clone(),
            name: tc.name.clone(),
            args: tc.arguments.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::ToolResult(tr) => Some(WebAgentEvent::ToolResult {
            call_id: tr.call_id.clone(),
            content: tr.content.clone(),
            is_error: tr.is_error,
            run_id: run_id.map(str::to_string),
        }),
        BE::Progress(msg) => Some(WebAgentEvent::Progress {
            message: msg.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::Complete(session) => {
            let session_json = serde_json::to_value(session).unwrap_or_default();
            Some(WebAgentEvent::Complete {
                session: session_json,
                run_id: run_id.map(str::to_string),
            })
        }
        BE::Error(msg) => Some(WebAgentEvent::Error {
            message: msg.clone(),
            run_id: run_id.map(str::to_string),
        }),
        BE::TokenUsage {
            input_tokens,
            output_tokens,
            cost_usd,
        } => Some(WebAgentEvent::TokenUsage {
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
            run_id: run_id.map(str::to_string),
        }),
        BE::BudgetWarning {
            threshold_percent,
            current_cost_usd,
            max_budget_usd,
        } => Some(WebAgentEvent::BudgetWarning {
            threshold_percent: *threshold_percent,
            current_cost_usd: *current_cost_usd,
            max_budget_usd: *max_budget_usd,
            run_id: run_id.map(str::to_string),
        }),
        BE::PlanStepComplete { step_id } => Some(WebAgentEvent::PlanStepComplete {
            step_id: step_id.clone(),
            run_id: run_id?.to_string(),
        }),
        BE::StreamingEditProgress {
            call_id,
            tool_name,
            file_path,
            bytes_received,
            ..
        } => Some(WebAgentEvent::StreamingEditProgress {
            call_id: call_id.clone(),
            tool_name: tool_name.clone(),
            file_path: file_path.clone(),
            bytes_received: *bytes_received,
            run_id: run_id?.to_string(),
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
        } => Some(WebAgentEvent::SubAgentComplete {
            call_id: call_id.clone(),
            session_id: session_id.clone(),
            description: description.clone(),
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
            agent_type: agent_type.clone(),
            provider: provider.clone(),
            resumed: *resumed,
            run_id: run_id?.to_string(),
        }),
        // ToolStats and DiffPreview have no direct frontend representation.
        other => {
            let discriminant = std::mem::discriminant(other);
            if backend_event_requires_interactive_projection(other) {
                tracing::warn!(
                    "convert_agent_event: required control-plane event {:?} has no web projection",
                    discriminant
                );
            } else {
                tracing::debug!(
                    "convert_agent_event: no frontend mapping for event type {:?} — dropping",
                    discriminant
                );
            }
            None
        }
    }
}

// ============================================================================
// Error helpers
// ============================================================================

#[derive(Serialize)]
pub struct ErrorResponse {
    pub error: String,
}

pub(crate) fn error_response(
    status: StatusCode,
    message: &str,
) -> (StatusCode, Json<ErrorResponse>) {
    (
        status,
        Json(ErrorResponse {
            error: message.to_string(),
        }),
    )
}

#[cfg(test)]
mod tests {
    use crate::web::state::WebEvent;
    use ava_agent::agent_loop::AgentEvent as BackendEvent;
    use ava_agent::canonical_event_spec;
    use ava_types::Session;
    use ava_types::{ToolCall, ToolResult};
    use serde_json::json;

    use super::{convert_agent_event, convert_web_event, WebAgentEvent};

    fn assert_required_fields(event: WebAgentEvent) {
        let json = serde_json::to_value(event).expect("web event should serialize");
        let type_tag = json["type"].as_str().expect("tagged event type");
        let spec = canonical_event_spec(type_tag).expect("canonical event spec");
        for field in spec.required_fields {
            assert!(
                json.get(field.json_key()).is_some(),
                "missing required field {} for {type_tag}",
                field.json_key()
            );
        }
    }

    #[test]
    fn projected_backend_events_preserve_required_correlation_fields() {
        let run_id = Some("web-run-42");

        let tool_call = convert_agent_event(
            &BackendEvent::ToolCall(ToolCall {
                id: "call-1".to_string(),
                name: "bash".to_string(),
                arguments: json!({ "command": "pwd" }),
            }),
            run_id,
        )
        .expect("tool_call projection");
        let tool_call_json = serde_json::to_value(tool_call).expect("tool_call should serialize");
        assert_eq!(tool_call_json["id"], "call-1");
        assert_eq!(tool_call_json["run_id"], "web-run-42");

        let tool_result = convert_agent_event(
            &BackendEvent::ToolResult(ToolResult {
                call_id: "call-1".to_string(),
                content: "ok".to_string(),
                is_error: false,
            }),
            run_id,
        )
        .expect("tool_result projection");
        let tool_result_json =
            serde_json::to_value(tool_result).expect("tool_result should serialize");
        assert_eq!(tool_result_json["call_id"], "call-1");
        assert_eq!(tool_result_json["run_id"], "web-run-42");

        assert_required_fields(
            convert_agent_event(&BackendEvent::Complete(Session::new()), run_id)
                .expect("complete projection"),
        );

        assert_required_fields(
            convert_agent_event(
                &BackendEvent::StreamingEditProgress {
                    call_id: "call-2".to_string(),
                    tool_name: "apply_patch".to_string(),
                    file_path: Some("src/main.rs".to_string()),
                    bytes_received: 128,
                },
                run_id,
            )
            .expect("streaming_edit_progress projection"),
        );

        assert_required_fields(
            convert_agent_event(
                &BackendEvent::SubAgentComplete {
                    call_id: "call-3".to_string(),
                    session_id: "child-session".to_string(),
                    messages: vec![],
                    description: "Investigate bug".to_string(),
                    input_tokens: 10,
                    output_tokens: 20,
                    cost_usd: 0.1,
                    agent_type: Some("reviewer".to_string()),
                    provider: Some("openai".to_string()),
                    resumed: false,
                },
                run_id,
            )
            .expect("subagent projection"),
        );
    }

    #[test]
    fn token_projection_keeps_run_correlation() {
        let event = convert_agent_event(&BackendEvent::Token("hi".to_string()), Some("web-run-7"))
            .expect("token projection");
        let json = serde_json::to_value(event).expect("token should serialize");
        assert_eq!(json["type"], "token");
        assert_eq!(json["run_id"], "web-run-7");
    }

    #[test]
    fn forwarded_interactive_events_preserve_required_correlation_fields() {
        assert_required_fields(
            convert_web_event(&WebEvent::ApprovalRequest {
                id: "approval-1".to_string(),
                tool_call_id: "call-1".to_string(),
                tool_name: "bash".to_string(),
                args: json!({ "command": "pwd" }),
                risk_level: "medium".to_string(),
                reason: "reads working directory".to_string(),
                warnings: vec!["shell access".to_string()],
                run_id: Some("web-run-99".to_string()),
            })
            .expect("approval_request projection"),
        );

        assert_required_fields(
            convert_web_event(&WebEvent::InteractiveRequestCleared {
                request_id: "approval-1".to_string(),
                request_kind: "approval".to_string(),
                timed_out: false,
                run_id: Some("web-run-99".to_string()),
            })
            .expect("interactive_request_cleared projection"),
        );
    }
}
