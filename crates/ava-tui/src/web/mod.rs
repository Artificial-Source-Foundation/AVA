//! AVA web server — HTTP API + WebSocket for agent events.
//!
//! Replaces the Tauri IPC layer for browser-based frontends. The server
//! exposes a JSON REST API under `/api/` and a WebSocket endpoint at `/ws`
//! for streaming agent events.
//!
//! # Endpoints
//!
//! | Method | Path                              | Description                               |
//! |--------|-----------------------------------|-------------------------------------------|
//! | POST   | `/api/agent/submit`               | Start the agent (async, streams via WS)   |
//! | POST   | `/api/agent/cancel`               | Cancel the running agent                  |
//! | GET    | `/api/agent/status`               | Get agent running status                  |
//! | POST   | `/api/agent/resolve-approval`     | Resolve a pending tool approval request   |
//! | POST   | `/api/agent/resolve-question`     | Resolve a pending question request        |
//! | POST   | `/api/agent/resolve-plan`         | Resolve a pending plan approval request   |
//! | POST   | `/api/agent/retry`                | Retry the last user message               |
//! | POST   | `/api/agent/edit-resend`          | Edit a message and re-run the agent       |
//! | POST   | `/api/agent/regenerate`           | Regenerate the last assistant response    |
//! | POST   | `/api/agent/undo`                 | Undo the last file edit                   |
//! | POST   | `/api/agent/steer`                | Inject a steering message (Tier 1)        |
//! | POST   | `/api/agent/follow-up`            | Queue a follow-up message (Tier 2)        |
//! | POST   | `/api/agent/post-complete`        | Queue a post-complete message (Tier 3)    |
//! | GET    | `/api/agent/queue`                | Get message queue state                   |
//! | POST   | `/api/agent/queue/clear`          | Clear the message queue                   |
//! | POST   | `/api/context/compact`            | Compact conversation context               |
//! | GET    | `/api/sessions`                   | List recent sessions                      |
//! | POST   | `/api/sessions/create`            | Create a new session                      |
//! | POST   | `/api/sessions/search`            | Search sessions by message content        |
//! | GET    | `/api/sessions/{id}`              | Get session with messages                 |
//! | POST   | `/api/sessions/{id}/rename`       | Rename a session                          |
//! | DELETE | `/api/sessions/{id}`              | Delete a session                          |
//! | GET    | `/api/sessions/{id}/messages`     | List all messages for a session           |
//! | POST   | `/api/sessions/{id}/message`      | Add a message to a session                |
//! | PATCH  | `/api/sessions/{id}/messages/{msg_id}` | Update an existing message (content/metadata) |
//! | GET    | `/api/sessions/{id}/agents`       | List agents for a session (stub)          |
//! | GET    | `/api/sessions/{id}/files`        | List file operations (stub)               |
//! | GET    | `/api/sessions/{id}/terminal`     | List terminal executions (stub)           |
//! | GET    | `/api/sessions/{id}/memory`       | List memory items (stub)                  |
//! | GET    | `/api/sessions/{id}/checkpoints`  | List checkpoints (stub)                   |
//! | GET    | `/api/mcp`                        | List configured MCP servers               |
//! | POST   | `/api/mcp/reload`                 | Reload MCP config from disk               |
//! | POST   | `/api/mcp/servers/{name}/enable`  | Enable an MCP server                      |
//! | POST   | `/api/mcp/servers/{name}/disable` | Disable an MCP server                     |
//! | GET    | `/api/plugins`                    | List loaded power plugins                 |
//! | GET    | `/api/models`                     | List available models                     |
//! | GET    | `/api/models/current`             | Get the currently-active model            |
//! | POST   | `/api/models/switch`              | Switch the active model                   |
//! | GET    | `/api/providers`                  | List configured providers                 |
//! | GET    | `/api/cli-agents`                 | List discovered CLI agents                |
//! | GET    | `/api/config`                     | Get full configuration as JSON            |
//! | POST   | `/api/tools/agent`                | List runtime-visible tools for a session  |
//! | GET    | `/api/permissions`                | Get current permission level              |
//! | POST   | `/api/permissions`                | Set permission level                      |
//! | POST   | `/api/permissions/toggle`         | Toggle permission level                   |
//! | GET    | `/api/plans`                      | List saved plans from `.ava/plans/`       |
//! | GET    | `/api/plans/{filename}`           | Load a specific saved plan                |
//! | POST   | `/api/log`                        | Ingest frontend log entry                 |
//! | GET    | `/api/health`                     | Health check                              |
//! | GET    | `/ws`                             | WebSocket for streaming events            |

