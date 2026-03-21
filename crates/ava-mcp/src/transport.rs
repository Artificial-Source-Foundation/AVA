use std::collections::HashMap;

use async_trait::async_trait;
use ava_types::{AvaError, Result};
use futures::StreamExt;
use reqwest::header::{ACCEPT, AUTHORIZATION, CONTENT_TYPE};
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

/// Bidirectional transport for MCP (Model Context Protocol) communication.
///
/// Implementations handle the framing and delivery of JSON-RPC messages
/// between the host and an MCP server. Messages are sent and received
/// sequentially — callers must not overlap `send`/`receive` calls.
#[async_trait]
pub trait MCPTransport: Send + Sync {
    /// Send a JSON-RPC message to the MCP server.
    async fn send(&mut self, message: &JsonRpcMessage) -> Result<()>;
    /// Receive the next JSON-RPC message from the MCP server.
    async fn receive(&mut self) -> Result<JsonRpcMessage>;
    /// Shut down the transport, releasing resources (e.g., killing child process).
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
// Newline-delimited JSON reader/writer (used by StdioTransport)
//
// The official MCP SDK (@modelcontextprotocol/sdk) uses NDJSON over stdio:
// each message is a single line of JSON terminated by '\n'. This matches
// what Playwright MCP, Claude Desktop, and other MCP SDK-based servers expect.
// See: https://spec.modelcontextprotocol.io/specification/basic/transports/#stdio
// ---------------------------------------------------------------------------

async fn send_ndjson<W: AsyncWrite + Unpin>(writer: &mut W, msg: &JsonRpcMessage) -> Result<()> {
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

async fn receive_ndjson<R: AsyncRead + Unpin>(reader: &mut BufReader<R>) -> Result<JsonRpcMessage> {
    use tokio::io::AsyncBufReadExt;

    let mut line = String::new();
    loop {
        line.clear();
        let n = reader.read_line(&mut line).await.map_err(AvaError::from)?;
        if n == 0 {
            return Err(AvaError::ValidationError(
                "unexpected EOF while reading MCP message".to_string(),
            ));
        }
        let trimmed = line.trim();
        if trimmed.is_empty() {
            // Skip blank lines (some servers emit them between messages)
            continue;
        }
        return serde_json::from_str(trimmed)
            .map_err(|e| AvaError::SerializationError(format!("MCP message parse error: {e}")));
    }
}

// ---------------------------------------------------------------------------
// StdioTransport — spawns subprocess, communicates via stdin/stdout
// ---------------------------------------------------------------------------

pub struct StdioTransport {
    child: tokio::process::Child,
    stdin: tokio::io::BufWriter<tokio::process::ChildStdin>,
    stdout: BufReader<tokio::process::ChildStdout>,
}

/// Environment variable names that should never be forwarded to MCP server processes.
const SENSITIVE_ENV_VARS: &[&str] = &[
    "AWS_SECRET_ACCESS_KEY",
    "AWS_SESSION_TOKEN",
    "GITHUB_TOKEN",
    "GH_TOKEN",
    "OPENAI_API_KEY",
    "ANTHROPIC_API_KEY",
    "OPENROUTER_API_KEY",
    "GOOGLE_API_KEY",
    "AZURE_OPENAI_API_KEY",
    "HF_TOKEN",
    "HUGGING_FACE_HUB_TOKEN",
    "AVA_MASTER_PASSWORD",
];

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
            .stderr(std::process::Stdio::null())
            .kill_on_drop(true);

        // Remove sensitive environment variables from the inherited environment
        for var in SENSITIVE_ENV_VARS {
            cmd.env_remove(var);
        }

        // Apply user-configured env vars (these are explicitly allowed by the user)
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
            AvaError::ToolError("MCP server started but stdin is unavailable".to_string())
        })?;
        let stdout = child.stdout.take().ok_or_else(|| {
            AvaError::ToolError("MCP server started but stdout is unavailable".to_string())
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
        send_ndjson(&mut self.stdin, message).await
    }

    async fn receive(&mut self) -> Result<JsonRpcMessage> {
        receive_ndjson(&mut self.stdout).await
    }

    async fn close(&mut self) -> Result<()> {
        let _ = self.child.kill().await;
        Ok(())
    }
}

