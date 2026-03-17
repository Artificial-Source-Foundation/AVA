//! WebSocket handler for streaming agent events to connected clients.
//!
//! Each WebSocket connection subscribes to the shared `broadcast::Sender`
//! in `WebState`. When the agent emits events, they are broadcast to all
//! connected clients as JSON messages.
//!
//! Message format (server -> client):
//! ```json
//! {"Token":"hello"}
//! {"ToolCall":{"id":"...","name":"read","arguments":{...}}}
//! {"ToolResult":{"tool_use_id":"...","content":"..."}}
//! {"Complete":{...session...}}
//! {"Error":"something went wrong"}
//! ```

use axum::extract::ws::{Message, WebSocket};
use axum::extract::{State, WebSocketUpgrade};
use axum::response::IntoResponse;
use futures::{SinkExt, StreamExt};
use tracing::{debug, warn};

use super::state::WebState;

/// Axum handler that upgrades the HTTP connection to a WebSocket.
pub async fn ws_handler(ws: WebSocketUpgrade, State(state): State<WebState>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| handle_socket(socket, state))
}

/// Handle a single WebSocket connection.
///
/// Subscribes to the broadcast channel and forwards all agent events as JSON
/// text frames. Also listens for incoming messages from the client (currently
/// used for ping/pong keep-alive; future: mid-stream steering).
async fn handle_socket(socket: WebSocket, state: WebState) {
    let (mut ws_tx, mut ws_rx) = socket.split();

    // Subscribe to agent events
    let mut event_rx = state.inner.event_tx.subscribe();

    debug!("WebSocket client connected");

    // Spawn a task that forwards broadcast events to the WebSocket
    let send_task = tokio::spawn(async move {
        while let Ok(event) = event_rx.recv().await {
            let json = match serde_json::to_string(&event) {
                Ok(j) => j,
                Err(e) => {
                    warn!("Failed to serialize agent event: {e}");
                    continue;
                }
            };
            if ws_tx.send(Message::Text(json.into())).await.is_err() {
                // Client disconnected
                break;
            }
        }
    });

    // Listen for incoming messages (ping/pong, future: steering commands)
    let recv_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_rx.next().await {
            match msg {
                Message::Text(text) => {
                    debug!("WebSocket received text: {}", text);
                    // Future: parse steering/follow-up/post-complete commands
                }
                Message::Close(_) => break,
                _ => {}
            }
        }
    });

    // Wait for either task to finish (client disconnect or broadcast end)
    tokio::select! {
        _ = send_task => {},
        _ = recv_task => {},
    }

    debug!("WebSocket client disconnected");
}