pub mod api;
mod api_agent;
mod api_config;
mod api_interactive;
mod api_plans;
mod api_plugin_host;
mod api_sessions;
mod api_tools;
pub mod state;
pub mod ws;

use axum::http::Method;
use axum::routing::{get, patch, post};
use axum::Router;
use color_eyre::Result;
use tower_http::cors::{Any, CorsLayer};
use tower_http::trace::TraceLayer;
use tracing::info;

use self::state::WebState;

/// Build the axum router with all API routes and WebSocket endpoint.
fn build_router(state: WebState) -> Router {
    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_methods([
            Method::GET,
            Method::POST,
            Method::PUT,
            Method::PATCH,
            Method::DELETE,
            Method::OPTIONS,
        ])
        .allow_headers(Any);

    Router::new()
        // Agent endpoints
        .route("/api/agent/submit", post(api::submit_goal))
        .route("/api/agent/cancel", post(api::cancel_agent))
        .route("/api/agent/status", get(api::agent_status))
        // Interactive approval / question / plan resolution
        .route("/api/agent/resolve-approval", post(api::resolve_approval))
        .route("/api/agent/resolve-question", post(api::resolve_question))
        .route("/api/agent/resolve-plan", post(api::resolve_plan))
        // Retry / edit-resend / regenerate / undo
        .route("/api/agent/retry", post(api::retry_last_message))
        .route("/api/agent/edit-resend", post(api::edit_and_resend))
        .route("/api/agent/regenerate", post(api::regenerate_response))
        .route("/api/agent/undo", post(api::undo_last_edit))
        // Mid-stream messaging (3-tier)
        .route("/api/agent/steer", post(api::steer_agent))
        .route("/api/agent/follow-up", post(api::follow_up_agent))
        .route("/api/agent/post-complete", post(api::post_complete_agent))
        .route("/api/agent/queue", get(api::get_message_queue))
        .route("/api/agent/queue/clear", post(api::clear_message_queue))
        // Context compaction
        .route("/api/context/compact", post(api::compact_context))
        // Session CRUD endpoints
        .route("/api/sessions", get(api::list_sessions))
        .route("/api/sessions/create", post(api::create_session))
        .route("/api/sessions/search", post(api::search_sessions))
        .route(
            "/api/sessions/{id}",
            get(api::get_session).delete(api::delete_session),
        )
        .route("/api/sessions/{id}/rename", post(api::rename_session))
        .route("/api/sessions/{id}/duplicate", post(api::duplicate_session))
        // Message endpoints
        .route(
            "/api/sessions/{id}/messages",
            get(api::get_session_messages),
        )
        .route("/api/sessions/{id}/message", post(api::add_message))
        .route(
            "/api/sessions/{id}/messages/{msg_id}",
            patch(api::update_message),
        )
        // Session sub-resource stubs (web DB parity)
        .route("/api/sessions/{id}/agents", get(api::list_session_agents))
        .route("/api/sessions/{id}/files", get(api::list_session_files))
        .route(
            "/api/sessions/{id}/terminal",
            get(api::list_session_terminal),
        )
        .route("/api/sessions/{id}/memory", get(api::list_session_memory))
        .route(
            "/api/sessions/{id}/checkpoints",
            get(api::list_session_checkpoints),
        )
        // Body-based session operations (for frontend apiInvoke compatibility)
        .route("/api/sessions/delete", post(api::delete_session_body))
        .route("/api/sessions/rename", post(api::rename_session_body))
        .route("/api/sessions/load", post(api::load_session_body))
        // MCP endpoints
        .route("/api/mcp", get(api::list_mcp_servers))
        .route("/api/mcp/reload", post(api::reload_mcp))
        .route(
            "/api/mcp/servers/{name}/enable",
            post(api::enable_mcp_server),
        )
        .route(
            "/api/mcp/servers/{name}/disable",
            post(api::disable_mcp_server),
        )
        // Plugins endpoint
        .route("/api/plugins", get(api::list_plugins))
        .route("/api/plugins/mounts", get(api::list_plugin_mounts))
        .route(
            "/api/plugins/{plugin}/commands/{command}",
            post(api::invoke_plugin_command),
        )
        .route(
            "/api/plugins/{plugin}/routes/{*route_path}",
            get(api::get_plugin_route).post(api::post_plugin_route),
        )
        // Model/provider endpoints
        .route("/api/models", get(api::list_models))
        .route("/api/models/current", get(api::get_current_model))
        .route("/api/models/switch", post(api::switch_model))
        .route("/api/providers", get(api::list_providers))
        .route("/api/usage", get(api::get_subscription_usage))
        .route("/api/cli-agents", get(api::list_cli_agents))
        // Config
        .route("/api/config", get(api::get_config))
        // Tools
        .route("/api/tools/agent", post(api::list_agent_tools))
        // Permission level
        .route(
            "/api/permissions",
            get(api::get_permission_level).post(api::set_permission_level),
        )
        .route(
            "/api/permissions/toggle",
            post(api::toggle_permission_level),
        )
        // Plan persistence
        .route("/api/plans", get(api_plans::list_plans))
        .route("/api/plans/{filename}", get(api_plans::get_plan))
        // WebSocket
        .route("/ws", get(ws::ws_handler))
        // Frontend log ingestion
        .route("/api/log", post(api::ingest_frontend_log))
        // Health check
        .route("/api/health", get(api::health))
        .layer(cors)
        .layer(TraceLayer::new_for_http())
        .with_state(state)
}

