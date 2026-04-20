//! Interactive agent resolution endpoints: approval, question, plan, and undo.

use ava_agent::control_plane::interactive::ResolveInteractiveRequestError;
use axum::extract::State;
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tracing::info;

use super::api::{error_response, ErrorResponse};
use super::api_agent::{
    cancel_run, discard_deferred_interactive_request_event,
    emit_or_defer_interactive_request_event, emit_promoted_interactive_request_event,
    RunCorrelationRequest,
};
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
        discard_deferred_interactive_request_event(&state.inner, &reply.handle.request_id).await;
        emit_interactive_request_cleared(
            &state,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        emit_promoted_interactive_request_event(
            &state.inner,
            reply.handle.kind,
            reply.handle.run_id.as_deref(),
        )
        .await;
        return Err(error_response(
            StatusCode::GONE,
            "Failed to send approval — the agent may have already moved on",
        ));
    }

    discard_deferred_interactive_request_event(&state.inner, &reply.handle.request_id).await;
    emit_interactive_request_cleared(
        &state,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );
    emit_promoted_interactive_request_event(
        &state.inner,
        reply.handle.kind,
        reply.handle.run_id.as_deref(),
    )
    .await;

    Ok(Json(serde_json::json!({ "ok": true })))
}

#[cfg(debug_assertions)]
#[derive(Deserialize)]
pub struct InjectApprovalRequest {
    #[serde(alias = "sessionId")]
    pub session_id: String,
    #[serde(default)]
    pub tool_name: Option<String>,
    #[serde(default)]
    pub tool_call_id: Option<String>,
    #[serde(default)]
    pub args: Option<Value>,
    #[serde(default)]
    pub risk_level: Option<String>,
    #[serde(default)]
    pub reason: Option<String>,
    #[serde(default)]
    pub warnings: Option<Vec<String>>,
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    pub provider: Option<String>,
    #[serde(default)]
    pub model: Option<String>,
}

#[cfg(debug_assertions)]
fn debug_bad_request(message: &str) -> (StatusCode, Json<ErrorResponse>) {
    error_response(StatusCode::BAD_REQUEST, message)
}

#[cfg(debug_assertions)]
async fn ensure_debug_run(
    state: &WebState,
    session_id: uuid::Uuid,
    requested_run_id: Option<&str>,
    request_kind: &str,
    provider: &str,
    model: &str,
) -> Result<(String, bool), (StatusCode, Json<ErrorResponse>)> {
    if let Some(run_id) = requested_run_id {
        if state
            .resolve_run(Some(run_id), Some(session_id))
            .await
            .is_ok()
        {
            return Ok((run_id.to_string(), false));
        }
    } else if let Ok(run) = state.resolve_run(None, Some(session_id)).await {
        return Ok((run.run_id.clone(), false));
    }

    let run_id = requested_run_id
        .map(str::to_string)
        .unwrap_or_else(|| format!("debug-{request_kind}-run-{}", uuid::Uuid::new_v4()));

    state
        .register_run(
            run_id.clone(),
            session_id,
            provider.to_string(),
            model.to_string(),
        )
        .await
        .map_err(|message| error_response(StatusCode::CONFLICT, &message))?;

    Ok((run_id, true))
}