// ---------------------------------------------------------------------------
// HttpTransport — connects to a remote HTTP/SSE MCP server
// ---------------------------------------------------------------------------

/// HTTP transport configuration for a remote MCP server.
#[derive(Debug, Clone, Default)]
pub struct HttpTransportConfig {
    /// Bearer token for `Authorization: Bearer <token>` header.
    pub bearer_token: Option<String>,
    /// Additional headers sent on every request.
    pub headers: HashMap<String, String>,
}

/// HTTP/SSE transport for remote MCP servers.
///
/// Protocol:
/// - Tool calls (JSON-RPC requests/notifications): HTTP POST to `<base_url>/`
///   with `Content-Type: application/json`.
/// - Responses: HTTP POST returns the JSON-RPC response directly (for requests)
///   OR the server uses SSE for server-initiated messages.
/// - SSE stream: GET `<base_url>/sse` with `Accept: text/event-stream` for
///   server-push notifications and streaming responses.
///
/// This implementation buffers incoming SSE messages so that `receive()` can
/// be called after `send()` in the standard request-response pattern used by
/// `MCPClient`. A background task reads the SSE stream and places parsed
/// messages into a channel.
pub struct HttpTransport {
    base_url: String,
    client: reqwest::Client,
    config: HttpTransportConfig,
    /// Session ID negotiated during the SSE handshake (`Mcp-Session-Id` header).
    session_id: Option<String>,
    /// Buffered incoming messages from the SSE stream or direct POST responses.
    incoming: tokio::sync::mpsc::UnboundedReceiver<JsonRpcMessage>,
    /// Sender half — kept alive so the channel stays open.
    incoming_tx: tokio::sync::mpsc::UnboundedSender<JsonRpcMessage>,
    /// Whether the SSE background listener has been started.
    sse_started: bool,
    /// Handle to the SSE listener task for cancellation on close.
    sse_task: Option<tokio::task::JoinHandle<()>>,
}

impl HttpTransport {
    /// Create a new HTTP transport.
    ///
    /// Call `connect_sse()` before using if the server requires SSE for responses.
    pub fn new(base_url: &str) -> Self {
        Self::with_config(base_url, HttpTransportConfig::default())
    }

    /// Create a new HTTP transport with authentication configuration.
    pub fn with_config(base_url: &str, config: HttpTransportConfig) -> Self {
        let client = reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(30))
            .connect_timeout(std::time::Duration::from_secs(10))
            .build()
            .expect("failed to build HTTP client for MCP transport");

