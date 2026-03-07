use std::collections::HashMap;

use async_trait::async_trait;
use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt, BufReader};

// ---------------------------------------------------------------------------
// JSON-RPC types
// ---------------------------------------------------------------------------

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
    pub fn request(id: u64, method: &str, params: Value) -> Self {
        Self {
            jsonrpc: "2.0".to_string(),
            id: Some(Value::Number(id.into())),
            method: Some(method.to_string()),
            params: Some(params),
            result: None,
            error: None,
        }
    }

    pub fn notification(method: &str, params: Value) -> Self {
        Self {
            jsonrpc: "2.0".to_string(),
            id: None,
            method: Some(method.to_string()),
            params: Some(params),
            result: None,
            error: None,
        }
    }

    pub fn is_error(&self) -> bool {
        self.error.is_some()
    }
}

// ---------------------------------------------------------------------------
// MCPTransport trait
// ---------------------------------------------------------------------------

#[async_trait]
pub trait MCPTransport: Send + Sync {
    async fn send(&mut self, message: &JsonRpcMessage) -> Result<()>;
    async fn receive(&mut self) -> Result<JsonRpcMessage>;
    async fn close(&mut self) -> Result<()>;
}

// ---------------------------------------------------------------------------
// Wire-format helpers (Content-Length framing)
// ---------------------------------------------------------------------------

pub fn encode_message(payload: &str) -> String {
    format!("Content-Length: {}\r\n\r\n{}", payload.len(), payload)
}

pub fn decode_message(frame: &str) -> Result<String> {
    let (headers, body) = frame
        .split_once("\r\n\r\n")
        .ok_or_else(|| AvaError::ValidationError("missing header delimiter".to_string()))?;

    let headers = parse_headers(headers)?;
    let len = headers
        .get("content-length")
        .ok_or_else(|| AvaError::ValidationError("missing content-length header".to_string()))?
        .parse::<usize>()
        .map_err(|error| AvaError::ValidationError(format!("invalid content-length: {error}")))?;

    if body.len() != len {
        return Err(AvaError::ValidationError(format!(
            "body length mismatch: expected {len}, got {}",
            body.len()
        )));
    }

    Ok(body.to_string())
}

fn parse_headers(raw: &str) -> Result<HashMap<String, String>> {
    let mut headers = HashMap::new();
    for line in raw.lines() {
        let (key, value) = line
            .split_once(':')
            .ok_or_else(|| AvaError::ValidationError(format!("invalid header line: {line}")))?;
        headers.insert(key.trim().to_ascii_lowercase(), value.trim().to_string());
    }
    Ok(headers)
}

// ---------------------------------------------------------------------------
// Raw framed reader/writer (shared by StdioTransport and tests)
// ---------------------------------------------------------------------------

async fn send_framed<W: AsyncWrite + Unpin>(writer: &mut W, msg: &JsonRpcMessage) -> Result<()> {
    let payload =
        serde_json::to_string(msg).map_err(|e| AvaError::SerializationError(e.to_string()))?;
    let frame = encode_message(&payload);
    writer
        .write_all(frame.as_bytes())
        .await
        .map_err(AvaError::from)?;
    writer.flush().await.map_err(AvaError::from)?;
    Ok(())
}

async fn receive_framed<R: AsyncRead + Unpin>(reader: &mut BufReader<R>) -> Result<JsonRpcMessage> {
    let mut header_bytes = Vec::new();
    let mut byte = [0_u8; 1];

    loop {
        let n = reader.read(&mut byte).await.map_err(AvaError::from)?;
        if n == 0 {
            return Err(AvaError::ValidationError(
                "unexpected EOF while reading headers".to_string(),
            ));
        }
        header_bytes.push(byte[0]);
        if header_bytes.ends_with(b"\r\n\r\n") {
            break;
        }
    }

    let header_text = String::from_utf8(header_bytes)
        .map_err(|e| AvaError::SerializationError(e.to_string()))?;
    let headers = parse_headers(header_text.trim_end_matches("\r\n\r\n"))?;
    let len = headers
        .get("content-length")
        .ok_or_else(|| AvaError::ValidationError("missing content-length header".to_string()))?
        .parse::<usize>()
        .map_err(|e| AvaError::ValidationError(format!("invalid content-length header: {e}")))?;

    let mut body = vec![0_u8; len];
    reader.read_exact(&mut body).await.map_err(AvaError::from)?;

    let body =
        String::from_utf8(body).map_err(|e| AvaError::SerializationError(e.to_string()))?;
    serde_json::from_str(&body).map_err(|e| AvaError::SerializationError(e.to_string()))
}

