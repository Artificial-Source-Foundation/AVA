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
//! | GET    | `/api/sessions`                   | List recent sessions                      |
//! | POST   | `/api/sessions/create`            | Create a new session                      |
//! | POST   | `/api/sessions/search`            | Search sessions by message content        |
//! | GET    | `/api/sessions/{id}`              | Get session with messages                 |
//! | POST   | `/api/sessions/{id}/rename`       | Rename a session                          |
//! | DELETE | `/api/sessions/{id}`              | Delete a session                          |
//! | POST   | `/api/sessions/{id}/message`      | Add a message to a session                |
//! | GET    | `/api/sessions/{id}/agents`       | List agents for a session (stub)          |
//! | GET    | `/api/sessions/{id}/files`        | List file operations (stub)               |
//! | GET    | `/api/sessions/{id}/terminal`     | List terminal executions (stub)           |
//! | GET    | `/api/sessions/{id}/memory`       | List memory items (stub)                  |
//! | GET    | `/api/sessions/{id}/checkpoints`  | List checkpoints (stub)                   |
//! | GET    | `/api/models`                     | List available models                     |
//! | GET    | `/api/models/current`             | Get the currently-active model            |
//! | POST   | `/api/models/switch`              | Switch the active model                   |
//! | GET    | `/api/providers`                  | List configured providers                 |
//! | GET    | `/api/config`                     | Get full configuration as JSON            |
//! | GET    | `/api/permissions`                | Get current permission level              |
//! | POST   | `/api/permissions`                | Set permission level                      |
//! | POST   | `/api/permissions/toggle`         | Toggle permission level                   |
//! | POST   | `/api/log`                        | Ingest frontend log entry                 |
//! | GET    | `/api/health`                     | Health check                              |
//! | GET    | `/ws`                             | WebSocket for streaming events            |

pub mod api;
pub mod state;
pub mod ws;

use std::path::PathBuf;

use axum::http::Method;
use axum::routing::{get, post};
use axum::Router;
use color_eyre::Result;
use tower_http::cors::{Any, CorsLayer};
use tracing::info;

use self::state::WebState;

/// Build the axum router with all API routes and WebSocket endpoint.
fn build_router(state: WebState) -> Router {
    let cors = CorsLayer::new()
        .allow_origin(Any)
        .allow_methods([Method::GET, Method::POST, Method::DELETE, Method::OPTIONS])
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
        // Session CRUD endpoints
        .route("/api/sessions", get(api::list_sessions))
        .route("/api/sessions/create", post(api::create_session))
        .route("/api/sessions/search", post(api::search_sessions))
        .route(
            "/api/sessions/{id}",
            get(api::get_session).delete(api::delete_session),
        )
        .route("/api/sessions/{id}/rename", post(api::rename_session))
        // Message endpoint
        .route("/api/sessions/{id}/message", post(api::add_message))
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
        // Model/provider endpoints
        .route("/api/models", get(api::list_models))
        .route("/api/models/current", get(api::get_current_model))
        .route("/api/models/switch", post(api::switch_model))
        .route("/api/providers", get(api::list_providers))
        // Config
        .route("/api/config", get(api::get_config))
        // Permission level
        .route(
            "/api/permissions",
            get(api::get_permission_level).post(api::set_permission_level),
        )
        .route(
            "/api/permissions/toggle",
            post(api::toggle_permission_level),
        )
        // WebSocket
        .route("/ws", get(ws::ws_handler))
        // Frontend log ingestion
        .route("/api/log", post(api::ingest_frontend_log))
        // Health check
        .route("/api/health", get(api::health))
        .layer(cors)
        .with_state(state)
}

/// Start the AVA web server on the given host and port.
///
/// The TCP listener is bound **before** MCP/plugin initialisation begins so
/// that the port is claimed immediately.  MCP servers and plugins are
/// initialised as a background task; the server starts serving requests as soon
/// as `AgentStack` construction completes (everything except MCP connect).
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
    info!("Initialising agent stack (MCP / plugins loading in background)…");

    // Initialise the agent stack.  `AgentStack::new` already spawns codebase
    // indexing as a background task; MCP and plugin init are synchronous here
    // but bounded by per-server timeouts (15 s for MCP, 10 s for plugins).
    // The listener is already bound above, so the port is usable during init.
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

/// Resolve the data directory, creating it if necessary.
#[allow(dead_code)]
fn resolve_data_dir() -> PathBuf {
    let dir = dirs::home_dir().unwrap_or_default().join(".ava");
    std::fs::create_dir_all(&dir).ok();
    dir
}
