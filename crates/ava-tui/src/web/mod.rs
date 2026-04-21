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
//! | POST   | `/api/sessions/{id}/archive`      | Archive a session                         |
//! | POST   | `/api/sessions/{id}/unarchive`    | Restore an archived session               |
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
mod security;
pub mod state;
pub mod ws;

use axum::middleware::from_fn_with_state;
use axum::routing::{get, patch, post};
use axum::Router;
use color_eyre::Result;
use std::io::IsTerminal;
use tower_http::trace::TraceLayer;
use tracing::info;
use uuid::Uuid;

use self::security::WebSecurityConfig;
use self::state::WebState;

/// Build the axum router with all API routes and WebSocket endpoint.
fn build_router_with_security(state: WebState, security: WebSecurityConfig) -> Router {
    let public_routes = Router::new()
        .route("/api/mcp", get(api::list_mcp_servers))
        .route("/api/plugins", get(api::list_plugins))
        .route("/api/models", get(api::list_models))
        .route("/api/models/current", get(api::get_current_model))
        .route("/api/providers", get(api::list_providers))
        .route("/api/tools/agent", post(api::list_agent_tools))
        .route("/api/log", post(api::ingest_frontend_log))
        .route("/api/health", get(api::health));

    let protected_routes = Router::new()
        .route("/api/agent/status", get(api::agent_status))
        .route("/api/agent/submit", post(api::submit_goal))
        .route("/api/agent/cancel", post(api::cancel_agent))
        .route("/api/agent/resolve-approval", post(api::resolve_approval))
        .route("/api/agent/resolve-question", post(api::resolve_question))
        .route("/api/agent/resolve-plan", post(api::resolve_plan))
        .route("/api/agent/retry", post(api::retry_last_message))
        .route("/api/agent/edit-resend", post(api::edit_and_resend))
        .route("/api/agent/regenerate", post(api::regenerate_response))
        .route("/api/agent/undo", post(api::undo_last_edit))
        .route("/api/agent/steer", post(api::steer_agent))
        .route("/api/agent/follow-up", post(api::follow_up_agent))
        .route("/api/agent/post-complete", post(api::post_complete_agent))
        .route("/api/agent/queue", get(api::get_message_queue))
        .route("/api/agent/queue/clear", post(api::clear_message_queue))
        .route("/api/context/compact", post(api::compact_context))
        .route("/api/sessions", get(api::list_sessions))
        .route("/api/sessions/create", post(api::create_session))
        .route("/api/sessions/search", post(api::search_sessions))
        .route("/api/sessions/load", post(api::load_session_body))
        .route("/api/sessions/{id}", get(api::get_session))
        .route(
            "/api/sessions/{id}",
            axum::routing::delete(api::delete_session),
        )
        .route("/api/sessions/{id}/rename", post(api::rename_session))
        .route("/api/sessions/{id}/archive", post(api::archive_session))
        .route("/api/sessions/{id}/unarchive", post(api::unarchive_session))
        .route("/api/sessions/{id}/duplicate", post(api::duplicate_session))
        .route(
            "/api/sessions/{id}/messages",
            get(api::get_session_messages),
        )
        .route("/api/sessions/{id}/message", post(api::add_message))
        .route(
            "/api/sessions/{id}/messages/{msg_id}",
            patch(api::update_message),
        )
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
        .route("/api/sessions/delete", post(api::delete_session_body))
        .route("/api/sessions/rename", post(api::rename_session_body))
        .route("/api/mcp/reload", post(api::reload_mcp))
        .route(
            "/api/mcp/servers/{name}/enable",
            post(api::enable_mcp_server),
        )
        .route(
            "/api/mcp/servers/{name}/disable",
            post(api::disable_mcp_server),
        )
        .route(
            "/api/plugins/{plugin}/commands/{command}",
            post(api::invoke_plugin_command),
        )
        .route("/api/plugins/mounts", get(api::list_plugin_mounts))
        .route(
            "/api/plugins/{plugin}/routes/{*route_path}",
            get(api::get_plugin_route),
        )
        .route(
            "/api/plugins/{plugin}/routes/{*route_path}",
            post(api::post_plugin_route),
        )
        .route("/api/cli-agents", get(api::list_cli_agents))
        .route("/api/models/switch", post(api::switch_model))
        .route("/api/config", get(api::get_config))
        .route("/api/plans", get(api_plans::list_plans))
        .route("/api/plans/{filename}", get(api_plans::get_plan))
        .route(
            "/api/permissions",
            get(api::get_permission_level).post(api::set_permission_level),
        )
        .route(
            "/api/permissions/toggle",
            post(api::toggle_permission_level),
        )
        .layer(from_fn_with_state(
            security.clone(),
            security::require_control_plane_http_access,
        ));

    #[cfg(debug_assertions)]
    let protected_routes = protected_routes
        .route(
            "/api/debug/inject-approval",
            post(api::inject_approval_request),
        )
        .route(
            "/api/debug/inject-question",
            post(api::inject_question_request),
        )
        .route("/api/debug/finish-run", post(api::finish_debug_run));

    public_routes
        .merge(protected_routes)
        .route(
            "/ws",
            get(ws::ws_handler).route_layer(from_fn_with_state(
                security.clone(),
                security::require_control_plane_ws_access,
            )),
        )
        .layer(security.cors_layer())
        .layer(TraceLayer::new_for_http())
        .with_state(state)
}

#[cfg(test)]
fn build_router(state: WebState) -> Router {
    build_router_with_security(state, WebSecurityConfig::permissive_for_tests())
}

fn announce_control_token(control_token: &str, generated: bool) {
    let stderr_is_terminal = std::io::stderr().is_terminal();

    if generated && stderr_is_terminal {
        eprintln!("  Control token (shown only on this terminal): {control_token}");
    }

    if generated {
        if stderr_is_terminal {
            info!("  Control token: [generated; shown only on the live terminal]");
        } else {
            info!("  Control token: [generated; redacted from non-terminal logs]");
            info!("  Supply --token <token> for unattended or reconnectable web sessions.");
        }
    } else {
        info!("  Control token: [provided via --token; redacted]");
    }
}