// ---------------------------------------------------------------------------
// StdioTransport — spawns subprocess, communicates via stdin/stdout
// ---------------------------------------------------------------------------

pub struct StdioTransport {
    child: tokio::process::Child,
    stdin: tokio::io::BufWriter<tokio::process::ChildStdin>,
    stdout: BufReader<tokio::process::ChildStdout>,
}

impl StdioTransport {
    pub async fn spawn(
        command: &str,
        args: &[String],
        env: &HashMap<String, String>,
    ) -> Result<Self> {
        let mut cmd = tokio::process::Command::new(command);
        cmd.args(args)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped());

        for (key, value) in env {
            cmd.env(key, value);
        }

        let mut child = cmd.spawn().map_err(|e| {
            AvaError::ToolError(format!(
                "failed to spawn MCP server (command: {command} {}): {e}. \
                 Ensure the server binary is installed and in PATH",
                args.join(" ")
            ))
        })?;

        let stdin = child.stdin.take().ok_or_else(|| {
            AvaError::ToolError(
                "MCP server started but stdin is unavailable".to_string(),
            )
        })?;
        let stdout = child.stdout.take().ok_or_else(|| {
            AvaError::ToolError(
                "MCP server started but stdout is unavailable".to_string(),
            )
        })?;

        Ok(Self {
            child,
            stdin: tokio::io::BufWriter::new(stdin),
            stdout: BufReader::new(stdout),
        })
    }
}

#[async_trait]
impl MCPTransport for StdioTransport {
    async fn send(&mut self, message: &JsonRpcMessage) -> Result<()> {
        send_framed(&mut self.stdin, message).await
    }

    async fn receive(&mut self) -> Result<JsonRpcMessage> {
        receive_framed(&mut self.stdout).await
    }