#[cfg(debug_assertions)]
pub(crate) async fn inject_approval_request(
    State(state): State<WebState>,
    Json(req): Json<InjectApprovalRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    use ava_agent::control_plane::interactive::InteractiveRequestKind;
    use ava_tools::permission_middleware::ToolApproval;
    use tokio::sync::oneshot;

    let session_id = uuid::Uuid::parse_str(&req.session_id)
        .map_err(|e| debug_bad_request(&format!("Invalid session_id: {e}")))?;
    let provider = req.provider.unwrap_or_else(|| "debug".to_string());
    let model = req.model.unwrap_or_else(|| "debug-approval".to_string());
    let (run_id, synthetic_run) = ensure_debug_run(
        &state,
        session_id,
        req.run_id.as_deref(),
        "approval",
        &provider,
        &model,
    )
    .await?;

    let (reply_tx, reply_rx) = oneshot::channel::<ToolApproval>();
    let handle = state
        .inner
        .pending_approval_reply
        .register_with_run_id(reply_tx, Some(run_id.clone()))
        .await;

    if synthetic_run {
        let state_for_cleanup = state.clone();
        let synthetic_run_id = run_id.clone();
        tokio::spawn(async move {
            let _ = reply_rx.await;
            state_for_cleanup.finish_run(&synthetic_run_id).await;
        });
    } else {
        tokio::spawn(async move {
            let _ = reply_rx.await;
        });
    }

    let tool_name = req.tool_name.unwrap_or_else(|| "bash".to_string());
    let tool_call_id = req
        .tool_call_id
        .unwrap_or_else(|| format!("tool-call-{}", handle.request_id));
    let args = req
        .args
        .unwrap_or_else(|| serde_json::json!({ "command": "pwd" }));
    let risk_level = req.risk_level.unwrap_or_else(|| "medium".to_string());
    let reason = req
        .reason
        .unwrap_or_else(|| "Deterministic approval request for browser E2E".to_string());
    let warnings = req.warnings.unwrap_or_default();

    emit_or_defer_interactive_request_event(
        &state.inner,
        &handle.request_id,
        InteractiveRequestKind::Approval,
        Some(run_id.as_str()),
        super::state::WebEvent::ApprovalRequest {
            id: handle.request_id.clone(),
            tool_call_id,
            tool_name,
            args,
            risk_level,
            reason,
            warnings,
            run_id: handle.run_id.clone(),
        },
    )
    .await;

    Ok(Json(serde_json::json!({
        "ok": true,
        "requestId": handle.request_id,
        "runId": run_id,
        "syntheticRun": synthetic_run,
    })))
}

#[cfg(debug_assertions)]
#[derive(Deserialize)]
pub struct InjectQuestionRequest {
    #[serde(alias = "sessionId")]
    pub session_id: String,
    #[serde(default)]
    pub question: Option<String>,
    #[serde(default)]
    pub options: Option<Vec<String>>,
    #[serde(default)]
    #[serde(alias = "runId")]
    pub run_id: Option<String>,
    #[serde(default)]
    pub provider: Option<String>,
    #[serde(default)]
    pub model: Option<String>,
}

#[cfg(debug_assertions)]
pub(crate) async fn inject_question_request(
    State(state): State<WebState>,
    Json(req): Json<InjectQuestionRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    use ava_agent::control_plane::interactive::InteractiveRequestKind;
    use tokio::sync::oneshot;

    let session_id = uuid::Uuid::parse_str(&req.session_id)
        .map_err(|e| debug_bad_request(&format!("Invalid session_id: {e}")))?;
    let provider = req.provider.unwrap_or_else(|| "debug".to_string());
    let model = req.model.unwrap_or_else(|| "debug-question".to_string());
    let (run_id, synthetic_run) = ensure_debug_run(
        &state,
        session_id,
        req.run_id.as_deref(),
        "question",
        &provider,
        &model,
    )
    .await?;

    let (reply_tx, reply_rx) = oneshot::channel::<String>();
    let handle = state
        .inner
        .pending_question_reply
        .register_with_run_id(reply_tx, Some(run_id.clone()))
        .await;

    if synthetic_run {
        let state_for_cleanup = state.clone();
        let synthetic_run_id = run_id.clone();
        tokio::spawn(async move {
            let _ = reply_rx.await;
            state_for_cleanup.finish_run(&synthetic_run_id).await;
        });
    } else {
        tokio::spawn(async move {
            let _ = reply_rx.await;
        });
    }

    let question = req
        .question
        .unwrap_or_else(|| "Deterministic question request for browser E2E".to_string());
    let options = req.options.unwrap_or_default();

    emit_or_defer_interactive_request_event(
        &state.inner,
        &handle.request_id,
        InteractiveRequestKind::Question,
        Some(run_id.as_str()),
        super::state::WebEvent::QuestionRequest {
            id: handle.request_id.clone(),
            question,
            options,
            run_id: handle.run_id.clone(),
        },
    )
    .await;

    Ok(Json(serde_json::json!({
        "ok": true,
        "requestId": handle.request_id,
        "runId": run_id,
        "syntheticRun": synthetic_run,
    })))
}

