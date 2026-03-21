//! WebSocket handler for streaming agent events to connected clients.
//!
//! Each WebSocket connection subscribes to the shared `broadcast::Sender`
//! in `WebState`. When the agent emits events, they are broadcast to all
//! connected clients as JSON messages.
//!
//! Message format (server -> client):
//! ```json
//! {"type":"token","content":"hello"}
//! {"type":"tool_call","name":"read","args":{...}}
//! {"type":"tool_result","content":"...","is_error":false}
//! {"type":"complete","session":{...}}
//! {"type":"error","message":"something went wrong"}
//! ```

use axum::extract::ws::{Message, WebSocket};
use axum::extract::{State, WebSocketUpgrade};
use axum::response::IntoResponse;
use futures::{SinkExt, StreamExt};
use tokio::sync::broadcast::error::RecvError;
use tracing::{debug, warn};

use super::api::convert_web_event;
use super::state::WebState;

/// Axum handler that upgrades the HTTP connection to a WebSocket.
pub async fn ws_handler(ws: WebSocketUpgrade, State(state): State<WebState>) -> impl IntoResponse {
    ws.on_upgrade(move |socket| handle_socket(socket, state))
}

/// Handle a single WebSocket connection.
///
/// Subscribes to the broadcast channel and forwards all agent events as JSON
/// text frames. Events are converted from the backend `AgentEvent` enum to the
/// frontend-compatible `WebAgentEvent` format (with `"type"` tag).
///
/// The send loop survives across multiple agent runs — it only exits when the
/// WebSocket client disconnects or the broadcast sender is dropped (server
/// shutdown). `RecvError::Lagged` (buffer overflow) is handled by logging a
/// warning and continuing so that a burst of events never closes the connection.
async fn handle_socket(socket: WebSocket, state: WebState) {
    let (mut ws_tx, mut ws_rx) = socket.split();

    // Subscribe to agent events
    let mut event_rx = state.inner.event_tx.subscribe();

    debug!("WebSocket client connected");

    // Spawn a task that forwards broadcast events to the WebSocket.
    //
    // The loop explicitly matches on RecvError variants so that a Lagged error
    // (broadcast buffer overflow) merely skips the dropped events rather than
    // terminating the connection. Only RecvError::Closed (sender dropped on
    // server shutdown) causes a clean exit.
    let send_task = tokio::spawn(async move {
        loop {
            let event = match event_rx.recv().await {
                Ok(e) => e,
                Err(RecvError::Lagged(skipped)) => {
                    // The broadcast buffer overflowed; some events were dropped.
                    // Notify the client and continue — do NOT close the connection.
                    warn!("WebSocket broadcast lagged, skipped {skipped} events");
                    let lag_msg = serde_json::json!({
                        "type": "lag",
                        "skipped": skipped,
                    })
                    .to_string();
                    if ws_tx.send(Message::Text(lag_msg.into())).await.is_err() {
                        // Client disconnected while we were recovering
                        break;
                    }
                    continue;
                }
                Err(RecvError::Closed) => {
                    // Broadcast sender was dropped — server is shutting down.
                    break;
                }
            };

            // Convert backend event to frontend-compatible format
            let web_event = match convert_web_event(&event) {
                Some(e) => e,
                None => continue, // Skip events without frontend representation
            };
            let json = match serde_json::to_string(&web_event) {
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

    // Listen for incoming messages (ping/pong keep-alive)
    let recv_task = tokio::spawn(async move {
        while let Some(Ok(msg)) = ws_rx.next().await {
            match msg {
                Message::Text(text) => {
                    debug!("WebSocket received text: {}", text);
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
