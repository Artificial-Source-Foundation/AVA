use std::collections::HashMap;

use axum::extract::{Path, Query, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;
use serde::Serialize;
use serde_json::Value;

use super::state::{WebEvent, WebState};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginHostInvokeResponse {
    pub result: Value,
    pub emitted_events: Vec<ava_plugin::PluginAppEvent>,
}

pub(crate) async fn list_plugin_mounts(State(state): State<WebState>) -> impl IntoResponse {
    let manager = state.inner.stack.plugin_manager.lock().await;
    Json(manager.list_plugin_mounts())
}

pub(crate) async fn invoke_plugin_command(
    State(state): State<WebState>,
    Path((plugin, command)): Path<(String, String)>,
    Json(request_body): Json<Value>,
) -> impl IntoResponse {
    let payload = request_body.get("payload").cloned().unwrap_or(request_body);
    let handle = {
        let manager = state.inner.stack.plugin_manager.lock().await;
        manager.get_app_command_handle(&plugin, &command)
    };
    let response = match handle {
        Ok(handle) => handle.invoke_command(&command, payload).await,
        Err(error) => Err(error),
    };

    match response {
        Ok(response) => {
            emit_plugin_events(&state, &plugin, &response.emitted_events);
            (
                StatusCode::OK,
                Json(PluginHostInvokeResponse {
                    result: response.result,
                    emitted_events: response.emitted_events,
                }),
            )
                .into_response()
        }
        Err(error) => (
            StatusCode::BAD_REQUEST,
            Json(serde_json::json!({ "error": error.to_string() })),
        )
            .into_response(),
    }
}

pub(crate) async fn get_plugin_route(
    State(state): State<WebState>,
    Path((plugin, route_path)): Path<(String, String)>,
    Query(query): Query<HashMap<String, String>>,
) -> impl IntoResponse {
    invoke_plugin_route_inner(state, plugin, route_path, "GET", query, None).await
}

pub(crate) async fn post_plugin_route(
    State(state): State<WebState>,
    Path((plugin, route_path)): Path<(String, String)>,
    Query(query): Query<HashMap<String, String>>,
    Json(body): Json<Value>,
) -> impl IntoResponse {
    invoke_plugin_route_inner(state, plugin, route_path, "POST", query, Some(body)).await
}

async fn invoke_plugin_route_inner(
    state: WebState,
    plugin: String,
    route_path: String,
    method: &str,
    query: HashMap<String, String>,
    body: Option<Value>,
) -> axum::response::Response {
    let normalized_path = format!("/{}", route_path.trim_start_matches('/'));
    let query = serde_json::to_value(query).unwrap_or_default();
    let handle = {
        let manager = state.inner.stack.plugin_manager.lock().await;
        manager.get_app_route_handle(&plugin, method, &normalized_path)
    };
    let response = match handle {
        Ok(handle) => {
            handle
                .invoke_route(method, &normalized_path, query, body)
                .await
        }
        Err(error) => Err(error),
    };

    match response {
        Ok(response) => {
            emit_plugin_events(&state, &plugin, &response.emitted_events);
            (
                StatusCode::OK,
                Json(PluginHostInvokeResponse {
                    result: response.result,
                    emitted_events: response.emitted_events,
                }),
            )
                .into_response()
        }
        Err(error) => (
            StatusCode::BAD_REQUEST,
            Json(serde_json::json!({ "error": error.to_string() })),
        )
            .into_response(),
    }
}

fn emit_plugin_events(state: &WebState, plugin: &str, events: &[ava_plugin::PluginAppEvent]) {
    for event in events {
        let _ = state.inner.event_tx.send(WebEvent::Plugin {
            plugin: plugin.to_string(),
            event: event.event.clone(),
            payload: event.payload.clone(),
        });
    }
}