#[cfg(debug_assertions)]
#[derive(Deserialize)]
pub struct FinishDebugRunRequest {
    #[serde(alias = "runId")]
    pub run_id: String,
}

#[cfg(debug_assertions)]
pub(crate) async fn finish_debug_run(
    State(state): State<WebState>,
    Json(req): Json<FinishDebugRunRequest>,
) -> Result<impl IntoResponse, (StatusCode, Json<ErrorResponse>)> {
    if req.run_id.trim().is_empty() {
        return Err(debug_bad_request("run_id is required"));
    }

    cancel_run(&state, &req.run_id).await;
    state.finish_run(&req.run_id).await;

    Ok(Json(serde_json::json!({
        "ok": true,
        "runId": req.run_id,
    })))
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
        discard_deferred_interactive_request_event(&state.inner, &reply.handle.request_id).await;
        emit_interactive_request_cleared(
            &state,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        emit_promoted_interactive_request_event(
            &state.inner,
            reply.handle.kind,
            reply.handle.run_id.as_deref(),
        )
        .await;
        return Err(error_response(
            StatusCode::GONE,
            "Failed to send answer — the agent may have already moved on",
        ));
    }

    discard_deferred_interactive_request_event(&state.inner, &reply.handle.request_id).await;
    emit_interactive_request_cleared(
        &state,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );
    emit_promoted_interactive_request_event(
        &state.inner,
        reply.handle.kind,
        reply.handle.run_id.as_deref(),
    )
    .await;

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
    #[serde(rename = "step_comments")]
    pub _step_comments: Option<std::collections::HashMap<String, String>>,
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
        discard_deferred_interactive_request_event(&state.inner, &reply.handle.request_id).await;
        emit_interactive_request_cleared(
            &state,
            &reply.handle.request_id,
            reply.handle.kind.as_str(),
            false,
            reply.handle.run_id.as_deref(),
        );
        emit_promoted_interactive_request_event(
            &state.inner,
            reply.handle.kind,
            reply.handle.run_id.as_deref(),
        )
        .await;
        return Err(error_response(
            StatusCode::GONE,
            "Failed to send plan decision — the agent may have already moved on",
        ));
    }

    discard_deferred_interactive_request_event(&state.inner, &reply.handle.request_id).await;
    emit_interactive_request_cleared(
        &state,
        &reply.handle.request_id,
        reply.handle.kind.as_str(),
        false,
        reply.handle.run_id.as_deref(),
    );
    emit_promoted_interactive_request_event(
        &state.inner,
        reply.handle.kind,
        reply.handle.run_id.as_deref(),
    )
    .await;

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
    maybe_req: Option<Json<RunCorrelationRequest>>,
) -> Result<Json<UndoResult>, (StatusCode, Json<ErrorResponse>)> {
    let req = maybe_req.map(|Json(req)| req).unwrap_or_default();
    let requested_session_id = req
        .session_id
        .as_deref()
        .map(uuid::Uuid::parse_str)
        .transpose()
        .map_err(|e| {
            error_response(StatusCode::BAD_REQUEST, &format!("invalid session_id: {e}"))
        })?;

    let session_id = if let Some(session_id) = requested_session_id {
        if let Some(run_id) = req.run_id.as_deref() {
            if let Ok(run) = state.resolve_run(Some(run_id), None).await {
                if run.session_id != session_id {
                    return Err(error_response(
                        StatusCode::CONFLICT,
                        &format!("Run {run_id} does not own session {session_id}"),
                    ));
                }
            }
        }
        session_id
    } else if let Some(run_id) = req.run_id.as_deref() {
        state
            .resolve_run(Some(run_id), requested_session_id)
            .await
            .map_err(|message| error_response(StatusCode::CONFLICT, &message))?
            .session_id
    } else {
        (*state.inner.last_session_id.read().await)
            .ok_or_else(|| error_response(StatusCode::CONFLICT, "No previous session to undo"))?
    };

    let record = state.pop_last_edit(session_id).await;

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