/// Start the AVA web server on the given host and port.
///
/// The TCP listener is bound **before** `AgentStack` construction so that the
/// port is claimed immediately.  MCP servers connect lazily on the first API
/// call (background task, 30 s timeout per server, all in parallel) — the web
/// server never waits for MCP at startup.
pub async fn run_server(
    host: &str,
    port: u16,
    token: Option<String>,
    insecure_open_cors: bool,
) -> Result<()> {
    let data_dir = ava_config::data_dir().unwrap_or_default();

    // Ensure the logs directory exists for frontend log ingestion
    let logs_dir = ava_config::logs_dir().unwrap_or_else(|_| {
        std::env::var("XDG_STATE_HOME")
            .map(PathBuf::from)
            .or_else(|_| std::env::var("HOME").map(|home| PathBuf::from(home).join(".local/state")))
            .unwrap_or_else(|_| std::env::temp_dir())
            .join("ava")
            .join("logs")
    });
    std::fs::create_dir_all(&logs_dir).ok();

    // Bind the port FIRST so the address is claimed before any slow init.
    let addr = format!("{host}:{port}");
    let listener = tokio::net::TcpListener::bind(&addr).await?;

    let token_was_generated = token.is_none();
    let control_token = token.unwrap_or_else(|| Uuid::new_v4().simple().to_string());
    let security = WebSecurityConfig::new(control_token.clone(), insecure_open_cors);

    info!("AVA web server listening on http://{addr}");
    info!("  API:       http://{addr}/api/");
    info!("  WebSocket: ws://{addr}/ws?token=<token> (alias: access_token)");
    info!("  Health:    http://{addr}/api/health");
    announce_control_token(&control_token, token_was_generated);
    info!("  Privileged HTTP routes require Authorization: Bearer <token> (or x-ava-token).");
    if insecure_open_cors {
        info!("  Browser origins: open to any origin (--insecure-open-cors).");
    } else {
        info!("  Browser origins: localhost / 127.0.0.1 / [::1] only.");
    }
    info!("Press Ctrl+C to stop.");
    info!("Initialising agent stack (MCP connects lazily on first use)…");

    // Initialise the agent stack.  `AgentStack::new` spawns codebase indexing
    // as a background task; MCP lazy-init fires on the first run() call.
    // The listener is already bound above, so the port is usable immediately.
    let state = WebState::init(data_dir).await?;
    info!("Agent stack ready — serving requests.");

    let app = build_router_with_security(state, security);

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
    use crate::web::api_agent::{
        emit_promoted_interactive_request_event, spawn_interactive_forwarders,
    };
    use crate::web::security::TOKEN_HEADER;
    use crate::web::state::WebEvent;
    use ava_agent::control_plane::interactive::{
        InteractiveRequestKind, InteractiveRequestStore, InteractiveTimeoutPolicy,
    };
    use ava_agent::stack::{AgentStack, AgentStackConfig};
    use ava_llm::providers::mock::MockProvider;
    use ava_tools::core::{plan::PlanRequest, question::QuestionRequest};
    use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
    use ava_types::PlanDecision;
    use axum::body::{to_bytes, Body};
    use axum::http::header::{
        ACCESS_CONTROL_ALLOW_ORIGIN, CONNECTION, ORIGIN, SEC_WEBSOCKET_KEY, SEC_WEBSOCKET_VERSION,
        UPGRADE,
    };
    use axum::http::{Method, Request, StatusCode};
    use serde_json::json;
    use std::collections::HashMap;
    use std::sync::Arc;
    use std::time::Duration;
    use tokio::sync::{broadcast, mpsc, oneshot, Mutex, RwLock};
    use tower::ServiceExt;

    fn timeout_test_state(base: &WebState, timeout: Duration) -> WebState {
        let (event_tx, _) = broadcast::channel(32);
        let timeout_policy = InteractiveTimeoutPolicy::new(timeout, timeout, timeout);

        WebState {
            inner: Arc::new(crate::web::state::WebStateInner {
                stack: base.inner.stack.clone(),
                db: base.inner.db.clone(),
                startup_lock: Mutex::new(()),
                queue_lifecycle_lock: Mutex::new(()),
                interactive_lifecycle_lock: Arc::new(Mutex::new(())),
                runs: RwLock::new(HashMap::new()),
                session_runs: RwLock::new(HashMap::new()),
                event_tx,
                pending_approval_reply: InteractiveRequestStore::with_timeout_policy(
                    InteractiveRequestKind::Approval,
                    timeout_policy,
                ),
                pending_question_reply: InteractiveRequestStore::with_timeout_policy(
                    InteractiveRequestKind::Question,
                    timeout_policy,
                ),
                pending_plan_reply: InteractiveRequestStore::with_timeout_policy(
                    InteractiveRequestKind::Plan,
                    timeout_policy,
                ),
                deferred_interactive_events: Mutex::new(HashMap::new()),
                last_session_id: RwLock::new(None),
                edit_history: Arc::new(RwLock::new(HashMap::new())),
                deferred_queue: Arc::new(RwLock::new(HashMap::new())),
                in_flight_deferred: Arc::new(RwLock::new(HashMap::new())),
            }),
        }
    }

    async fn register_test_run(state: &WebState, run_id: &str) -> uuid::Uuid {
        let session_id = uuid::Uuid::new_v4();
        state
            .register_run(
                run_id.to_string(),
                session_id,
                "openai".to_string(),
                "gpt-5.4".to_string(),
            )
            .await
            .expect("register test run");
        session_id
    }

    async fn response_json(response: axum::response::Response) -> serde_json::Value {
        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        serde_json::from_slice(&body).expect("json body")
    }

    fn websocket_upgrade_request(uri: &str) -> Request<Body> {
        Request::builder()
            .method(Method::GET)
            .uri(uri)
            .header(CONNECTION, "upgrade")
            .header(UPGRADE, "websocket")
            .header(SEC_WEBSOCKET_VERSION, "13")
            .header(SEC_WEBSOCKET_KEY, "dGhlIHNhbXBsZSBub25jZQ==")
            .body(Body::empty())
            .expect("websocket request")
    }

    async fn mock_run_test_state(data_dir: std::path::PathBuf) -> WebState {
        let db = ava_db::Database::create_at(data_dir.join("ava.db"))
            .await
            .expect("db");
        db.run_migrations().await.expect("migrations");

        let mut config = AgentStackConfig::for_web(data_dir);
        config.provider = Some("openai".to_string());
        config.model = Some("gpt-5.4".to_string());
        config.injected_provider = Some(Arc::new(MockProvider::new(
            "test-model",
            vec!["done".to_string()],
        )));

        let (stack, question_rx, approval_rx, plan_rx) =
            AgentStack::new(config).await.expect("stack");
        let (event_tx, _) = broadcast::channel(256);

        let inner = Arc::new(crate::web::state::WebStateInner {
            stack: Arc::new(stack),
            db: Arc::new(db),
            startup_lock: Mutex::new(()),
            queue_lifecycle_lock: Mutex::new(()),
            interactive_lifecycle_lock: Arc::new(Mutex::new(())),
            runs: RwLock::new(HashMap::new()),
            session_runs: RwLock::new(HashMap::new()),
            event_tx,
            pending_approval_reply: InteractiveRequestStore::new(InteractiveRequestKind::Approval),
            pending_question_reply: InteractiveRequestStore::new(InteractiveRequestKind::Question),
            pending_plan_reply: InteractiveRequestStore::new(InteractiveRequestKind::Plan),
            deferred_interactive_events: Mutex::new(HashMap::new()),
            last_session_id: RwLock::new(None),
            edit_history: Arc::new(RwLock::new(HashMap::new())),
            deferred_queue: Arc::new(RwLock::new(HashMap::new())),
            in_flight_deferred: Arc::new(RwLock::new(HashMap::new())),
        });

        spawn_interactive_forwarders(inner.clone(), approval_rx, question_rx, plan_rx);

        WebState { inner }
    }

    async fn wait_for_no_active_runs(state: &WebState) {
        tokio::time::timeout(Duration::from_secs(1), async {
            loop {
                if state.active_run_count().await == 0 {
                    break;
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        })
        .await
        .expect("run cleanup");
    }

    async fn alternate_replay_run_identity(state: &WebState) -> (String, String) {
        let current = state.inner.stack.current_model().await;
        for (provider, model) in [
            ("openai", "gpt-5.4-nano"),
            ("openai", "gpt-5.4-mini"),
            ("anthropic", "claude-sonnet-4.6"),
        ] {
            if current == (provider.to_string(), model.to_string()) {
                continue;
            }
            if state
                .inner
                .stack
                .router
                .route_required(provider, model)
                .await
                .is_ok()
            {
                return (provider.to_string(), model.to_string());
            }
        }

        panic!("no alternate replay route available for test state");
    }

    #[tokio::test]
    async fn protected_control_routes_require_valid_token() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router_with_security(
            state,
            WebSecurityConfig::new("secret-token".to_string(), false),
        );

        let unauthorized = app
            .clone()
            .oneshot(
                Request::builder()
                    .method(Method::POST)
                    .uri("/api/sessions/create")
                    .header("content-type", "application/json")
                    .body(Body::from(r#"{"name":"Needs Auth"}"#))
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(unauthorized.status(), StatusCode::UNAUTHORIZED);

        let authorized = app
            .clone()
            .oneshot(
                Request::builder()
                    .method(Method::POST)
                    .uri("/api/sessions/create")
                    .header("content-type", "application/json")
                    .header(TOKEN_HEADER, "secret-token")
                    .body(Body::from(r#"{"name":"Authorized"}"#))
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(authorized.status(), StatusCode::OK);

        let body = to_bytes(authorized.into_body(), 8 * 1024)
            .await
            .expect("session body");
        let created: serde_json::Value = serde_json::from_slice(&body).expect("session json");
        let session_id = created["id"].as_str().expect("session id");

        for (method, uri, body) in [
            (Method::GET, "/api/agent/status".to_string(), None),
            (Method::GET, "/api/agent/queue".to_string(), None),
            (Method::GET, "/api/sessions".to_string(), None),
            (Method::GET, "/api/plans".to_string(), None),
            (
                Method::GET,
                "/api/plans/2026-04-19-example-plan.md".to_string(),
                None,
            ),
            (
                Method::POST,
                "/api/sessions/search".to_string(),
                Some(r#"{"query":"Authorized"}"#.to_string()),
            ),
            (Method::GET, "/api/cli-agents".to_string(), None),
            (Method::GET, "/api/plugins/mounts".to_string(), None),
            (
                Method::GET,
                "/api/plugins/example/routes/v1/status".to_string(),
                None,
            ),
            (Method::GET, format!("/api/sessions/{session_id}"), None),
            (
                Method::GET,
                format!("/api/sessions/{session_id}/messages"),
                None,
            ),
            (
                Method::GET,
                format!("/api/sessions/{session_id}/files"),
                None,
            ),
        ] {
            let mut unauthorized = Request::builder().method(method.clone()).uri(&uri);
            if body.is_some() {
                unauthorized = unauthorized.header("content-type", "application/json");
            }
            let unauthorized = app
                .clone()
                .oneshot(
                    unauthorized
                        .body(body.clone().map_or_else(Body::empty, Body::from))
                        .expect("request"),
                )
                .await
                .expect("response");
            assert_eq!(
                unauthorized.status(),
                StatusCode::UNAUTHORIZED,
                "expected {uri} to require auth"
            );

            let mut authorized = Request::builder()
                .method(method)
                .uri(&uri)
                .header(TOKEN_HEADER, "secret-token");
            if body.is_some() {
                authorized = authorized.header("content-type", "application/json");
            }
            let authorized = app
                .clone()
                .oneshot(
                    authorized
                        .body(body.map_or_else(Body::empty, Body::from))
                        .expect("request"),
                )
                .await
                .expect("response");
            assert_ne!(
                authorized.status(),
                StatusCode::UNAUTHORIZED,
                "expected {uri} to accept a valid token"
            );
        }
    }

    #[tokio::test]
    async fn plan_route_rejects_traversal_filename_even_with_valid_token() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router_with_security(
            state,
            WebSecurityConfig::new("secret-token".to_string(), false),
        );

        let response = app
            .oneshot(
                Request::builder()
                    .method(Method::GET)
                    .uri("/api/plans/..%2F..%2FCargo.toml")
                    .header(TOKEN_HEADER, "secret-token")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn protected_control_routes_reject_non_local_browser_origins_by_default() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router_with_security(
            state,
            WebSecurityConfig::new("secret-token".to_string(), false),
        );

        let response = app
            .oneshot(
                Request::builder()
                    .method(Method::POST)
                    .uri("/api/sessions/create")
                    .header("content-type", "application/json")
                    .header(TOKEN_HEADER, "secret-token")
                    .header(ORIGIN, "https://example.com")
                    .body(Body::from(r#"{"name":"Blocked Origin"}"#))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::FORBIDDEN);
    }

    #[tokio::test]
    async fn protected_control_routes_reject_query_token_auth() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router_with_security(
            state,
            WebSecurityConfig::new("secret-token".to_string(), false),
        );

        for uri in [
            "/api/sessions/create?token=secret-token",
            "/api/sessions/create?access_token=secret-token",
        ] {
            let response = app
                .clone()
                .oneshot(
                    Request::builder()
                        .method(Method::POST)
                        .uri(uri)
                        .header("content-type", "application/json")
                        .body(Body::from(r#"{"name":"Should Be Rejected"}"#))
                        .expect("request"),
                )
                .await
                .expect("response");

            assert_eq!(
                response.status(),
                StatusCode::UNAUTHORIZED,
                "expected {uri} to reject query token auth"
            );
        }
    }

    #[tokio::test]
    async fn health_route_emits_cors_header_for_local_origins_only() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router_with_security(
            state,
            WebSecurityConfig::new("secret-token".to_string(), false),
        );

        let allowed = app
            .clone()
            .oneshot(
                Request::builder()
                    .method(Method::GET)
                    .uri("/api/health")
                    .header(ORIGIN, "http://localhost:11420")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(allowed.status(), StatusCode::OK);
        assert_eq!(
            allowed.headers().get(ACCESS_CONTROL_ALLOW_ORIGIN),
            Some(&axum::http::HeaderValue::from_static(
                "http://localhost:11420"
            )),
        );

        let blocked = app
            .oneshot(
                Request::builder()
                    .method(Method::GET)
                    .uri("/api/health")
                    .header(ORIGIN, "https://example.com")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(blocked.status(), StatusCode::OK);
        assert!(blocked.headers().get(ACCESS_CONTROL_ALLOW_ORIGIN).is_none());
    }

    #[tokio::test]
    async fn websocket_handshake_requires_token_and_local_origin() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router_with_security(
            state,
            WebSecurityConfig::new("secret-token".to_string(), false),
        );

        let unauthorized = app
            .clone()
            .oneshot(websocket_upgrade_request("/ws"))
            .await
            .expect("response");
        assert_eq!(unauthorized.status(), StatusCode::UNAUTHORIZED);

        let forbidden_origin = app
            .clone()
            .oneshot(
                Request::builder()
                    .method(Method::GET)
                    .uri("/ws?token=secret-token")
                    .header(CONNECTION, "upgrade")
                    .header(UPGRADE, "websocket")
                    .header(SEC_WEBSOCKET_VERSION, "13")
                    .header(SEC_WEBSOCKET_KEY, "dGhlIHNhbXBsZSBub25jZQ==")
                    .header(ORIGIN, "https://example.com")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(forbidden_origin.status(), StatusCode::FORBIDDEN);

        let authorized = app
            .clone()
            .oneshot(
                Request::builder()
                    .method(Method::GET)
                    .uri("/ws?token=secret-token")
                    .header(CONNECTION, "upgrade")
                    .header(UPGRADE, "websocket")
                    .header(SEC_WEBSOCKET_VERSION, "13")
                    .header(SEC_WEBSOCKET_KEY, "dGhlIHNhbXBsZSBub25jZQ==")
                    .header(ORIGIN, "http://127.0.0.1:1490")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(authorized.status(), StatusCode::SWITCHING_PROTOCOLS);

        let alias_authorized = app
            .clone()
            .oneshot(
                Request::builder()
                    .method(Method::GET)
                    .uri("/ws?access_token=secret-token")
                    .header(CONNECTION, "upgrade")
                    .header(UPGRADE, "websocket")
                    .header(SEC_WEBSOCKET_VERSION, "13")
                    .header(SEC_WEBSOCKET_KEY, "dGhlIHNhbXBsZSBub25jZQ==")
                    .header(ORIGIN, "http://127.0.0.1:1490")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(alias_authorized.status(), StatusCode::SWITCHING_PROTOCOLS);
    }

    fn replay_run_context_metadata(provider: &str, model: &str) -> serde_json::Value {
        json!({
            "runContext": {
                "provider": provider,
                "model": model,
                "thinkingLevel": "high",
                "autoCompact": false,
                "compactionThreshold": 72,
                "compactionProvider": provider,
                "compactionModel": model
            }
        })
    }

    #[tokio::test]
    async fn session_create_and_list_round_trip_project_id() {
        let temp = tempfile::tempdir().expect("tempdir");
        let session_id = uuid::Uuid::new_v4();
        let other_session_id = uuid::Uuid::new_v4();
        let project_id = "project-web-1";

        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router(state.clone());

        let created = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("POST")
                        .uri("/api/sessions/create")
                        .header("content-type", "application/json")
                        .body(Body::from(format!(
                            r#"{{"id":"{session_id}","name":"Project Session","project_id":"{project_id}"}}"#
                        )))
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;

        assert_eq!(
            created["project_id"],
            serde_json::Value::String(project_id.to_string())
        );

        let other_created = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/sessions/create")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"id":"{other_session_id}","name":"Other Project Session","project_id":"project-web-2"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(other_created.status(), StatusCode::OK);

        let listed = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri("/api/sessions?project_id=project-web-1")
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;

        let listed = listed.as_array().expect("session list array");
        assert!(listed
            .iter()
            .all(|session| session["id"] != other_session_id.to_string()));
        let listed = listed
            .iter()
            .find(|session| session["id"] == session_id.to_string())
            .expect("created session in list");
        assert_eq!(
            listed["project_id"],
            serde_json::Value::String(project_id.to_string())
        );
    }

    #[tokio::test]
    async fn session_archive_state_persists_across_reload_and_list_filters() {
        let temp = tempfile::tempdir().expect("tempdir");
        let session_id = uuid::Uuid::new_v4();

        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router(state.clone());

        let response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/sessions/create")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"id":"{session_id}","name":"Archive Me"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(response.status(), StatusCode::OK);

        let response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri(&format!("/api/sessions/{session_id}/archive"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(response.status(), StatusCode::OK);

        let active_sessions = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri("/api/sessions")
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        assert!(active_sessions
            .as_array()
            .expect("active sessions array")
            .iter()
            .all(|session| session["id"] != session_id.to_string()));

        let archived_sessions = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri("/api/sessions?status=archived")
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        let archived = archived_sessions
            .as_array()
            .expect("archived sessions array")
            .iter()
            .find(|session| session["id"] == session_id.to_string())
            .expect("archived session entry");
        assert_eq!(
            archived["status"],
            serde_json::Value::String("archived".to_string())
        );

        drop(app);
        drop(state);

        let reloaded_state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("reloaded web state");
        let reloaded_app = build_router(reloaded_state.clone());

        let archived_after_reload = response_json(
            reloaded_app
                .clone()
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri("/api/sessions?status=archived")
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        let archived_after_reload = archived_after_reload
            .as_array()
            .expect("archived reload array")
            .iter()
            .find(|session| session["id"] == session_id.to_string())
            .expect("archived session after reload");
        assert_eq!(
            archived_after_reload["status"],
            serde_json::Value::String("archived".to_string())
        );

        let archived_detail = response_json(
            reloaded_app
                .clone()
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri(&format!("/api/sessions/{session_id}"))
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        assert_eq!(
            archived_detail["status"],
            serde_json::Value::String("archived".to_string())
        );

        let response = reloaded_app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri(&format!("/api/sessions/{session_id}/unarchive"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(response.status(), StatusCode::OK);

        let active_after_unarchive = response_json(
            reloaded_app
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri("/api/sessions")
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        let restored = active_after_unarchive
            .as_array()
            .expect("active sessions after unarchive")
            .iter()
            .find(|session| session["id"] == session_id.to_string())
            .expect("restored active session");
        assert_eq!(
            restored["status"],
            serde_json::Value::String("active".to_string())
        );
    }

    #[tokio::test]
    async fn fork_session_persists_parent_linkage_across_reload_and_refetch() {
        let temp = tempfile::tempdir().expect("tempdir");
        let source_id = uuid::Uuid::new_v4();
        let fork_id = uuid::Uuid::new_v4();

        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let app = build_router(state.clone());

        let response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/sessions/create")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"id":"{source_id}","name":"Source Session"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(response.status(), StatusCode::OK);

        let response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri(&format!("/api/sessions/{source_id}/message"))
                    .header("content-type", "application/json")
                    .body(Body::from(
                        r#"{"role":"user","content":"Parent conversation message"}"#,
                    ))
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(response.status(), StatusCode::OK);

        let mut hidden_tool_message = ava_types::Message::new(
            ava_types::Role::Tool,
            "hidden tool payload that should persist in cloned session",
        );
        hidden_tool_message.user_visible = false;
        state
            .inner
            .stack
            .session_manager
            .add_message(source_id, &hidden_tool_message)
            .expect("hidden tool message added to source session");

        let fork_response = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("POST")
                        .uri(&format!("/api/sessions/{source_id}/duplicate"))
                        .header("content-type", "application/json")
                        .body(Body::from(format!(
                            r#"{{"id":"{fork_id}","name":"Fork Session","kind":"fork"}}"#
                        )))
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;

        assert_eq!(
            fork_response["parent_session_id"],
            serde_json::Value::String(source_id.to_string())
        );
        assert_eq!(
            fork_response["last_preview"],
            serde_json::Value::String("Parent conversation message".to_string())
        );
        assert_eq!(fork_response["message_count"], serde_json::Value::from(1));

        drop(app);
        drop(state);

        let reloaded_state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("reloaded web state");
        let reloaded_app = build_router(reloaded_state.clone());

        let listed_sessions = response_json(
            reloaded_app
                .clone()
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri("/api/sessions")
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        let listed_fork = listed_sessions
            .as_array()
            .expect("listed sessions array")
            .iter()
            .find(|session| session["id"] == fork_id.to_string())
            .expect("fork session listed after reload");
        assert_eq!(
            listed_fork["parent_session_id"],
            serde_json::Value::String(source_id.to_string())
        );
        assert_eq!(
            listed_fork["last_preview"],
            serde_json::Value::String("Parent conversation message".to_string())
        );

        let fork_detail = response_json(
            reloaded_app
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri(&format!("/api/sessions/{fork_id}"))
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        assert_eq!(
            fork_detail["parent_session_id"],
            serde_json::Value::String(source_id.to_string())
        );
        assert_eq!(
            fork_detail["last_preview"],
            serde_json::Value::String("Parent conversation message".to_string())
        );
        assert_eq!(fork_detail["message_count"], serde_json::Value::from(1));

        let persisted_fork = reloaded_state
            .inner
            .stack
            .session_manager
            .get(fork_id)
            .expect("load persisted fork session")
            .expect("fork session persisted after reload");
        assert_eq!(persisted_fork.messages.len(), 2);
        assert!(persisted_fork.messages.iter().any(|message| {
            message.role == ava_types::Role::Tool
                && !message.user_visible
                && message.content == "hidden tool payload that should persist in cloned session"
        }));
    }

    #[tokio::test]
    async fn duplicate_and_fork_preserve_replay_metadata_while_overriding_clone_fields() {
        let temp = tempfile::tempdir().expect("tempdir");
        let source_id = uuid::Uuid::new_v4();
        let duplicate_id = uuid::Uuid::new_v4();
        let fork_id = uuid::Uuid::new_v4();
        let root_message_id = uuid::Uuid::new_v4();
        let child_message_id = uuid::Uuid::new_v4();

        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let source_metadata = json!({
            "title": "Source Session",
            "status": "archived",
            "runContext": {
                "provider": "test-provider",
                "model": "test-model",
                "thinkingLevel": "high",
                "autoCompact": false,
                "compactionThreshold": 72,
                "compactionProvider": "test-provider",
                "compactionModel": "test-model"
            },
            "routing": {
                "provider": "fallback-provider",
                "model": "fallback-model"
            },
            "customKey": "preserve-me",
            "parentSessionId": "stale-parent"
        });
        let mut root_message = ava_types::Message::new(ava_types::Role::User, "root message");
        root_message.id = root_message_id;
        let mut child_message =
            ava_types::Message::new(ava_types::Role::Assistant, "child message")
                .with_parent(root_message_id)
                .with_metadata(json!({ "parentId": root_message_id.to_string() }));
        child_message.id = child_message_id;
        child_message.metadata["parent_id"] =
            serde_json::Value::String(root_message_id.to_string());

        let mut source_session = ava_types::Session::new()
            .with_id(source_id)
            .with_metadata(source_metadata.clone());
        source_session.add_message(root_message);
        source_session.add_message(child_message);
        state
            .inner
            .stack
            .session_manager
            .save(&source_session)
            .expect("save source session");

        let app = build_router(state.clone());

        let duplicate_response = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("POST")
                        .uri(&format!("/api/sessions/{source_id}/duplicate"))
                        .header("content-type", "application/json")
                        .body(Body::from(format!(
                            r#"{{"id":"{duplicate_id}","name":"Duplicate Session","kind":"duplicate"}}"#
                        )))
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        assert_eq!(
            duplicate_response["title"],
            serde_json::Value::String("Duplicate Session".to_string())
        );
        assert_eq!(
            duplicate_response["parent_session_id"],
            serde_json::Value::Null
        );

        let persisted_duplicate = state
            .inner
            .stack
            .session_manager
            .get(duplicate_id)
            .expect("load duplicate session")
            .expect("duplicate session persisted");
        assert_eq!(
            persisted_duplicate.metadata["runContext"],
            source_metadata["runContext"]
        );
        assert_eq!(
            persisted_duplicate.metadata["routing"],
            source_metadata["routing"]
        );
        assert_eq!(
            persisted_duplicate.metadata["customKey"],
            source_metadata["customKey"]
        );
        assert_eq!(
            persisted_duplicate.metadata["title"],
            serde_json::Value::String("Duplicate Session".to_string())
        );
        assert_eq!(
            persisted_duplicate.metadata["status"],
            serde_json::Value::String("active".to_string())
        );
        assert!(persisted_duplicate
            .metadata
            .get("parentSessionId")
            .is_none());
        assert!(persisted_duplicate
            .metadata
            .get("parent_session_id")
            .is_none());
        let duplicated_child = persisted_duplicate
            .messages
            .iter()
            .find(|message| message.content == "child message")
            .expect("duplicated child message persisted");
        let duplicated_root = persisted_duplicate
            .messages
            .iter()
            .find(|message| message.content == "root message")
            .expect("duplicated root message persisted");
        assert_eq!(duplicated_child.parent_id, Some(duplicated_root.id));
        assert_eq!(
            duplicated_child.metadata["parentId"],
            serde_json::Value::String(duplicated_root.id.to_string())
        );
        assert_eq!(
            duplicated_child.metadata["parent_id"],
            serde_json::Value::String(duplicated_root.id.to_string())
        );

        let fork_response = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("POST")
                        .uri(&format!("/api/sessions/{source_id}/duplicate"))
                        .header("content-type", "application/json")
                        .body(Body::from(format!(
                            r#"{{"id":"{fork_id}","name":"Fork Session","kind":"fork"}}"#
                        )))
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        assert_eq!(
            fork_response["title"],
            serde_json::Value::String("Fork Session".to_string())
        );
        assert_eq!(
            fork_response["parent_session_id"],
            serde_json::Value::String(source_id.to_string())
        );

        let persisted_fork = state
            .inner
            .stack
            .session_manager
            .get(fork_id)
            .expect("load fork session")
            .expect("fork session persisted");
        assert_eq!(
            persisted_fork.metadata["runContext"],
            source_metadata["runContext"]
        );
        assert_eq!(
            persisted_fork.metadata["routing"],
            source_metadata["routing"]
        );
        assert_eq!(
            persisted_fork.metadata["customKey"],
            source_metadata["customKey"]
        );
        assert_eq!(
            persisted_fork.metadata["title"],
            serde_json::Value::String("Fork Session".to_string())
        );
        assert_eq!(
            persisted_fork.metadata["status"],
            serde_json::Value::String("active".to_string())
        );
        assert_eq!(
            persisted_fork.metadata["parentSessionId"],
            serde_json::Value::String(source_id.to_string())
        );
        let forked_child = persisted_fork
            .messages
            .iter()
            .find(|message| message.content == "child message")
            .expect("forked child message persisted");
        let forked_root = persisted_fork
            .messages
            .iter()
            .find(|message| message.content == "root message")
            .expect("forked root message persisted");
        assert_eq!(forked_child.parent_id, Some(forked_root.id));
        assert_eq!(
            forked_child.metadata["parentId"],
            serde_json::Value::String(forked_root.id.to_string())
        );
        assert_eq!(
            forked_child.metadata["parent_id"],
            serde_json::Value::String(forked_root.id.to_string())
        );
    }

    #[tokio::test]
    async fn session_message_endpoints_round_trip_images() {
        let temp = tempfile::tempdir().expect("tempdir");
        let session_id = uuid::Uuid::new_v4();
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut session = ava_types::Session::new().with_id(session_id);
        session.add_message(
            ava_types::Message::new(ava_types::Role::User, "describe this").with_images(vec![
                ava_types::ImageContent::new("base64-image", ava_types::ImageMediaType::Png),
            ]),
        );
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .expect("save source session");

        let app = build_router(state);

        let detail = response_json(
            app.clone()
                .oneshot(
                    Request::builder()
                        .method("GET")
                        .uri(format!("/api/sessions/{session_id}"))
                        .body(Body::empty())
                        .expect("request"),
                )
                .await
                .expect("response"),
        )
        .await;
        assert_eq!(
            detail["messages"][0]["images"],
            json!([{ "data": "base64-image", "media_type": "image/png" }])
        );

        let messages = response_json(
            app.oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/api/sessions/{session_id}/messages"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response"),
        )
        .await;
        assert_eq!(
            messages[0]["images"],
            json!([{ "data": "base64-image", "media_type": "image/png" }])
        );
    }

    #[tokio::test]
    async fn agent_status_scoped_to_session_ignores_other_active_runs() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let active_session = register_test_run(&state, "web-run-status-a").await;
        let inactive_session = uuid::Uuid::new_v4();
        register_test_run(&state, "web-run-status-b").await;

        let app = build_router(state.clone());

        let active_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/api/agent/status?session_id={active_session}"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(active_response.status(), StatusCode::OK);
        let active_body = to_bytes(active_response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let active_json: serde_json::Value =
            serde_json::from_slice(&active_body).expect("active status json");
        assert_eq!(
            active_json.get("running"),
            Some(&serde_json::Value::Bool(true))
        );
        assert_eq!(
            active_json.get("runId").and_then(|value| value.as_str()),
            Some("web-run-status-a")
        );

        let inactive_response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/api/agent/status?session_id={inactive_session}"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(inactive_response.status(), StatusCode::OK);
        let inactive_body = to_bytes(inactive_response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let inactive_json: serde_json::Value =
            serde_json::from_slice(&inactive_body).expect("inactive status json");
        assert_eq!(
            inactive_json.get("running"),
            Some(&serde_json::Value::Bool(false))
        );
        assert!(inactive_json.get("runId").is_none());
    }

    #[tokio::test]
    async fn agent_status_uses_active_run_provider_and_model() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let session_id = uuid::Uuid::new_v4();
        state
            .register_run(
                "web-run-provider-model".to_string(),
                session_id,
                "anthropic".to_string(),
                "claude-sonnet-4.6".to_string(),
            )
            .await
            .expect("register run");

        let app = build_router(state.clone());
        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/api/agent/status?run_id=web-run-provider-model")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let json: serde_json::Value = serde_json::from_slice(&body).expect("status json");
        assert_eq!(
            json.get("provider").and_then(|value| value.as_str()),
            Some("anthropic")
        );
        assert_eq!(
            json.get("model").and_then(|value| value.as_str()),
            Some("claude-sonnet-4.6")
        );
    }

    #[tokio::test]
    async fn agent_status_only_exposes_same_kind_prompt_when_run_is_globally_actionable() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        register_test_run(&state, "web-run-a").await;
        register_test_run(&state, "web-run-b").await;

        let (run_a_tx, _run_a_rx) = oneshot::channel::<String>();
        let run_a = state
            .inner
            .pending_question_reply
            .register_with_run_id(run_a_tx, Some("web-run-a".to_string()))
            .await;
        let (run_b_tx, _run_b_rx) = oneshot::channel::<String>();
        let run_b = state
            .inner
            .pending_question_reply
            .register_with_run_id(run_b_tx, Some("web-run-b".to_string()))
            .await;

        state.inner.deferred_interactive_events.lock().await.insert(
            run_a.request_id.clone(),
            WebEvent::QuestionRequest {
                id: run_a.request_id.clone(),
                question: "Question A?".to_string(),
                options: vec![],
                run_id: run_a.run_id.clone(),
            },
        );
        state.inner.deferred_interactive_events.lock().await.insert(
            run_b.request_id.clone(),
            WebEvent::QuestionRequest {
                id: run_b.request_id.clone(),
                question: "Question B?".to_string(),
                options: vec![],
                run_id: run_b.run_id.clone(),
            },
        );

        let app = build_router(state.clone());

        let run_a_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/api/agent/status?run_id=web-run-a")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        let run_a_body = to_bytes(run_a_response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let run_a_json: serde_json::Value =
            serde_json::from_slice(&run_a_body).expect("run A status json");
        assert_eq!(
            run_a_json
                .get("pendingQuestion")
                .and_then(|value| value.get("runId"))
                .and_then(|value| value.as_str()),
            Some("web-run-a")
        );

        let run_b_hidden_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/api/agent/status?run_id=web-run-b")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        let run_b_hidden_body = to_bytes(run_b_hidden_response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let run_b_hidden_json: serde_json::Value =
            serde_json::from_slice(&run_b_hidden_body).expect("run B hidden status json");
        assert!(run_b_hidden_json.get("pendingQuestion").is_none());

        let _ = state
            .inner
            .pending_question_reply
            .resolve(Some(&run_a.request_id))
            .await
            .expect("run A request should resolve");

        let run_b_visible_response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/api/agent/status?run_id=web-run-b")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        let run_b_visible_body = to_bytes(run_b_visible_response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let run_b_visible_json: serde_json::Value =
            serde_json::from_slice(&run_b_visible_body).expect("run B visible status json");
        assert_eq!(
            run_b_visible_json
                .get("pendingQuestion")
                .and_then(|value| value.get("runId"))
                .and_then(|value| value.as_str()),
            Some("web-run-b")
        );
    }

    #[tokio::test]
    async fn unrelated_web_question_requests_stay_hidden_until_globally_actionable() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        register_test_run(&state, "web-run-a").await;
        register_test_run(&state, "web-run-b").await;
        let mut events = state.inner.event_tx.subscribe();

        let (question_tx, question_rx) = mpsc::unbounded_channel();
        let (_, empty_approval_rx) = mpsc::unbounded_channel();
        let (_, empty_plan_rx) = mpsc::unbounded_channel();

        let (approval_forwarder, question_forwarder, plan_forwarder) = spawn_interactive_forwarders(
            state.inner.clone(),
            empty_approval_rx,
            question_rx,
            empty_plan_rx,
        );

        let (reply_a_tx, reply_a_rx) = oneshot::channel::<String>();
        let (reply_b_tx, reply_b_rx) = oneshot::channel::<String>();
        question_tx
            .send(QuestionRequest {
                run_id: Some("web-run-a".to_string()),
                question: "Question A?".to_string(),
                options: vec!["Yes".to_string(), "No".to_string()],
                reply: reply_a_tx,
            })
            .expect("question A should enqueue");
        question_tx
            .send(QuestionRequest {
                run_id: Some("web-run-b".to_string()),
                question: "Question B?".to_string(),
                options: vec!["Yes".to_string(), "No".to_string()],
                reply: reply_b_tx,
            })
            .expect("question B should enqueue");
        drop(question_tx);

        let request_a = match events.recv().await.expect("question A event") {
            WebEvent::QuestionRequest { id, run_id, .. } => {
                assert_eq!(run_id.as_deref(), Some("web-run-a"));
                id
            }
            other => panic!("expected question_request event, got {other:?}"),
        };

        assert!(
            tokio::time::timeout(Duration::from_millis(20), events.recv())
                .await
                .is_err(),
            "same-kind request for another run should stay hidden until globally actionable"
        );

        let request_b = state
            .inner
            .pending_question_reply
            .current_request_id_for_run(Some("web-run-b"))
            .await
            .expect("run B request id");

        let app = build_router(state.clone());
        let resolve_b_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-question")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{request_b}","answer":"beta"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(resolve_b_response.status(), StatusCode::OK);
        assert_eq!(reply_b_rx.await.expect("question B answer"), "beta");
        assert_eq!(
            state
                .inner
                .pending_question_reply
                .current_request_id_for_run(Some("web-run-a"))
                .await,
            Some(request_a.clone())
        );

        let resolve_a_response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-question")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{request_a}","answer":"alpha"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(resolve_a_response.status(), StatusCode::OK);
        assert_eq!(reply_a_rx.await.expect("question A answer"), "alpha");

        approval_forwarder.abort();
        question_forwarder.abort();
        plan_forwarder.abort();
    }

    #[tokio::test]
    async fn resolve_plan_route_requires_request_id_and_preserves_pending_state() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let (tx, rx) = oneshot::channel();
        let request = state.inner.pending_plan_reply.register(tx).await;

        let retry_state = state.clone();
        let app = build_router(state.clone());

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
                run_id,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "plan");
                assert!(!timed_out);
                assert_eq!(run_id, None);
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
        state.inner.deferred_interactive_events.lock().await.insert(
            request.request_id.clone(),
            WebEvent::ApprovalRequest {
                id: request.request_id.clone(),
                tool_call_id: "tool-1".to_string(),
                tool_name: "bash".to_string(),
                args: serde_json::json!({ "command": "pwd" }),
                risk_level: "low".to_string(),
                reason: "Need approval".to_string(),
                warnings: vec![],
                run_id: None,
            },
        );

        let app = build_router(state.clone());
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
                run_id,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "approval");
                assert!(!timed_out);
                assert_eq!(run_id, None);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
        assert!(state
            .inner
            .deferred_interactive_events
            .lock()
            .await
            .get(&request.request_id)
            .is_none());
    }

    #[cfg(debug_assertions)]
    #[tokio::test]
    async fn debug_injected_approval_creates_correlated_run_and_cleans_up_after_resolution() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();
        let session_id = uuid::Uuid::new_v4();
        let run_id = "debug-run-approval";

        let app = build_router(state.clone());
        let inject_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/debug/inject-approval")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"session_id":"{}","run_id":"{}","tool_name":"bash","args":{{"command":"pwd"}}}}"#,
                        session_id, run_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(inject_response.status(), StatusCode::OK);
        let inject_body = to_bytes(inject_response.into_body(), 8 * 1024)
            .await
            .expect("inject body");
        let inject_payload: serde_json::Value =
            serde_json::from_slice(&inject_body).expect("inject payload");
        let request_id = inject_payload["requestId"]
            .as_str()
            .expect("requestId")
            .to_string();

        match events.recv().await.expect("approval event") {
            WebEvent::ApprovalRequest {
                id,
                run_id: event_run_id,
                ..
            } => {
                assert_eq!(id, request_id);
                assert_eq!(event_run_id.as_deref(), Some(run_id));
            }
            other => panic!("expected approval_request event, got {other:?}"),
        }

        let status_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/api/agent/status?session_id={session_id}"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(status_response.status(), StatusCode::OK);
        let status_body = to_bytes(status_response.into_body(), 8 * 1024)
            .await
            .expect("status body");
        let status_payload: serde_json::Value =
            serde_json::from_slice(&status_body).expect("status payload");
        assert_eq!(status_payload["running"], serde_json::json!(true));
        assert_eq!(status_payload["runId"], serde_json::json!(run_id));
        assert_eq!(
            status_payload["pendingApproval"]["id"],
            serde_json::json!(request_id)
        );

        let resolve_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-approval")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","approved":false}}"#,
                        request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(resolve_response.status(), StatusCode::OK);

        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id: cleared_request_id,
                request_kind,
                timed_out,
                run_id: cleared_run_id,
            } => {
                assert_eq!(cleared_request_id, request_id);
                assert_eq!(request_kind, "approval");
                assert!(!timed_out);
                assert_eq!(cleared_run_id.as_deref(), Some(run_id));
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }

        tokio::time::timeout(Duration::from_secs(1), async {
            loop {
                if state.active_run_count().await == 0 {
                    break;
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        })
        .await
        .expect("synthetic run cleanup");
    }

    #[cfg(debug_assertions)]
    #[tokio::test]
    async fn debug_injected_question_creates_correlated_run_and_cleans_up_after_resolution() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();
        let session_id = uuid::Uuid::new_v4();
        let run_id = "debug-run-question";

        let app = build_router(state.clone());
        let inject_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/debug/inject-question")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"session_id":"{}","run_id":"{}","question":"Continue?","options":["yes","no"]}}"#,
                        session_id, run_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(inject_response.status(), StatusCode::OK);
        let inject_body = to_bytes(inject_response.into_body(), 8 * 1024)
            .await
            .expect("inject body");
        let inject_payload: serde_json::Value =
            serde_json::from_slice(&inject_body).expect("inject payload");
        let request_id = inject_payload["requestId"]
            .as_str()
            .expect("requestId")
            .to_string();

        match events.recv().await.expect("question event") {
            WebEvent::QuestionRequest {
                id,
                question,
                options,
                run_id: event_run_id,
            } => {
                assert_eq!(id, request_id);
                assert_eq!(question, "Continue?");
                assert_eq!(options, vec!["yes", "no"]);
                assert_eq!(event_run_id.as_deref(), Some(run_id));
            }
            other => panic!("expected question_request event, got {other:?}"),
        }

        let status_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(format!("/api/agent/status?session_id={session_id}"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(status_response.status(), StatusCode::OK);
        let status_body = to_bytes(status_response.into_body(), 8 * 1024)
            .await
            .expect("status body");
        let status_payload: serde_json::Value =
            serde_json::from_slice(&status_body).expect("status payload");
        assert_eq!(status_payload["running"], serde_json::json!(true));
        assert_eq!(status_payload["runId"], serde_json::json!(run_id));
        assert_eq!(
            status_payload["pendingQuestion"]["id"],
            serde_json::json!(request_id)
        );
        assert_eq!(
            status_payload["pendingQuestion"]["question"],
            serde_json::json!("Continue?")
        );

        let resolve_response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/resolve-question")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"request_id":"{}","answer":"yes"}}"#,
                        request_id
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(resolve_response.status(), StatusCode::OK);

        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id: cleared_request_id,
                request_kind,
                timed_out,
                run_id: cleared_run_id,
            } => {
                assert_eq!(cleared_request_id, request_id);
                assert_eq!(request_kind, "question");
                assert!(!timed_out);
                assert_eq!(cleared_run_id.as_deref(), Some(run_id));
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }

        tokio::time::timeout(Duration::from_secs(1), async {
            loop {
                if state.active_run_count().await == 0 {
                    break;
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        })
        .await
        .expect("synthetic run cleanup");
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
                run_id,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "question");
                assert!(!timed_out);
                assert_eq!(run_id, None);
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
                run_id,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "plan");
                assert!(!timed_out);
                assert_eq!(run_id, None);
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
        register_test_run(&state, "web-run-cancel").await;

        let (tx, rx) = oneshot::channel();
        let request = state
            .inner
            .pending_approval_reply
            .register_with_run_id(tx, Some("web-run-cancel".to_string()))
            .await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/cancel")
                    .header("content-type", "application/json")
                    .body(Body::from(r#"{"run_id":"web-run-cancel"}"#))
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
                run_id,
            } => {
                assert_eq!(request_id, request.request_id);
                assert_eq!(request_kind, "approval");
                assert!(!timed_out);
                assert_eq!(run_id.as_deref(), Some("web-run-cancel"));
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn cancel_route_only_clears_target_run_requests() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let mut events = state.inner.event_tx.subscribe();
        register_test_run(&state, "web-run-a").await;
        register_test_run(&state, "web-run-b").await;

        let (first_tx, first_rx) = oneshot::channel();
        let first_request = state
            .inner
            .pending_approval_reply
            .register_with_run_id(first_tx, Some("web-run-a".to_string()))
            .await;
        let (second_tx, second_rx) = oneshot::channel::<ToolApproval>();
        let _second_request = state
            .inner
            .pending_approval_reply
            .register_with_run_id(second_tx, Some("web-run-b".to_string()))
            .await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/cancel")
                    .header("content-type", "application/json")
                    .body(Body::from(r#"{"run_id":"web-run-a"}"#))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        assert!(matches!(
            first_rx.await.expect("first reply"),
            ToolApproval::Rejected(_)
        ));
        assert!(tokio::time::timeout(Duration::from_millis(20), second_rx)
            .await
            .is_err());

        match events.recv().await.expect("clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                run_id,
                ..
            } => {
                assert_eq!(request_id, first_request.request_id);
                assert_eq!(request_kind, "approval");
                assert_eq!(run_id.as_deref(), Some("web-run-a"));
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }
        assert!(
            tokio::time::timeout(Duration::from_millis(20), events.recv())
                .await
                .is_err()
        );
    }

    #[tokio::test]
    async fn status_route_keeps_aggregate_running_but_requires_target_for_correlation() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let session_a = register_test_run(&state, "web-run-a").await;
        register_test_run(&state, "web-run-b").await;

        let app = build_router(state.clone());
        let response = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri("/api/agent/status")
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let payload: serde_json::Value = serde_json::from_slice(&body).expect("status payload");
        assert_eq!(payload["running"], serde_json::Value::Bool(true));
        assert!(payload.get("runId").is_none());

        let response = app
            .oneshot(
                Request::builder()
                    .method("GET")
                    .uri(&format!("/api/agent/status?session_id={session_a}"))
                    .body(Body::empty())
                    .expect("request"),
            )
            .await
            .expect("response");
        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let payload: serde_json::Value = serde_json::from_slice(&body).expect("status payload");
        assert_eq!(
            payload["runId"],
            serde_json::Value::String("web-run-a".to_string())
        );
    }

    #[tokio::test]
    async fn steer_route_targets_requested_run_queue_only() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let _session_a = register_test_run(&state, "web-run-a").await;
        let session_b = register_test_run(&state, "web-run-b").await;

        let (mut queue_a, tx_a, control_a) = state.inner.stack.create_message_queue_with_control();
        let (mut queue_b, tx_b, control_b) = state.inner.stack.create_message_queue_with_control();
        state
            .activate_message_queue("web-run-a", tx_a, control_a)
            .await
            .expect("activate run a queue");
        state
            .activate_message_queue("web-run-b", tx_b, control_b)
            .await
            .expect("activate run b queue");

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/steer")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"message":"only-b","run_id":"web-run-b","session_id":"{session_b}"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        queue_a.poll();
        queue_b.poll();
        assert!(
            queue_a.drain_steering().is_empty(),
            "run A queue should stay untouched"
        );
        assert_eq!(queue_b.drain_steering(), vec!["only-b".to_string()]);
    }

    #[tokio::test]
    async fn websocket_question_timeout_emits_correlated_clear_event_with_timed_out_true() {
        let temp = tempfile::tempdir().expect("tempdir");
        let base_state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let state = timeout_test_state(&base_state, Duration::from_millis(5));
        let mut events = state.inner.event_tx.subscribe();
        register_test_run(&state, "web-run-timeout").await;

        let (question_tx, question_rx) = mpsc::unbounded_channel();
        let (_, empty_approval_rx) = mpsc::unbounded_channel();
        let (_, empty_plan_rx) = mpsc::unbounded_channel();

        let (approval_forwarder, question_forwarder, plan_forwarder) = spawn_interactive_forwarders(
            state.inner.clone(),
            empty_approval_rx,
            question_rx,
            empty_plan_rx,
        );

        let (_reply_tx, reply_rx) = oneshot::channel::<String>();
        question_tx
            .send(QuestionRequest {
                run_id: Some("web-run-timeout".to_string()),
                question: "Continue?".to_string(),
                options: vec!["Yes".to_string(), "No".to_string()],
                reply: _reply_tx,
            })
            .expect("question request should enqueue");
        drop(question_tx);

        match events.recv().await.expect("question request event") {
            WebEvent::QuestionRequest { run_id, .. } => {
                assert_eq!(run_id.as_deref(), Some("web-run-timeout"));
            }
            other => panic!("expected question_request event, got {other:?}"),
        }

        match events.recv().await.expect("timeout clear event") {
            WebEvent::InteractiveRequestCleared {
                request_kind,
                timed_out,
                run_id,
                ..
            } => {
                assert_eq!(request_kind, "question");
                assert!(timed_out);
                assert_eq!(run_id.as_deref(), Some("web-run-timeout"));
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }

        assert_eq!(reply_rx.await.expect("timeout answer"), "");

        approval_forwarder.abort();
        question_forwarder.abort();
        plan_forwarder.abort();
    }

    #[tokio::test]
    async fn queued_same_kind_web_question_timeout_starts_after_promotion() {
        let temp = tempfile::tempdir().expect("tempdir");
        let base_state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let state = timeout_test_state(&base_state, Duration::from_millis(20));
        let mut events = state.inner.event_tx.subscribe();
        register_test_run(&state, "web-run-queued-timeout").await;

        let (question_tx, question_rx) = mpsc::unbounded_channel();
        let (_, empty_approval_rx) = mpsc::unbounded_channel();
        let (_, empty_plan_rx) = mpsc::unbounded_channel();

        let (approval_forwarder, question_forwarder, plan_forwarder) = spawn_interactive_forwarders(
            state.inner.clone(),
            empty_approval_rx,
            question_rx,
            empty_plan_rx,
        );

        let (first_reply_tx, first_reply_rx) = oneshot::channel::<String>();
        let (second_reply_tx, second_reply_rx) = oneshot::channel::<String>();
        question_tx
            .send(QuestionRequest {
                run_id: Some("web-run-queued-timeout".to_string()),
                question: "First queued question?".to_string(),
                options: vec!["Yes".to_string(), "No".to_string()],
                reply: first_reply_tx,
            })
            .expect("first question should enqueue");
        question_tx
            .send(QuestionRequest {
                run_id: Some("web-run-queued-timeout".to_string()),
                question: "Second queued question?".to_string(),
                options: vec!["Yes".to_string(), "No".to_string()],
                reply: second_reply_tx,
            })
            .expect("second question should enqueue");
        drop(question_tx);

        let first_request_id = match events.recv().await.expect("question request event") {
            WebEvent::QuestionRequest { id, .. } => id,
            other => panic!("expected question_request event, got {other:?}"),
        };

        assert!(
            tokio::time::timeout(Duration::from_millis(8), events.recv())
                .await
                .is_err(),
            "same-kind queued question should remain hidden until front request clears"
        );

        match events.recv().await.expect("first timeout clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
                ..
            } => {
                assert_eq!(request_id, first_request_id);
                assert_eq!(request_kind, "question");
                assert!(timed_out);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }

        let second_request_id = match events.recv().await.expect("promoted question request") {
            WebEvent::QuestionRequest { id, .. } => id,
            other => panic!("expected question_request event, got {other:?}"),
        };

        assert!(
            tokio::time::timeout(Duration::from_millis(8), events.recv())
                .await
                .is_err(),
            "promoted queued question should not inherit the stale registration-time timeout"
        );

        match events.recv().await.expect("second timeout clear event") {
            WebEvent::InteractiveRequestCleared {
                request_id,
                request_kind,
                timed_out,
                ..
            } => {
                assert_eq!(request_id, second_request_id);
                assert_eq!(request_kind, "question");
                assert!(timed_out);
            }
            other => panic!("expected interactive_request_cleared event, got {other:?}"),
        }

        assert_eq!(first_reply_rx.await.expect("first timeout answer"), "");
        assert_eq!(second_reply_rx.await.expect("second timeout answer"), "");

        approval_forwarder.abort();
        question_forwarder.abort();
        plan_forwarder.abort();
    }

    #[tokio::test]
    async fn web_timeout_cleanup_discards_deferred_interactive_event_cache() {
        let temp = tempfile::tempdir().expect("tempdir");
        let base_state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let state = timeout_test_state(&base_state, Duration::from_millis(20));
        let session_id = register_test_run(&state, "web-run-timeout-cleanup").await;
        let _ = session_id;

        let (_, approval_rx) = mpsc::unbounded_channel::<ApprovalRequest>();
        let (question_tx, question_rx) = mpsc::unbounded_channel();
        let (_, plan_rx) = mpsc::unbounded_channel::<PlanRequest>();

        let (approval_forwarder, question_forwarder, plan_forwarder) =
            spawn_interactive_forwarders(state.inner.clone(), approval_rx, question_rx, plan_rx);

        let (reply_tx, _reply_rx) = oneshot::channel::<String>();
        question_tx
            .send(QuestionRequest {
                run_id: Some("web-run-timeout-cleanup".to_string()),
                question: "Will this timeout?".to_string(),
                options: vec![],
                reply: reply_tx,
            })
            .expect("question should enqueue");
        drop(question_tx);

        tokio::time::sleep(Duration::from_millis(60)).await;

        assert!(state
            .inner
            .deferred_interactive_events
            .lock()
            .await
            .is_empty());

        approval_forwarder.abort();
        question_forwarder.abort();
        plan_forwarder.abort();
    }

    #[tokio::test]
    async fn promoted_interactive_request_can_advance_to_different_run() {
        let temp = tempfile::tempdir().expect("tempdir");
        let base_state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");
        let state = timeout_test_state(&base_state, Duration::from_millis(20));
        let mut events = state.inner.event_tx.subscribe();

        register_test_run(&state, "web-run-a").await;
        register_test_run(&state, "web-run-b").await;

        let (first_tx, _first_rx) = oneshot::channel::<String>();
        let first = state
            .inner
            .pending_question_reply
            .register_with_run_id(first_tx, Some("web-run-a".to_string()))
            .await;
        let (second_tx, _second_rx) = oneshot::channel::<String>();
        let second = state
            .inner
            .pending_question_reply
            .register_with_run_id(second_tx, Some("web-run-b".to_string()))
            .await;

        state.inner.deferred_interactive_events.lock().await.insert(
            first.request_id.clone(),
            WebEvent::QuestionRequest {
                id: first.request_id.clone(),
                question: "Question A".to_string(),
                options: vec![],
                run_id: first.run_id.clone(),
            },
        );
        state.inner.deferred_interactive_events.lock().await.insert(
            second.request_id.clone(),
            WebEvent::QuestionRequest {
                id: second.request_id.clone(),
                question: "Question B".to_string(),
                options: vec![],
                run_id: second.run_id.clone(),
            },
        );

        let resolved = state
            .inner
            .pending_question_reply
            .resolve(Some(&first.request_id))
            .await
            .expect("first request should resolve");
        assert_eq!(resolved.handle.run_id.as_deref(), Some("web-run-a"));

        emit_promoted_interactive_request_event(
            &state.inner,
            InteractiveRequestKind::Question,
            Some("web-run-a"),
        )
        .await;

        match events.recv().await.expect("promoted question request") {
            WebEvent::QuestionRequest { id, run_id, .. } => {
                assert_eq!(id, second.request_id);
                assert_eq!(run_id.as_deref(), Some("web-run-b"));
            }
            other => panic!("expected promoted question request, got {other:?}"),
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
    async fn compact_context_without_session_id_does_not_use_last_session_fallback() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let stale_session_id = uuid::Uuid::new_v4();
        let mut stale_session = ava_types::Session::new().with_id(stale_session_id);
        stale_session.add_message(ava_types::Message::new(
            ava_types::Role::User,
            "legacy context",
        ));
        state
            .inner
            .stack
            .session_manager
            .save(&stale_session)
            .expect("save stale session");
        *state.inner.last_session_id.write().await = Some(stale_session_id);

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/context/compact")
                    .header("content-type", "application/json")
                    .body(Body::from("{}"))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let payload: serde_json::Value =
            serde_json::from_slice(&body).expect("compact response json");

        assert_eq!(
            payload["summary"].as_str().expect("summary string"),
            "Nothing to compact -- conversation is empty."
        );
        assert!(payload["messages"]
            .as_array()
            .expect("messages array")
            .is_empty());
    }

    #[tokio::test]
    async fn submit_route_without_session_id_does_not_use_last_session_fallback() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let stale_session_id = uuid::Uuid::new_v4();
        let mut stale_session = ava_types::Session::new().with_id(stale_session_id);
        stale_session.add_message(ava_types::Message::new(
            ava_types::Role::User,
            "legacy context",
        ));
        state
            .inner
            .stack
            .session_manager
            .save(&stale_session)
            .expect("save stale session");
        *state.inner.last_session_id.write().await = Some(stale_session_id);

        let app = build_router(state.clone());
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/submit")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        r#"{"goal":"fresh task","run_id":"web-test-submit"}"#,
                    ))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        let payload: serde_json::Value = serde_json::from_slice(&body).expect("submit response");
        let returned_session_id = payload["sessionId"]
            .as_str()
            .expect("sessionId string")
            .to_string();

        assert_ne!(returned_session_id, stale_session_id.to_string());

        state.cancel().await;
    }

    #[tokio::test]
    async fn retry_route_requires_explicit_session_id_in_web_mode() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let stale_session_id = uuid::Uuid::new_v4();
        let mut stale_session = ava_types::Session::new().with_id(stale_session_id);
        stale_session.add_message(ava_types::Message::new(ava_types::Role::User, "hello"));
        state
            .inner
            .stack
            .session_manager
            .save(&stale_session)
            .expect("save session");
        *state.inner.last_session_id.write().await = Some(stale_session_id);

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/retry")
                    .header("content-type", "application/json")
                    .body(Body::from("{}"))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("session_id is required"));
    }

    #[tokio::test]
    async fn retry_route_reuses_persisted_run_context_metadata() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = mock_run_test_state(temp.path().to_path_buf()).await;
        let (provider, model) = alternate_replay_run_identity(&state).await;
        let expected_metadata = replay_run_context_metadata(&provider, &model);

        let session_id = uuid::Uuid::new_v4();
        let mut session = ava_types::Session::new()
            .with_id(session_id)
            .with_metadata(expected_metadata.clone());
        session.add_message(ava_types::Message::new(ava_types::Role::User, "retry me"));
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .expect("save session");

        let app = build_router(state.clone());
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/retry")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"session_id":"{session_id}","run_id":"web-retry-context"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);

        wait_for_no_active_runs(&state).await;

        let persisted = state
            .inner
            .stack
            .session_manager
            .get(session_id)
            .expect("load session")
            .expect("persisted session");
        assert_eq!(
            persisted.metadata["runContext"],
            expected_metadata["runContext"]
        );
    }

    #[tokio::test]
    async fn edit_resend_route_rejects_invalid_message_targets() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let stale_session_id = uuid::Uuid::new_v4();
        let session_id = uuid::Uuid::new_v4();
        let mut session = ava_types::Session::new().with_id(session_id);
        session.add_message(ava_types::Message::new(ava_types::Role::User, "hello"));
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .expect("save session");
        *state.inner.last_session_id.write().await = Some(stale_session_id);

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/edit-resend")
                    .header("content-type", "application/json")
                    .body(Body::from(
                        format!(
                            r#"{{"session_id":"{session_id}","message_id":"not-a-uuid","new_content":"retry this"}}"#
                        ),
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
    async fn edit_resend_route_reuses_persisted_run_context_metadata() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = mock_run_test_state(temp.path().to_path_buf()).await;
        let (provider, model) = alternate_replay_run_identity(&state).await;
        let expected_metadata = replay_run_context_metadata(&provider, &model);

        let session_id = uuid::Uuid::new_v4();
        let user = ava_types::Message::new(ava_types::Role::User, "before");
        let user_id = user.id;
        let mut session = ava_types::Session::new()
            .with_id(session_id)
            .with_metadata(expected_metadata.clone());
        session.add_message(user);
        session.add_message(ava_types::Message::new(ava_types::Role::Assistant, "done"));
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .expect("save session");

        let app = build_router(state.clone());
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/edit-resend")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"session_id":"{session_id}","message_id":"{user_id}","new_content":"after","run_id":"web-edit-context"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);

        wait_for_no_active_runs(&state).await;

        let persisted = state
            .inner
            .stack
            .session_manager
            .get(session_id)
            .expect("load session")
            .expect("persisted session");
        assert_eq!(
            persisted.metadata["runContext"],
            expected_metadata["runContext"]
        );
    }

    #[tokio::test]
    async fn edit_resend_route_requires_explicit_session_id_in_web_mode() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let stale_session_id = uuid::Uuid::new_v4();
        let mut stale_session = ava_types::Session::new().with_id(stale_session_id);
        stale_session.add_message(ava_types::Message::new(ava_types::Role::User, "hello"));
        state
            .inner
            .stack
            .session_manager
            .save(&stale_session)
            .expect("save session");
        *state.inner.last_session_id.write().await = Some(stale_session_id);

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

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("session_id is required"));
    }

    #[tokio::test]
    async fn edit_resend_route_rejects_non_user_targets() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let stale_session_id = uuid::Uuid::new_v4();
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
        *state.inner.last_session_id.write().await = Some(stale_session_id);

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/edit-resend")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"session_id":"{session_id}","message_id":"{assistant_id}","new_content":"retry this"}}"#
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

    #[tokio::test]
    async fn regenerate_route_requires_explicit_session_id_in_web_mode() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let stale_session_id = uuid::Uuid::new_v4();
        let mut stale_session = ava_types::Session::new().with_id(stale_session_id);
        stale_session.add_message(ava_types::Message::new(ava_types::Role::User, "hello"));
        stale_session.add_message(ava_types::Message::new(ava_types::Role::Assistant, "done"));
        state
            .inner
            .stack
            .session_manager
            .save(&stale_session)
            .expect("save session");
        *state.inner.last_session_id.write().await = Some(stale_session_id);

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/regenerate")
                    .header("content-type", "application/json")
                    .body(Body::from("{}"))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::BAD_REQUEST);

        let body = to_bytes(response.into_body(), 8 * 1024)
            .await
            .expect("body");
        assert!(String::from_utf8_lossy(&body).contains("session_id is required"));
    }

    #[tokio::test]
    async fn regenerate_route_reuses_persisted_run_context_metadata() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = mock_run_test_state(temp.path().to_path_buf()).await;
        let (provider, model) = alternate_replay_run_identity(&state).await;
        let expected_metadata = replay_run_context_metadata(&provider, &model);

        let session_id = uuid::Uuid::new_v4();
        let mut session = ava_types::Session::new()
            .with_id(session_id)
            .with_metadata(expected_metadata.clone());
        session.add_message(ava_types::Message::new(ava_types::Role::User, "regen me"));
        session.add_message(ava_types::Message::new(ava_types::Role::Assistant, "done"));
        state
            .inner
            .stack
            .session_manager
            .save(&session)
            .expect("save session");

        let app = build_router(state.clone());
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/regenerate")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(
                        r#"{{"session_id":"{session_id}","run_id":"web-regen-context"}}"#
                    )))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);

        wait_for_no_active_runs(&state).await;

        let persisted = state
            .inner
            .stack
            .session_manager
            .get(session_id)
            .expect("load session")
            .expect("persisted session");
        assert_eq!(
            persisted.metadata["runContext"],
            expected_metadata["runContext"]
        );
    }

    #[tokio::test]
    async fn undo_route_scopes_edit_history_to_the_requested_session() {
        let temp = tempfile::tempdir().expect("tempdir");
        let state = WebState::init(temp.path().to_path_buf())
            .await
            .expect("web state");

        let session_a = uuid::Uuid::new_v4();
        let session_b = uuid::Uuid::new_v4();
        let file_a = temp.path().join("session-a.txt");
        let file_b = temp.path().join("session-b.txt");
        tokio::fs::write(&file_a, "new-a")
            .await
            .expect("write file a");
        tokio::fs::write(&file_b, "new-b")
            .await
            .expect("write file b");

        state
            .push_edit(
                session_a,
                super::state::FileEditRecord {
                    file_path: file_a.to_string_lossy().to_string(),
                    previous_content: "old-a".to_string(),
                },
            )
            .await;
        state
            .push_edit(
                session_b,
                super::state::FileEditRecord {
                    file_path: file_b.to_string_lossy().to_string(),
                    previous_content: "old-b".to_string(),
                },
            )
            .await;

        let app = build_router(state);
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/agent/undo")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(r#"{{"session_id":"{session_a}"}}"#)))
                    .expect("request"),
            )
            .await
            .expect("response");

        assert_eq!(response.status(), StatusCode::OK);
        assert_eq!(
            tokio::fs::read_to_string(&file_a)
                .await
                .expect("read file a"),
            "old-a"
        );
        assert_eq!(
            tokio::fs::read_to_string(&file_b)
                .await
                .expect("read file b"),
            "new-b"
        );
    }
}
