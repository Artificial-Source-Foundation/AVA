//! Interactive agent resolution endpoints: approval, question, plan, and undo.

use ava_agent::control_plane::interactive::ResolveInteractiveRequestError;
use axum::extract::State;
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use tracing::info;

use super::api::{error_response, ErrorResponse};
use super::state::WebState;

fn emit_interactive_request_cleared(
    state: &WebState,
    request_id: &str,
    request_kind: &str,
    timed_out: bool,
    run_id: Option<&str>,
) {
    let _ = state
        .inner
        .event_tx
        .send(super::state::WebEvent::InteractiveRequestCleared {
            request_id: request_id.to_string(),
            request_kind: request_kind.to_string(),
            timed_out,
            run_id: run_id.map(str::to_string),
        });
}

fn missing_request_id_error(kind: &str) -> (StatusCode, Json<ErrorResponse>) {
    error_response(
        StatusCode::BAD_REQUEST,
        &format!("request_id is required to resolve pending {kind} request"),
    )
}

// ============================================================================
// Approval / Question / Plan resolution
// ============================================================================

#[derive(Deserialize)]
pub struct ResolveApprovalRequest {
    pub approved: bool,
    #[serde(default)]
    pub always_allow: bool,
    #[serde(default)]
    pub request_id: Option<String>,
}

/// Resolve a pending tool approval request.
///
/// The frontend calls this after the user clicks Approve or Deny in the ApprovalDock.
pub(crate) async fn resolve_approval(
    State(state): State<WebState>,
    Json(req): Json<ResolveApprovalRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    use ava_tools::permission_middleware::ToolApproval;

    let _interactive_guard = state.inner.interactive_lifecycle_lock.lock().await;

    let request_id = req
        .request_id
        .as_deref()
        .ok_or_else(|| missing_request_id_error("approval"))?;

    let reply = state
        .inner
        .pending_approval_reply
        .resolve(Some(request_id))
        .await
        .map_err(|err| interactive_resolve_error_response("approval", err))?;

    let approval = if req.approved {
        if req.always_allow {
            ToolApproval::AllowAlways
        } else {
            ToolApproval::AllowedForSession
        }
    } else {
        ToolApproval::Rejected(Some("User denied via web UI".to_string()))
    };

    if reply.reply.send(approval).is_err() {
        emit_interactive_request_cleared(
            &state,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        return Err(error_response(
            StatusCode::GONE,
            "Failed to send approval — the agent may have already moved on",
        ));
    }

    emit_interactive_request_cleared(
        &state,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );

    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct ResolveQuestionRequest {
    pub answer: String,
    #[serde(default)]
    pub request_id: Option<String>,
}

/// Resolve a pending question request.
pub(crate) async fn resolve_question(
    State(state): State<WebState>,
    Json(req): Json<ResolveQuestionRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let _interactive_guard = state.inner.interactive_lifecycle_lock.lock().await;
    let request_id = req
        .request_id
        .as_deref()
        .ok_or_else(|| missing_request_id_error("question"))?;

    let reply = state
        .inner
        .pending_question_reply
        .resolve(Some(request_id))
        .await
        .map_err(|err| interactive_resolve_error_response("question", err))?;

    if reply.reply.send(req.answer).is_err() {
        emit_interactive_request_cleared(
            &state,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        return Err(error_response(
            StatusCode::GONE,
            "Failed to send answer — the agent may have already moved on",
        ));
    }

    emit_interactive_request_cleared(
        &state,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );

    Ok(Json(serde_json::json!({ "ok": true })))
}

#[derive(Deserialize)]
pub struct ResolvePlanRequest {
    pub response: String,
    #[serde(default)]
    pub request_id: Option<String>,
    #[serde(default)]
    pub modified_plan: Option<serde_json::Value>,
    #[serde(default)]
    pub feedback: Option<String>,
    #[serde(default)]
    pub step_comments: Option<std::collections::HashMap<String, String>>,
}

/// Resolve a pending plan approval request.
pub(crate) async fn resolve_plan(
    State(state): State<WebState>,
    Json(req): Json<ResolvePlanRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    let _interactive_guard = state.inner.interactive_lifecycle_lock.lock().await;
    let request_id = req
        .request_id
        .as_deref()
        .ok_or_else(|| missing_request_id_error("plan"))?;

    let feedback = req.feedback.unwrap_or_default();
    let decision = match req.response.as_str() {
        "approved" => ava_types::PlanDecision::Approved,
        "rejected" => ava_types::PlanDecision::Rejected { feedback },
        "modified" => {
            let plan: ava_types::Plan = req
                .modified_plan
                .ok_or_else(|| {
                    error_response(
                        StatusCode::BAD_REQUEST,
                        "modified_plan is required for 'modified' response",
                    )
                })
                .and_then(|v| {
                    serde_json::from_value(v).map_err(|e| {
                        error_response(
                            StatusCode::BAD_REQUEST,
                            &format!("Invalid modified_plan: {e}"),
                        )
                    })
                })?;
            ava_types::PlanDecision::Modified { plan, feedback }
        }
        other => {
            return Err(error_response(
                StatusCode::BAD_REQUEST,
                &format!(
                    "Invalid response '{other}'. Expected 'approved', 'rejected', or 'modified'"
                ),
            ));
        }
    };

    let reply = state
        .inner
        .pending_plan_reply
        .resolve(Some(request_id))
        .await
        .map_err(|err| interactive_resolve_error_response("plan", err))?;

    if reply.reply.send(decision).is_err() {
        emit_interactive_request_cleared(
            &state,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        return Err(error_response(
            StatusCode::GONE,
            "Failed to send plan decision — the agent may have already moved on",
        ));
    }

    emit_interactive_request_cleared(
        &state,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );

    Ok(Json(serde_json::json!({ "ok": true })))
}

fn interactive_resolve_error_response(
    kind: &str,
    error: ResolveInteractiveRequestError,
) -> (StatusCode, Json<ErrorResponse>) {
    match error {
        ResolveInteractiveRequestError::MissingPendingRequest { .. } => {
            error_response(StatusCode::CONFLICT, &format!("No pending {kind} request"))
        }
        ResolveInteractiveRequestError::StaleRequestId { .. } => error_response(
            StatusCode::CONFLICT,
            &format!("No matching pending {kind} request"),
        ),
    }
}

// ============================================================================
// Undo
// ============================================================================

#[derive(Serialize)]
pub struct UndoResult {
    pub success: bool,
    pub message: String,
    #[serde(rename = "filePath")]
    pub file_path: Option<String>,
}

/// Undo the last file edit made by the agent.
pub(crate) async fn undo_last_edit(
    State(state): State<WebState>,
) -> Result<Json<UndoResult>, (StatusCode, Json<ErrorResponse>)> {
    let record = state.inner.edit_history.write().await.pop_back();

    match record {
        Some(edit) => {
            let path = edit.file_path.clone();
            match tokio::fs::write(&edit.file_path, &edit.previous_content).await {
                Ok(()) => {
                    info!(file = %path, "Web: undo_last_edit restored file");
                    Ok(Json(UndoResult {
                        success: true,
                        message: format!("Restored {path} to its previous content"),
                        file_path: Some(path),
                    }))
                }
                Err(e) => Ok(Json(UndoResult {
                    success: false,
                    message: format!("Failed to restore {path}: {e}"),
                    file_path: Some(path),
                })),
            }
        }
        None => Ok(Json(UndoResult {
            success: false,
            message: "No file edits to undo".to_string(),
            file_path: None,
        })),
    }
}