/// Start the AVA web server on the given host and port.
///
/// The TCP listener is bound **before** `AgentStack` construction so that the
/// port is claimed immediately.  MCP servers connect lazily on the first API
/// call (background task, 30 s timeout per server, all in parallel) — the web
/// server never waits for MCP at startup.
pub async fn run_server(host: &str, port: u16) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

    // Ensure the logs directory exists for frontend log ingestion
    let logs_dir = data_dir.join("logs");
    std::fs::create_dir_all(&logs_dir).ok();

    // Bind the port FIRST so the address is claimed before any slow init.
    let addr = format!("{host}:{port}");
    let listener = tokio::net::TcpListener::bind(&addr).await?;

    info!("AVA web server listening on http://{addr}");
    info!("  API:       http://{addr}/api/");
    info!("  WebSocket: ws://{addr}/ws");
    info!("  Health:    http://{addr}/api/health");
    info!("Press Ctrl+C to stop.");
    info!("Initialising agent stack (MCP connects lazily on first use)…");

    // Initialise the agent stack.  `AgentStack::new` spawns codebase indexing
    // as a background task; MCP lazy-init fires on the first run() call.
    // The listener is already bound above, so the port is usable immediately.
    let state = WebState::init(data_dir).await?;
    info!("Agent stack ready — serving requests.");

    let app = build_router(state);

    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown_signal())
        .await?;

    info!("Web server shut down.");
    Ok(())
}

