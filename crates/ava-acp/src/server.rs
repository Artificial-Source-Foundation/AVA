//! ACP Server — makes AVA an ACP-compliant agent server.
//!
//! When running in `--acp-server` mode, AVA listens on stdin for JSON-RPC
//! requests and streams agent events as NDJSON to stdout. This allows IDEs
//! like Zed to use AVA as an external agent.
//!
//! Protocol: NDJSON over stdio (one JSON object per line).
//!
//! Supported methods:
//! - `agent/query` — start a task, returns streaming notifications
//! - `agent/interrupt` — interrupt the running task
//! - `agent/cancel` — cancel the running task
//! - `agent/capabilities` — return agent capabilities

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tracing::{debug, info, warn};

/// JSON-RPC 2.0 message envelope.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcMessage {
    pub jsonrpc: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub method: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub params: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<JsonRpcError>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct JsonRpcError {
    pub code: i64,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<Value>,
}

impl JsonRpcMessage {
    fn response(id: Value, result: Value) -> Self {
        Self {
            jsonrpc: "2.0".into(),
            id: Some(id),
            method: None,
            params: None,
            result: Some(result),
            error: None,
        }
    }

    fn error_response(id: Value, code: i64, message: &str) -> Self {
        Self {
            jsonrpc: "2.0".into(),
            id: Some(id),
            method: None,
            params: None,
            result: None,
            error: Some(JsonRpcError {
                code,
                message: message.into(),
                data: None,
            }),
        }
    }

    fn notification(method: &str, params: Value) -> Self {
        Self {
            jsonrpc: "2.0".into(),
            id: None,
            method: Some(method.into()),
            params: Some(params),
            result: None,
            error: None,
        }
    }
}

/// ACP server capabilities response.
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct Capabilities {
    name: String,
    version: String,
    tools: Vec<String>,
    supports_streaming: bool,
    supports_interrupt: bool,
    supports_cancel: bool,
}

/// Run the ACP server on stdin/stdout.
///
/// Reads JSON-RPC requests from stdin, processes them, and writes
/// responses/notifications to stdout.
pub async fn run_acp_server() -> Result<()> {
    info!("AVA ACP server starting on stdio");

    let stdin = tokio::io::stdin();
    let mut stdout = tokio::io::stdout();
    let mut reader = BufReader::new(stdin);

    // Send init notification
    let init = JsonRpcMessage::notification(
        "agent/init",
        serde_json::json!({
            "name": "ava",
            "version": env!("CARGO_PKG_VERSION"),
            "protocol": "acp-v1"
        }),
    );
    write_message(&mut stdout, &init).await?;

    loop {
        let mut line = String::new();
        let n = reader.read_line(&mut line).await.map_err(AvaError::from)?;
        if n == 0 {
            debug!("stdin EOF, shutting down ACP server");
            break;
        }

        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }

        let msg: JsonRpcMessage = match serde_json::from_str(trimmed) {
            Ok(m) => m,
            Err(e) => {
                warn!("invalid JSON-RPC message: {e}");
                continue;
            }
        };

        let method = msg.method.as_deref().unwrap_or("");
        let id = msg.id.clone().unwrap_or(Value::Null);

        match method {
            "agent/capabilities" => {
                let caps = Capabilities {
                    name: "ava".into(),
                    version: env!("CARGO_PKG_VERSION").into(),
                    tools: vec![
                        "Read".into(),
                        "Write".into(),
                        "Edit".into(),
                        "Bash".into(),
                        "Glob".into(),
                        "Grep".into(),
                        "WebSearch".into(),
                        "WebFetch".into(),
                        "GitRead".into(),
                    ],
                    supports_streaming: true,
                    supports_interrupt: true,
                    supports_cancel: true,
                };
                let resp = JsonRpcMessage::response(id, serde_json::to_value(caps).unwrap());
                write_message(&mut stdout, &resp).await?;
            }

            "agent/query" => {
                // Parse query params
                let query: crate::protocol::AgentQuery = match msg.params {
                    Some(params) => match serde_json::from_value(params) {
                        Ok(q) => q,
                        Err(e) => {
                            let resp = JsonRpcMessage::error_response(
                                id,
                                -32602,
                                &format!("Invalid params: {e}"),
                            );
                            write_message(&mut stdout, &resp).await?;
                            continue;
                        }
                    },
                    None => {
                        let resp = JsonRpcMessage::error_response(id, -32602, "Missing params");
                        write_message(&mut stdout, &resp).await?;
                        continue;
                    }
                };

                // Acknowledge the request
                let ack = JsonRpcMessage::response(id, serde_json::json!({"status": "streaming"}));
                write_message(&mut stdout, &ack).await?;

                // TODO: Wire to AgentStack and stream events as notifications.
                // For now, send a placeholder response.
                let result_msg = JsonRpcMessage::notification(
                    "agent/message",
                    serde_json::json!({
                        "type": "result",
                        "result": format!("ACP server received query: {}", query.prompt),
                        "subtype": "success"
                    }),
                );
                write_message(&mut stdout, &result_msg).await?;
            }

            "agent/interrupt" => {
                debug!("received interrupt request");
                let resp =
                    JsonRpcMessage::response(id, serde_json::json!({"status": "interrupted"}));
                write_message(&mut stdout, &resp).await?;
            }

            "agent/cancel" => {
                debug!("received cancel request");
                let resp = JsonRpcMessage::response(id, serde_json::json!({"status": "cancelled"}));
                write_message(&mut stdout, &resp).await?;
            }

            _ => {
                let resp = JsonRpcMessage::error_response(
                    id,
                    -32601,
                    &format!("Method not found: {method}"),
                );
                write_message(&mut stdout, &resp).await?;
            }
        }
    }

    info!("AVA ACP server stopped");
    Ok(())
}

async fn write_message<W: AsyncWriteExt + Unpin>(
    writer: &mut W,
    msg: &JsonRpcMessage,
) -> Result<()> {
    let mut payload =
        serde_json::to_string(msg).map_err(|e| AvaError::SerializationError(e.to_string()))?;
    payload.push('\n');
    writer
        .write_all(payload.as_bytes())
        .await
        .map_err(AvaError::from)?;
    writer.flush().await.map_err(AvaError::from)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn json_rpc_response_serde() {
        let resp =
            JsonRpcMessage::response(Value::Number(1.into()), serde_json::json!({"status": "ok"}));
        let json = serde_json::to_string(&resp).unwrap();
        assert!(json.contains("\"jsonrpc\":\"2.0\""));
        assert!(json.contains("\"result\":{\"status\":\"ok\"}"));
    }

    #[test]
    fn json_rpc_notification_serde() {
        let notif = JsonRpcMessage::notification(
            "agent/message",
            serde_json::json!({"type": "text", "content": "hello"}),
        );
        let json = serde_json::to_string(&notif).unwrap();
        assert!(!json.contains("\"id\""));
        assert!(json.contains("\"method\":\"agent/message\""));
    }

    #[test]
    fn capabilities_serializes() {
        let caps = Capabilities {
            name: "ava".into(),
            version: "2.2.7".into(),
            tools: vec!["Read".into(), "Write".into()],
            supports_streaming: true,
            supports_interrupt: true,
            supports_cancel: true,
        };
        let json = serde_json::to_value(caps).unwrap();
        assert_eq!(json["name"], "ava");
        assert!(json["supportsStreaming"].as_bool().unwrap());
    }
}