        let (tx, rx) = tokio::sync::mpsc::unbounded_channel();
        Self {
            base_url: base_url.trim_end_matches('/').to_string(),
            client,
            config,
            session_id: None,
            incoming: rx,
            incoming_tx: tx,
            sse_started: false,
            sse_task: None,
        }
    }

    /// Set (or replace) the bearer token after construction.
    pub fn set_bearer_token(&mut self, token: impl Into<String>) {
        self.config.bearer_token = Some(token.into());
    }

    /// Start a background SSE listener that pushes incoming messages into the
    /// internal channel. Must be called before `receive()` if the server streams
    /// responses over SSE rather than returning them in the POST response body.
    pub async fn connect_sse(&mut self) -> Result<()> {
        if self.sse_started {
            return Ok(());
        }

        let sse_url = format!("{}/sse", self.base_url);
        let mut req = self
            .client
            .get(&sse_url)
            .header(ACCEPT, "text/event-stream");

        if let Some(token) = &self.config.bearer_token {
            req = req.header(AUTHORIZATION, format!("Bearer {token}"));
        }
        for (k, v) in &self.config.headers {
            req = req.header(k.as_str(), v.as_str());
        }
        if let Some(sid) = &self.session_id {
            req = req.header("Mcp-Session-Id", sid.as_str());
        }

        let response = req
            .send()
            .await
            .map_err(|e| AvaError::ToolError(format!("MCP SSE connect failed: {e}")))?;

        let status = response.status();
        if !status.is_success() {
            let body = response.text().await.unwrap_or_default();
            return Err(AvaError::ToolError(format!(
                "MCP SSE endpoint returned {status}: {}",
                &body[..body.len().min(300)]
            )));
        }

        // Capture session ID from response headers if server set one
        if let Some(sid) = response.headers().get("Mcp-Session-Id") {
            if let Ok(s) = sid.to_str() {
                self.session_id = Some(s.to_string());
            }
        }

        let tx = self.incoming_tx.clone();
        let stream = response.bytes_stream();

        let task = tokio::spawn(async move {
            parse_sse_stream(stream, tx).await;
        });

        self.sse_task = Some(task);
        self.sse_started = true;
        Ok(())
    }

    /// Build a POST request to the MCP endpoint with auth headers applied.
    fn build_post(&self, body: String) -> reqwest::RequestBuilder {
        let mut req = self
            .client
            .post(&self.base_url)
            .header(CONTENT_TYPE, "application/json")
            .header(ACCEPT, "application/json, text/event-stream")
            .body(body);

        if let Some(token) = &self.config.bearer_token {
            req = req.header(AUTHORIZATION, format!("Bearer {token}"));
        }
        for (k, v) in &self.config.headers {
            req = req.header(k.as_str(), v.as_str());
        }
        if let Some(sid) = &self.session_id {
            req = req.header("Mcp-Session-Id", sid.as_str());
        }
        req
    }
}

/// Parse an SSE byte stream, extracting `data:` lines and pushing parsed
/// JSON-RPC messages into `tx`.
///
/// SSE format (RFC 8607):
/// ```text
/// data: {"jsonrpc":"2.0","id":1,"result":{...}}\n\n
/// ```
/// Each event is separated by a blank line. Lines starting with `:` are
/// comment/heartbeat lines and are ignored. Lines starting with `data: `
/// contain the event payload. A `data: [DONE]` sentinel ends the stream.
async fn parse_sse_stream(
    stream: impl futures::Stream<Item = std::result::Result<bytes::Bytes, reqwest::Error>> + Unpin,
    tx: tokio::sync::mpsc::UnboundedSender<JsonRpcMessage>,
) {
    let mut stream = Box::pin(stream);
    let mut buf = String::new();
    let mut event_data = String::new();

    while let Some(chunk) = stream.next().await {
        let bytes = match chunk {
            Ok(b) => b,
            Err(e) => {
                tracing::debug!("MCP SSE stream error: {e}");
                break;
            }
        };

        let Ok(text) = std::str::from_utf8(&bytes) else {
            continue;
        };

        buf.push_str(text);

        // Process complete lines
        while let Some(newline_pos) = buf.find('\n') {
            let line = buf[..newline_pos].trim_end_matches('\r').to_string();
            buf.drain(..=newline_pos);

            if line.is_empty() {
                // Blank line = end of SSE event
                if !event_data.is_empty() {
                    let data = event_data.trim();
                    if data != "[DONE]" {
                        match serde_json::from_str::<JsonRpcMessage>(data) {
                            Ok(msg) => {
                                if tx.send(msg).is_err() {
                                    return; // receiver dropped
                                }
                            }
                            Err(e) => {
                                tracing::debug!(
                                    "MCP SSE: failed to parse event data: {e} — data={data}"
                                );
                            }
                        }
                    }
                    event_data.clear();
                }
            } else if let Some(data) = line.strip_prefix("data:") {
                let stripped = data.trim_start();
                if !event_data.is_empty() {
                    event_data.push('\n');
                }
                event_data.push_str(stripped);
            } else if line.starts_with(':')
                || line.starts_with("event:")
                || line.starts_with("id:")
                || line.starts_with("retry:")
            {
                // Comment / event type / ID / retry — ignore for MCP purposes
            }
        }
    }

    tracing::debug!("MCP SSE stream ended");
}

