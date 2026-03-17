//! AVA web server — HTTP API + WebSocket for agent events.
//!
//! Replaces the Tauri IPC layer for browser-based frontends. The server
//! exposes a JSON REST API under `/api/` and a WebSocket endpoint at `/ws`
//! for streaming agent events.
//!
//! # Endpoints
//!
//! | Method | Path                          | Description                              |
//! |--------|-------------------------------|------------------------------------------|
//! | POST   | `/api/agent/submit`           | Start the agent (async, streams via WS)  |
//! | POST   | `/api/agent/cancel`           | Cancel the running agent                 |
//! | GET    | `/api/agent/status`           | Get agent running status                 |
//! | GET    | `/api/sessions`               | List recent sessions                     |
//! | POST   | `/api/sessions/create`        | Create a new session                     |
//! | GET    | `/api/sessions/{id}`          | Get session with messages                |
//! | POST   | `/api/sessions/{id}/rename`   | Rename a session                         |
//! | DELETE | `/api/sessions/{id}`          | Delete a session                         |
//! | POST   | `/api/sessions/{id}/message`  | Add a message to a session               |
//! | GET    | `/api/models`                 | List available models                    |
//! | GET    | `/api/providers`              | List configured providers                |
//! | POST   | `/api/log`                    | Ingest frontend log entry                |
//! | GET    | `/ws`                         | WebSocket for streaming events           |

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
        // Session CRUD endpoints
        .route("/api/sessions", get(api::list_sessions))
        .route("/api/sessions/create", post(api::create_session))
        .route(
            "/api/sessions/{id}",
            get(api::get_session).delete(api::delete_session),
        )
        .route("/api/sessions/{id}/rename", post(api::rename_session))
        // Message endpoint
        .route("/api/sessions/{id}/message", post(api::add_message))
        // Body-based session operations (for frontend apiInvoke compatibility)
        .route("/api/sessions/delete", post(api::delete_session_body))
        .route("/api/sessions/rename", post(api::rename_session_body))
        .route("/api/sessions/load", post(api::load_session_body))
        // Model/provider endpoints
        .route("/api/models", get(api::list_models))
        .route("/api/providers", get(api::list_providers))
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
pub async fn run_server(host: &str, port: u16) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

    // Ensure the logs directory exists for frontend log ingestion
    let logs_dir = data_dir.join("logs");
    std::fs::create_dir_all(&logs_dir).ok();

    let state = WebState::init(data_dir).await?;

    let app = build_router(state);

    let addr = format!("{host}:{port}");
    let listener = tokio::net::TcpListener::bind(&addr).await?;

    info!("AVA web server listening on http://{addr}");
    eprintln!("AVA web server running at http://{addr}");
    eprintln!("  API:       http://{addr}/api/");
    eprintln!("  WebSocket: ws://{addr}/ws");
    eprintln!("  Health:    http://{addr}/api/health");
    eprintln!();
    eprintln!("Press Ctrl+C to stop.");

    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown_signal())
        .await?;

    eprintln!("\nShutting down.");
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
