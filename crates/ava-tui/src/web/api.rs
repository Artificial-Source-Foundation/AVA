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
    ingest_frontend_log, list_mcp_servers, list_models, list_plugins, list_providers, reload_mcp,
    set_permission_level, switch_model, toggle_permission_level,
};
pub(crate) use super::api_interactive::{
    resolve_approval, resolve_plan, resolve_question, undo_last_edit,
};
pub(crate) use super::api_sessions::{
    add_message, create_session, delete_session, delete_session_body, get_session,
    get_session_messages, list_session_agents, list_session_checkpoints, list_session_files,
    list_session_memory, list_session_terminal, list_sessions, load_session_body, rename_session,
    rename_session_body, search_sessions, update_message,
};

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
    #[serde(rename = "plan_created")]
    PlanCreated { plan: PlanPayload },
    #[serde(rename = "todo_update")]
    TodoUpdate { todos: Vec<TodoItemFrontend> },
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
        WebEvent::Agent(backend_event) => convert_agent_event(backend_event),
        WebEvent::ApprovalRequest {
            id,
            tool_name,
            args,
            risk_level,
            reason,
            warnings,
        } => Some(WebAgentEvent::ApprovalRequest {
            id: id.clone(),
            tool_name: tool_name.clone(),
            args: args.clone(),
            risk_level: risk_level.clone(),
            reason: reason.clone(),
            warnings: warnings.clone(),
        }),
        WebEvent::QuestionRequest {
            id,
            question,
            options,
        } => Some(WebAgentEvent::QuestionRequest {
            id: id.clone(),
            question: question.clone(),
            options: options.clone(),
        }),
        WebEvent::PlanCreated {
            summary,
            steps,
            estimated_turns,
        } => Some(WebAgentEvent::PlanCreated {
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
        }),
        WebEvent::TodoUpdate { todos } => Some(WebAgentEvent::TodoUpdate {
            todos: todos
                .iter()
                .map(|t| TodoItemFrontend {
                    content: t.content.clone(),
                    status: t.status.clone(),
                    priority: t.priority.clone(),
                })
                .collect(),
        }),
    }
}

/// Convert a backend `AgentEvent` to a frontend-compatible `WebAgentEvent`.
/// Returns `None` for events that have no direct frontend representation.
pub fn convert_agent_event(event: &ava_agent::agent_loop::AgentEvent) -> Option<WebAgentEvent> {
    use ava_agent::agent_loop::AgentEvent as BE;
    match event {
        BE::Token(content) => Some(WebAgentEvent::Token {
            content: content.clone(),
        }),
        BE::Thinking(content) => Some(WebAgentEvent::Thinking {
            content: content.clone(),
        }),
        BE::ToolCall(tc) => Some(WebAgentEvent::ToolCall {
            name: tc.name.clone(),
            args: tc.arguments.clone(),
        }),
        BE::ToolResult(tr) => Some(WebAgentEvent::ToolResult {
            content: tr.content.clone(),
            is_error: tr.is_error,
        }),
        BE::Progress(msg) => Some(WebAgentEvent::Progress {
            message: msg.clone(),
        }),
        BE::Complete(session) => {
            let session_json = serde_json::to_value(session).unwrap_or_default();
            Some(WebAgentEvent::Complete {
                session: session_json,
            })
        }
        BE::Error(msg) => Some(WebAgentEvent::Error {
            message: msg.clone(),
        }),
        BE::TokenUsage {
            input_tokens,
            output_tokens,
            cost_usd,
        } => Some(WebAgentEvent::TokenUsage {
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
        }),
        BE::BudgetWarning {
            threshold_percent,
            current_cost_usd,
            max_budget_usd,
        } => Some(WebAgentEvent::BudgetWarning {
            threshold_percent: *threshold_percent,
            current_cost_usd: *current_cost_usd,
            max_budget_usd: *max_budget_usd,
        }),
        // ToolStats, DiffPreview, SubAgentComplete have no direct frontend representation.
        _ => None,
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