    async fn close(&mut self) -> Result<()> {
        let _ = self.child.kill().await;
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// HttpTransport — connects to an HTTP MCP server
// ---------------------------------------------------------------------------

pub struct HttpTransport {
    #[allow(dead_code)]
    base_url: String,
    #[allow(dead_code)]
    session_id: Option<String>,
}

impl HttpTransport {
    pub fn new(base_url: &str) -> Self {
        Self {
            base_url: base_url.trim_end_matches('/').to_string(),
            session_id: None,
        }
    }
}

#[async_trait]
impl MCPTransport for HttpTransport {
    async fn send(&mut self, message: &JsonRpcMessage) -> Result<()> {
        // For HTTP transport we combine send+receive in receive().
        // Store the outgoing message for the next receive() call.
        // In a real implementation we'd use reqwest. For now we store
        // the serialized message and send it on receive().
        // NOTE: This is a simplified HTTP transport. A full implementation
        // would use SSE for server-to-client messages.
        let _ = message;
        Ok(())
    }

    async fn receive(&mut self) -> Result<JsonRpcMessage> {
        Err(AvaError::ToolError(
            "HTTP transport not yet fully implemented — use stdio transport".to_string(),
        ))
    }

    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// InMemoryTransport — for testing
// ---------------------------------------------------------------------------

/// A transport backed by in-memory channels. Useful for unit tests.
pub struct InMemoryTransport {
    outgoing: tokio::sync::mpsc::UnboundedSender<JsonRpcMessage>,
    incoming: tokio::sync::mpsc::UnboundedReceiver<JsonRpcMessage>,
}

impl InMemoryTransport {
    /// Create a pair of transports that are connected to each other.
    pub fn pair() -> (Self, Self) {
        let (tx_a, rx_b) = tokio::sync::mpsc::unbounded_channel();
        let (tx_b, rx_a) = tokio::sync::mpsc::unbounded_channel();
        (
            Self {
                outgoing: tx_a,
                incoming: rx_a,
            },
            Self {
                outgoing: tx_b,
                incoming: rx_b,
            },
        )
    }
}

#[async_trait]
impl MCPTransport for InMemoryTransport {
    async fn send(&mut self, message: &JsonRpcMessage) -> Result<()> {
        self.outgoing
            .send(message.clone())
            .map_err(|_| AvaError::ToolError("transport channel closed".to_string()))
    }

    async fn receive(&mut self) -> Result<JsonRpcMessage> {
        self.incoming
            .recv()
            .await
            .ok_or_else(|| AvaError::ToolError("transport channel closed".to_string()))
    }

    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Legacy generic transport (used by AVAMCPServer)
// ---------------------------------------------------------------------------

/// Generic framed transport over arbitrary async readers/writers.
/// Retained for backward compatibility with `AVAMCPServer`.
pub struct FramedTransport<R, W>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    reader: BufReader<R>,
    writer: W,
}

impl<R, W> FramedTransport<R, W>
where
    R: AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    pub fn new(reader: R, writer: W) -> Self {
        Self {
            reader: BufReader::new(reader),
            writer,
        }
    }

    pub async fn send(&mut self, value: Value) -> Result<()> {
        let payload =
            serde_json::to_string(&value).map_err(|e| AvaError::SerializationError(e.to_string()))?;
        let frame = encode_message(&payload);
        self.writer
            .write_all(frame.as_bytes())
            .await
            .map_err(AvaError::from)?;
        self.writer.flush().await.map_err(AvaError::from)?;
        Ok(())
    }

    pub async fn receive(&mut self) -> Result<Value> {
        let mut header_bytes = Vec::new();
        let mut byte = [0_u8; 1];

        loop {
            let n = self.reader.read(&mut byte).await.map_err(AvaError::from)?;
            if n == 0 {
                return Err(AvaError::ValidationError(
                    "unexpected EOF while reading headers".to_string(),
                ));
            }
            header_bytes.push(byte[0]);
            if header_bytes.ends_with(b"\r\n\r\n") {
                break;
            }
        }

        let header_text = String::from_utf8(header_bytes)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;
        let headers = parse_headers(header_text.trim_end_matches("\r\n\r\n"))?;
        let len = headers
            .get("content-length")
            .ok_or_else(|| {
                AvaError::ValidationError("missing content-length header".to_string())
            })?
            .parse::<usize>()
            .map_err(|e| {
                AvaError::ValidationError(format!("invalid content-length header: {e}"))
            })?;

        let mut body = vec![0_u8; len];
        self.reader
            .read_exact(&mut body)
            .await
            .map_err(AvaError::from)?;

        let body = String::from_utf8(body)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;
        serde_json::from_str(&body).map_err(|e| AvaError::SerializationError(e.to_string()))
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encode_decode_roundtrip() {
        let payload = r#"{"jsonrpc":"2.0","id":1,"method":"test"}"#;
        let frame = encode_message(payload);
        let decoded = decode_message(&frame).unwrap();
        assert_eq!(decoded, payload);
    }

    #[test]
    fn json_rpc_message_serialization() {
        let msg = JsonRpcMessage::request(1, "initialize", serde_json::json!({}));
        let json = serde_json::to_string(&msg).unwrap();
        let parsed: JsonRpcMessage = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.jsonrpc, "2.0");
        assert_eq!(parsed.method.as_deref(), Some("initialize"));
        assert_eq!(parsed.id, Some(Value::Number(1.into())));
    }

    #[test]
    fn json_rpc_notification_has_no_id() {
        let msg = JsonRpcMessage::notification("initialized", serde_json::json!({}));
        let json = serde_json::to_string(&msg).unwrap();
        assert!(!json.contains("\"id\""));
    }

    #[tokio::test]
    async fn in_memory_transport_roundtrip() {
        let (mut client, mut server) = InMemoryTransport::pair();
        let msg = JsonRpcMessage::request(1, "test", serde_json::json!({"foo": "bar"}));

        client.send(&msg).await.unwrap();
        let received = server.receive().await.unwrap();
        assert_eq!(received.method.as_deref(), Some("test"));
    }

    #[tokio::test]
    async fn stdio_transport_with_cat() {
        // Use `cat` as an echo server — we write framed bytes and read them back.
        // NOTE: `cat` simply echoes stdin to stdout, so we can round-trip framed
        // content through it. However, `cat` doesn't speak JSON-RPC, so we test
        // the raw framing rather than full protocol.
        let transport = StdioTransport::spawn("cat", &[], &HashMap::new()).await;
        if transport.is_err() {
            // cat may not be available in all test environments
            return;
        }
        let mut transport = transport.unwrap();

        let msg = JsonRpcMessage::request(42, "ping", serde_json::json!({}));
        transport.send(&msg).await.unwrap();
        let echo = transport.receive().await.unwrap();
        assert_eq!(echo.id, Some(Value::Number(42.into())));
        assert_eq!(echo.method.as_deref(), Some("ping"));

        transport.close().await.unwrap();
    }
}