#[async_trait]
impl MCPTransport for HttpTransport {
    async fn send(&mut self, message: &JsonRpcMessage) -> Result<()> {
        let payload = serde_json::to_string(message)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let response = self
            .build_post(payload)
            .send()
            .await
            .map_err(|e| AvaError::ToolError(format!("MCP HTTP request failed: {e}")))?;

        let status = response.status();

        // 401 → token expired; caller should refresh and retry
        if status == reqwest::StatusCode::UNAUTHORIZED {
            return Err(AvaError::ToolError(
                "MCP server returned 401 Unauthorized — refresh OAuth token and retry".to_string(),
            ));
        }

        if !status.is_success() {
            let body = response.text().await.unwrap_or_default();
            return Err(AvaError::ToolError(format!(
                "MCP server returned {status}: {}",
                &body[..body.len().min(500)]
            )));
        }

        // Capture session ID if server set one
        if let Some(sid) = response.headers().get("Mcp-Session-Id") {
            if let Ok(s) = sid.to_str() {
                self.session_id = Some(s.to_string());
            }
        }

        let content_type = response
            .headers()
            .get(reqwest::header::CONTENT_TYPE)
            .and_then(|v| v.to_str().ok())
            .unwrap_or("")
            .to_string();

        if content_type.contains("text/event-stream") {
            // Server responded with an inline SSE stream for this request
            // (Streamable HTTP pattern). Parse it and buffer messages.
            let tx = self.incoming_tx.clone();
            let stream = response.bytes_stream();
            parse_sse_stream(stream, tx).await;
        } else if content_type.contains("application/json") || !content_type.is_empty() {
            // Direct JSON response — parse and buffer it
            let body = response
                .text()
                .await
                .map_err(|e| AvaError::ToolError(format!("MCP HTTP read body failed: {e}")))?;

            // The server may return nothing for notifications (202/204)
            if !body.trim().is_empty() {
                let msg: JsonRpcMessage = serde_json::from_str(&body).map_err(|e| {
                    AvaError::SerializationError(format!("MCP HTTP response parse failed: {e}"))
                })?;
                let _ = self.incoming_tx.send(msg);
            }
        }
        // else: 202 Accepted with no body — response will arrive via SSE listener

        Ok(())
    }

    async fn receive(&mut self) -> Result<JsonRpcMessage> {
        // Try to receive from the buffered channel with a timeout
        tokio::time::timeout(std::time::Duration::from_secs(90), self.incoming.recv())
            .await
            .map_err(|_| AvaError::ToolError("MCP HTTP receive timed out after 90s".to_string()))?
            .ok_or_else(|| AvaError::ToolError("MCP HTTP transport channel closed".to_string()))
    }