/// Wait for Ctrl+C to gracefully shut down.
async fn shutdown_signal() {
    tokio::signal::ctrl_c()
        .await
        .expect("failed to install Ctrl+C handler");
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::web::state::WebEvent;
    use ava_tools::permission_middleware::ToolApproval;
    use ava_types::PlanDecision;
    use axum::body::{to_bytes, Body};
    use axum::http::{Request, StatusCode};
    use tokio::sync::oneshot;
    use tower::ServiceExt;

    #[tokio::test]
    async fn resolve_plan_route_requires_request_id_and_preserves_pending_state() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_plan_reply.register(tx).await;

        let retry_state = state.clone();
        let app = build_router(state);

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(r#"{"response":"approved"}"#))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("request_id is required"));

        let app = build_router(retry_state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","response":"approved"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        let decision = rx.await.expect("plan decision");
        assert!(matches!(decision, PlanDecision::Approved));
    }

    #[tokio::test]
    async fn resolve_plan_route_returns_ok_for_approved_decision() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_plan_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","response":"approved"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);

        let decision = rx.await.expect("plan decision");
        assert!(matches!(decision, PlanDecision::Approved));

        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "plan");
                assert!(!timed_out);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn resolve_approval_route_emits_clear_event_on_success() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_approval_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-approval")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","approved":true}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        assert!(matches!(
            rx.await.expect("approval decision"),
            ToolApproval::AllowedForSession
        ));

        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "approval");
                assert!(!timed_out);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn resolve_question_route_emits_clear_event_on_success() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_question_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-question")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","answer":"yes"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(rx.await.expect("question answer"), "yes");

        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "question");
                assert!(!timed_out);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn resolve_plan_route_returns_bad_request_for_modified_without_plan() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let retry_state = state.clone();

        let (tx, _rx) = oneshot::channel();
        let request = state.inner.pending_plan_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","response":"modified"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("modified_plan is required"));

        let app = build_router(retry_state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","response":"approved"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn resolve_plan_route_accepts_modified_plan_payloads() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_plan_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","response":"modified","modified_plan":{{"summary":"Ship polish","steps":[{{"id":"step-1","description":"Do it","files":[],"action":"implement","depends_on":[]}}],"estimated_turns":2}},"feedback":"looks good"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        match rx.await.expect("plan decision") {
            PlanDecision::Modified { plan, feedback } => {
                assert_eq!(plan.summary, "Ship polish");
                assert_eq!(plan.estimated_turns, Some(2));
                assert_eq!(feedback, "looks good");
            }
            other => panic!("expected modified plan decision, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn resolve_plan_route_rejects_stale_request_ids_without_consuming_current_request() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let retry_state = state.clone();

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_plan_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        r#"{"request_id":"plan-stale","response":"approved"}"#,
                    ))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::CONFLICT);
        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("No matching pending plan request"));

        let app = build_router(retry_state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","response":"approved"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        let decision = rx.await.expect("plan decision");
        assert!(matches!(decision, PlanDecision::Approved));
    }

    #[tokio::test]
    async fn resolve_plan_route_emits_clear_event_when_receiver_is_gone() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();

        let (tx, rx) = oneshot::channel::<PlanDecision>();
        drop(rx);
        let request = state.inner.pending_plan_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-plan")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","response":"approved"}}"#,
                        request.request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::GONE);
        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "plan");
                assert!(!timed_out);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn cancel_route_clears_pending_requests_and_emits_clear_event() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_approval_reply.register(tx).await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/cancel")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        match rx.await.expect("cancelled approval reply") {
            ToolApproval::Rejected(Some(reason)) => {
                assert!(reason.contains("cancelled from web UI"));
            }
            other => panic!("expected rejected approval, got {other:?}"),
        }

        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "approval");
                assert!(!timed_out);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn clear_message_queue_rejects_unsupported_follow_up_targets() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router(state);

        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/queue/clear")
                    .header("content-type", "application/json")
                    .body(Body::from(r#"{"target":"followUp"}"#))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("not supported yet"));
    }

    #[tokio::test]
    async fn edit_resend_route_rejects_invalid_message_targets() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let session_id = uuid::Uuid::new_v4();
        let mut session = ava_types::Session::new().with_id(session_id);
        session.add_message(ava_types::Message::new(ava_types::Role::User, "hello"));
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .expect("save session");
        *state.inner.last_session_id.write().await = Some(session_id);

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/edit-resend")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        r#"{"message_id":"not-a-uuid","new_content":"retry this"}"#,
                    ))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::CONFLICT);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("Invalid message ID"));
    }

    #[tokio::test]
    async fn edit_resend_route_rejects_non_user_targets() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let session_id = uuid::Uuid::new_v4();
        let mut session = ava_types::Session::new().with_id(session_id);
        let assistant = ava_types::Message::new(ava_types::Role::Assistant, "done");
        let assistant_id = assistant.id;
        session.add_message(assistant);
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .expect("save session");
        *state.inner.last_session_id.write().await = Some(session_id);

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/edit-resend")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"message_id":"{assistant_id}","new_content":"retry this"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::CONFLICT);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("Only user messages can be edited"));
    }
}