    async fn close(&mut self) -> Result<()> {
        if let Some(task) = self.sse_task.take() {
            task.abort();
        }
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
            .map_err(|e| AvaError::ToolError(format!("transport channel closed: {e}")))
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
        let payload = serde_json::to_string(&value)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;
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
            .ok_or_else(|| AvaError::ValidationError("missing content-length header".to_string()))?
            .parse::<usize>()
            .map_err(|e| {
                AvaError::ValidationError(format!("invalid content-length header: {e}"))
            })?;

        let mut body = vec![0_u8; len];
        self.reader
            .read_exact(&mut body)
            .await
            .map_err(AvaError::from)?;

        let body =
            String::from_utf8(body).map_err(|e| AvaError::SerializationError(e.to_string()))?;
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
        // Use `cat` as an echo server — we write NDJSON and read it back.
        // StdioTransport uses newline-delimited JSON (NDJSON), matching the
        // official MCP SDK protocol. `cat` echoes the line back unchanged,
        // so we can verify the round-trip without a full MCP server.
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

    #[tokio::test]
    async fn stdio_transport_with_playwright_mcp() {
        // Integration test: spawn the real Playwright MCP server and perform
        // the MCP initialize handshake. The server must respond within 5 seconds.
        //
        // This test verifies that StdioTransport uses NDJSON (not Content-Length
        // framing), since @playwright/mcp uses @modelcontextprotocol/sdk which
        // reads one JSON object per line.
        let node_path = which_node();
        let playwright_cli =
            "/home/xn3/.nvm/versions/node/v24.13.1/lib/node_modules/@playwright/mcp/cli.js";

        if node_path.is_none() || !std::path::Path::new(playwright_cli).exists() {
            eprintln!("skipping playwright MCP test: node or @playwright/mcp not available");
            return;
        }

        let node = node_path.unwrap();
        let args = vec![playwright_cli.to_string(), "--headless".to_string()];

        let transport = StdioTransport::spawn(&node, &args, &HashMap::new())
            .await
            .expect("failed to spawn Playwright MCP server");

        let mut client = crate::client::MCPClient::new(Box::new(transport), "playwright");

        let result =
            tokio::time::timeout(std::time::Duration::from_secs(5), client.initialize()).await;

        match result {
            Err(_elapsed) => panic!("Playwright MCP initialize timed out after 5s"),
            Ok(Err(e)) => panic!("Playwright MCP initialize failed: {e}"),
            Ok(Ok(caps)) => {
                assert!(
                    caps.tools,
                    "Playwright MCP should advertise tools capability"
                );
            }
        }

        client.disconnect().await.ok();
    }

    fn which_node() -> Option<String> {
        // Try the known nvm path first, then fall back to PATH lookup
        let known = "/home/xn3/.nvm/versions/node/v24.13.1/bin/node";
        if std::path::Path::new(known).exists() {
            return Some(known.to_string());
        }
        std::process::Command::new("which")
            .arg("node")
            .output()
            .ok()
            .and_then(|o| {
                if o.status.success() {
                    String::from_utf8(o.stdout)
                        .ok()
                        .map(|s| s.trim().to_string())
                } else {
                    None
                }
            })
    }

    #[tokio::test]
    async fn http_transport_send_returns_error_on_connection_refused() {
        // Sending to a port that refuses connections must fail with a transport error,
        // not a silent discard.
        let mut transport = HttpTransport::new("http://127.0.0.1:19999");
        let msg = JsonRpcMessage::request(1, "test", serde_json::json!({}));
        let result = transport.send(&msg).await;
        assert!(
            result.is_err(),
            "HttpTransport::send must return Err on network failure"
        );
        let err_msg = result.unwrap_err().to_string();
        // Either "connection refused" or a timeout/network message
        assert!(
            err_msg.contains("request failed")
                || err_msg.contains("connect")
                || err_msg.contains("refused")
                || err_msg.contains("HTTP request"),
            "Error should describe the network failure: {err_msg}"
        );
    }

    #[tokio::test]
    async fn http_transport_receive_times_out_with_no_messages() {
        // With no SSE listener and no buffered messages, receive() should eventually
        // time out. We test with a very short timeout by using `tokio::time::timeout`.
        let mut transport = HttpTransport::new("http://127.0.0.1:19999");
        let result =
            tokio::time::timeout(std::time::Duration::from_millis(100), transport.receive()).await;
        // We expect either a timeout or a channel-closed error
        assert!(result.is_err() || result.unwrap().is_err());
    }

    #[test]
    fn http_transport_with_config_applies_bearer_token() {
        let config = HttpTransportConfig {
            bearer_token: Some("my-secret-token".to_string()),
            headers: HashMap::new(),
        };
        let transport = HttpTransport::with_config("https://example.com/mcp", config);
        assert_eq!(
            transport.config.bearer_token.as_deref(),
            Some("my-secret-token")
        );
        assert_eq!(transport.base_url, "https://example.com/mcp");
    }

    #[test]
    fn http_transport_trims_trailing_slash() {
        let transport = HttpTransport::new("https://example.com/mcp/");
        assert_eq!(transport.base_url, "https://example.com/mcp");
    }
}
