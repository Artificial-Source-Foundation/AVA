use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Instant;

use ava_types::{AvaError, Result, Tool, ToolCall};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tracing::warn;

use crate::transport::{JsonRpcMessage, MCPTransport};

// ---------------------------------------------------------------------------
// Connection health tracking (F40)
// ---------------------------------------------------------------------------

/// Terminal error patterns that indicate the connection is broken and
/// a reconnect should be attempted.
const TERMINAL_ERROR_PATTERNS: &[&str] = &[
    "connection reset",
    "timed out",
    "broken pipe",
    "connection refused",
    "host unreachable",
];

/// Number of consecutive terminal errors before triggering reconnect.
const RECONNECT_THRESHOLD: u32 = 3;

/// Connection health tracker for an MCP client.
#[derive(Debug)]
pub struct ConnectionHealth {
    consecutive_errors: u32,
    last_error_time: Option<Instant>,
}

impl ConnectionHealth {
    pub fn new() -> Self {
        Self {
            consecutive_errors: 0,
            last_error_time: None,
        }
    }

    /// Record an error and return `true` if a reconnect is needed.
    ///
    /// Only terminal errors (connection reset, timeout, broken pipe, etc.)
    /// count toward the reconnect threshold. Non-terminal errors (e.g.,
    /// application-level errors) are ignored.
    pub fn record_error(&mut self, error: &str) -> bool {
        let lower = error.to_lowercase();
        let is_terminal = TERMINAL_ERROR_PATTERNS
            .iter()
            .any(|pattern| lower.contains(pattern));

        if !is_terminal {
            return false;
        }

        self.consecutive_errors += 1;
        self.last_error_time = Some(Instant::now());

        let needs_reconnect = self.consecutive_errors >= RECONNECT_THRESHOLD;
        if needs_reconnect {
            warn!(
                consecutive_errors = self.consecutive_errors,
                error = %error,
                "MCP connection needs reconnect after {} consecutive terminal errors",
                self.consecutive_errors
            );
        }
        needs_reconnect
    }

    /// Record a successful operation, resetting the error counter.
    pub fn record_success(&mut self) {
        self.consecutive_errors = 0;
        self.last_error_time = None;
    }

    /// Current number of consecutive terminal errors.
    pub fn consecutive_errors(&self) -> u32 {
        self.consecutive_errors
    }

    /// Time of the last recorded terminal error, if any.
    pub fn last_error_time(&self) -> Option<Instant> {
        self.last_error_time
    }
}

impl Default for ConnectionHealth {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// Server capabilities returned by the MCP server on initialize
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ServerCapabilities {
    #[serde(default)]
    pub tools: bool,
    #[serde(default)]
    pub resources: bool,
    #[serde(default)]
    pub prompts: bool,
}

// ---------------------------------------------------------------------------
// Tool definition returned by tools/list
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPTool {
    pub name: String,
    #[serde(default)]
    pub description: String,
    /// JSON Schema for the tool's input parameters.
    #[serde(rename = "inputSchema", default)]
    pub parameters: Value,
}

// ---------------------------------------------------------------------------
// Resource types returned by resources/list and resources/read
// ---------------------------------------------------------------------------

/// A resource available on an MCP server (file, doc, database record, etc.).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPResource {
    /// Unique URI identifying the resource (e.g., "file:///path/to/file").
    pub uri: String,
    /// Human-readable name for the resource.
    #[serde(default)]
    pub name: String,
    /// Optional description of the resource.
    #[serde(default)]
    pub description: String,
    /// MIME type of the resource content (e.g., "text/plain", "application/json").
    #[serde(rename = "mimeType", default)]
    pub mime_type: String,
}

/// A single content block in a resource read response.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPResourceContent {
    /// The URI of the resource that was read.
    pub uri: String,
    /// MIME type of this content block.
    #[serde(rename = "mimeType", default)]
    pub mime_type: String,
    /// Text content (present when mimeType is text/*, application/json, etc.).
    #[serde(default)]
    pub text: Option<String>,
    /// Binary content as base64 (present when mimeType is binary).
    #[serde(default)]
    pub blob: Option<String>,
}

// ---------------------------------------------------------------------------
// Prompt types returned by prompts/list and prompts/get
// ---------------------------------------------------------------------------

/// A prompt template available on an MCP server.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPPrompt {
    /// Unique name for the prompt.
    pub name: String,
    /// Human-readable description of what the prompt does.
    #[serde(default)]
    pub description: String,
    /// Input arguments accepted by this prompt template.
    #[serde(default)]
    pub arguments: Vec<MCPPromptArgument>,
}

/// An argument accepted by a prompt template.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPPromptArgument {
    pub name: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub required: bool,
}

/// The result of a prompts/get request — the rendered prompt messages.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPPromptResult {
    /// Optional description of this prompt invocation.
    #[serde(default)]
    pub description: String,
    /// The rendered prompt messages ready to inject into a conversation.
    pub messages: Vec<MCPPromptMessage>,
}

/// A single message in a rendered prompt.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPPromptMessage {
    /// "user" or "assistant".
    pub role: String,
    /// The content of the message (text or embedded resource).
    pub content: MCPPromptContent,
}

/// Content within a prompt message — text or an embedded resource.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "lowercase")]
pub enum MCPPromptContent {
    Text {
        text: String,
    },
    Resource {
        resource: MCPResourceContent,
    },
    #[serde(other)]
    Unknown,
}

// ---------------------------------------------------------------------------
// MCPClient — communicates with a single MCP server
// ---------------------------------------------------------------------------

pub struct MCPClient {
    transport: Box<dyn MCPTransport>,
    server_capabilities: Option<ServerCapabilities>,
    request_id: AtomicU64,
    server_name: String,
    health: ConnectionHealth,
}

impl MCPClient {
    /// Create a new client wrapping the given transport.
    pub fn new(transport: Box<dyn MCPTransport>, server_name: &str) -> Self {
        Self {
            transport,
            server_capabilities: None,
            request_id: AtomicU64::new(1),
            server_name: server_name.to_string(),
            health: ConnectionHealth::new(),
        }
    }

    /// Access the connection health tracker.
    pub fn health(&self) -> &ConnectionHealth {
        &self.health
    }

    /// Record an MCP error and return `true` if reconnect is needed.
    pub fn record_mcp_error(&mut self, error: &str) -> bool {
        self.health.record_error(error)
    }

    /// Record a successful MCP operation.
    pub fn record_mcp_success(&mut self) {
        self.health.record_success();
    }

    /// Send the MCP `initialize` handshake and return server capabilities.
    pub async fn initialize(&mut self) -> Result<ServerCapabilities> {
        let id = self.next_id();
        let request = JsonRpcMessage::request(
            id,
            "initialize",
            json!({
                "protocolVersion": "2024-11-05",
                "capabilities": {
                    "roots": { "listChanged": true }
                },
                "clientInfo": {
                    "name": "ava",
                    "version": "0.1.0"
                }
            }),
        );

        self.transport.send(&request).await?;
        let response = self.receive_response(id).await?;

        if let Some(err) = &response.error {
            return Err(AvaError::ToolError(format!(
                "MCP server '{}' initialize failed: {}",
                self.server_name, err.message
            )));
        }

        let result = response.result.ok_or_else(|| {
            AvaError::ToolError(format!(
                "MCP server '{}' returned no result for initialize. \
                 The server may not implement the MCP protocol correctly",
                self.server_name
            ))
        })?;

        // Parse capabilities from the result
        let caps = if let Some(caps_value) = result.get("capabilities") {
            let tools = caps_value
                .get("tools")
                .map(|v| !v.is_null())
                .unwrap_or(false);
            let resources = caps_value
                .get("resources")
                .map(|v| !v.is_null())
                .unwrap_or(false);
            let prompts = caps_value
                .get("prompts")
                .map(|v| !v.is_null())
                .unwrap_or(false);
            ServerCapabilities {
                tools,
                resources,
                prompts,
            }
        } else {
            ServerCapabilities::default()
        };

        // Send `initialized` notification (no id, no response expected)
        let notification = JsonRpcMessage::notification("notifications/initialized", json!({}));
        self.transport.send(&notification).await?;

        self.server_capabilities = Some(caps.clone());
        Ok(caps)
    }

    /// List available tools from the server.
    pub async fn list_tools(&mut self) -> Result<Vec<MCPTool>> {
        let id = self.next_id();
        let request = JsonRpcMessage::request(id, "tools/list", json!({}));
        self.transport.send(&request).await?;
        let response = self.receive_response(id).await?;

        if let Some(err) = &response.error {
            return Err(AvaError::ToolError(format!(
                "MCP server '{}' tools/list failed: {}",
                self.server_name, err.message
            )));
        }

        let result = response.result.ok_or_else(|| {
            AvaError::SerializationError("missing result in tools/list response".to_string())
        })?;

        let tools_value = result.get("tools").cloned().ok_or_else(|| {
            AvaError::SerializationError("missing tools array in tools/list response".to_string())
        })?;

        serde_json::from_value::<Vec<MCPTool>>(tools_value)
            .map_err(|e| AvaError::SerializationError(e.to_string()))
    }

    /// Poll for an incoming notification from the server without sending a request.
    ///
    /// Returns `Some(method)` if a notification was available, `None` if the transport
    /// had no pending message. This is a non-blocking best-effort check: callers should
    /// use it between request/response cycles, not concurrently with other send/receive
    /// calls on the same client.
    ///
    /// The only notification currently acted on by AVA is
    /// `notifications/tools/list_changed`, which triggers a `list_tools` re-fetch.
    pub async fn poll_notification(&mut self) -> Option<String> {
        // The MCP spec allows servers to send notifications at any time.
        // We use a short-timeout receive here so callers don't block.
        use tokio::time::{timeout, Duration};
        match timeout(Duration::from_millis(1), self.transport.receive()).await {
            Ok(Ok(msg)) if msg.id.is_none() => {
                // No id → this is a notification (not a response to a request).
                msg.method
            }
            _ => None,
        }
    }

    /// List available resources from the server.
    ///
    /// MCP method: `resources/list`
    /// Returns an empty list when the server does not advertise resource support.
    pub async fn list_resources(&mut self) -> Result<Vec<MCPResource>> {
        let id = self.next_id();
        let request = JsonRpcMessage::request(id, "resources/list", json!({}));
        self.transport.send(&request).await?;
        let response = self.receive_response(id).await?;

        if let Some(err) = &response.error {
            return Err(AvaError::ToolError(format!(
                "MCP server '{}' resources/list failed: {}",
                self.server_name, err.message
            )));
        }

        let result = response.result.ok_or_else(|| {
            AvaError::SerializationError("missing result in resources/list response".to_string())
        })?;

        let resources_value = result.get("resources").cloned().unwrap_or(json!([]));
        serde_json::from_value::<Vec<MCPResource>>(resources_value)
            .map_err(|e| AvaError::SerializationError(e.to_string()))
    }

    /// Read the content of a specific resource by URI.
    ///
    /// MCP method: `resources/read`
    pub async fn read_resource(&mut self, uri: &str) -> Result<Vec<MCPResourceContent>> {
        let id = self.next_id();
        let request = JsonRpcMessage::request(id, "resources/read", json!({ "uri": uri }));
        self.transport.send(&request).await?;
        let response = self.receive_response(id).await?;

        if let Some(err) = &response.error {
            return Err(AvaError::ToolError(format!(
                "MCP server '{}' resources/read failed for '{}': {}",
                self.server_name, uri, err.message
            )));
        }

        let result = response.result.ok_or_else(|| {
            AvaError::SerializationError("missing result in resources/read response".to_string())
        })?;

        let contents_value = result.get("contents").cloned().unwrap_or(json!([]));
        serde_json::from_value::<Vec<MCPResourceContent>>(contents_value)
            .map_err(|e| AvaError::SerializationError(e.to_string()))
    }

    /// List available prompt templates from the server.
    ///
    /// MCP method: `prompts/list`
    /// Returns an empty list when the server does not advertise prompt support.
    pub async fn list_prompts(&mut self) -> Result<Vec<MCPPrompt>> {
        let id = self.next_id();
        let request = JsonRpcMessage::request(id, "prompts/list", json!({}));
        self.transport.send(&request).await?;
        let response = self.receive_response(id).await?;

        if let Some(err) = &response.error {
            return Err(AvaError::ToolError(format!(
                "MCP server '{}' prompts/list failed: {}",
                self.server_name, err.message
            )));
        }

        let result = response.result.ok_or_else(|| {
            AvaError::SerializationError("missing result in prompts/list response".to_string())
        })?;

        let prompts_value = result.get("prompts").cloned().unwrap_or(json!([]));
        serde_json::from_value::<Vec<MCPPrompt>>(prompts_value)
            .map_err(|e| AvaError::SerializationError(e.to_string()))
    }

    /// Retrieve and render a specific prompt template with the provided arguments.
    ///
    /// MCP method: `prompts/get`
    ///
    /// `arguments` is a map of argument names to string values, passed directly
    /// to the MCP server for template interpolation.
    pub async fn get_prompt(&mut self, name: &str, arguments: Value) -> Result<MCPPromptResult> {
        let id = self.next_id();
        let request = JsonRpcMessage::request(
            id,
            "prompts/get",
            json!({
                "name": name,
                "arguments": arguments
            }),
        );
        self.transport.send(&request).await?;
        let response = self.receive_response(id).await?;

        if let Some(err) = &response.error {
            return Err(AvaError::ToolError(format!(
                "MCP server '{}' prompts/get failed for '{}': {}",
                self.server_name, name, err.message
            )));
        }

        let result = response.result.ok_or_else(|| {
            AvaError::SerializationError("missing result in prompts/get response".to_string())
        })?;

        serde_json::from_value::<MCPPromptResult>(result)
            .map_err(|e| AvaError::SerializationError(e.to_string()))
    }

    /// Call a tool on the server and return the result.
    pub async fn call_tool(&mut self, name: &str, arguments: Value) -> Result<Value> {
        let id = self.next_id();
        let request = JsonRpcMessage::request(
            id,
            "tools/call",
            json!({
                "name": name,
                "arguments": arguments
            }),
        );

        self.transport.send(&request).await?;
        let response = self.receive_response(id).await?;

        if let Some(err) = &response.error {
            return Err(AvaError::ToolError(format!(
                "MCP tool '{}' on server '{}' failed: {}",
                name, self.server_name, err.message
            )));
        }

        response.result.ok_or_else(|| {
            AvaError::SerializationError(format!(
                "missing result in tools/call response for tool '{name}'"
            ))
        })
    }

    /// Disconnect from the server.
    pub async fn disconnect(&mut self) -> Result<()> {
        self.transport.close().await
    }

    /// The server name this client is connected to.
    pub fn server_name(&self) -> &str {
        &self.server_name
    }

    /// The server capabilities, if initialize has been called.
    pub fn capabilities(&self) -> Option<&ServerCapabilities> {
        self.server_capabilities.as_ref()
    }

    /// Receive the response for a specific request ID, skipping any
    /// notifications (messages without an ID) that the server may send
    /// between the request and its response (e.g. progress, log events).
    async fn receive_response(&mut self, expected_id: u64) -> Result<JsonRpcMessage> {
        let expected_value = Value::Number(expected_id.into());
        loop {
            let msg = self.transport.receive().await?;
            match &msg.id {
                Some(id) if *id == expected_value => return Ok(msg),
                Some(_id) => {
                    // Response for a different request — skip (shouldn't happen
                    // in single-threaded usage but be defensive).
                    tracing::debug!(
                        server = %self.server_name,
                        expected = expected_id,
                        "skipping MCP response with unexpected ID"
                    );
                }
                None => {
                    // Notification (no ID) — skip silently. MCP servers send
                    // these for progress updates, log events, etc.
                    tracing::trace!(
                        server = %self.server_name,
                        method = msg.method.as_deref().unwrap_or("?"),
                        "skipping MCP notification"
                    );
                }
            }
        }
    }

    fn next_id(&self) -> u64 {
        self.request_id.fetch_add(1, Ordering::Relaxed)
    }
}

// ---------------------------------------------------------------------------
// Helper: parse tool call from a JSON-RPC request params (used by server)
// ---------------------------------------------------------------------------

pub fn tool_call_from_request(id: &str, params: &Value) -> Result<ToolCall> {
    let name = params
        .get("name")
        .and_then(Value::as_str)
        .ok_or_else(|| AvaError::ValidationError("missing tool name".to_string()))?;

    Ok(ToolCall {
        id: id.to_string(),
        name: name.to_string(),
        arguments: params
            .get("arguments")
            .cloned()
            .unwrap_or_else(|| json!({})),
    })
}

// ---------------------------------------------------------------------------
// Helper: convert MCPTool to ava_types::Tool
// ---------------------------------------------------------------------------

impl MCPTool {
    /// Convert to the AVA tool definition type.
    pub fn to_ava_tool(&self) -> Tool {
        Tool {
            name: self.name.clone(),
            description: self.description.clone(),
            parameters: self.parameters.clone(),
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::transport::InMemoryTransport;

    /// Simulates an MCP server on the other end of an InMemoryTransport.
    async fn mock_server(mut transport: InMemoryTransport) {
        // 1. Receive initialize
        let init_req = transport.receive().await.unwrap();
        assert_eq!(init_req.method.as_deref(), Some("initialize"));
        let init_resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: init_req.id.clone(),
            method: None,
            params: None,
            result: Some(json!({
                "protocolVersion": "2024-11-05",
                "capabilities": {
                    "tools": {}
                },
                "serverInfo": {
                    "name": "mock-server",
                    "version": "1.0.0"
                }
            })),
            error: None,
        };
        transport.send(&init_resp).await.unwrap();

        // 2. Receive initialized notification
        let _notif = transport.receive().await.unwrap();

        // 3. Receive tools/list
        let list_req = transport.receive().await.unwrap();
        assert_eq!(list_req.method.as_deref(), Some("tools/list"));
        let list_resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: list_req.id.clone(),
            method: None,
            params: None,
            result: Some(json!({
                "tools": [
                    {
                        "name": "get_weather",
                        "description": "Get the weather for a city",
                        "inputSchema": {
                            "type": "object",
                            "properties": {
                                "city": { "type": "string" }
                            },
                            "required": ["city"]
                        }
                    }
                ]
            })),
            error: None,
        };
        transport.send(&list_resp).await.unwrap();

        // 4. Receive tools/call
        let call_req = transport.receive().await.unwrap();
        assert_eq!(call_req.method.as_deref(), Some("tools/call"));
        let call_resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: call_req.id.clone(),
            method: None,
            params: None,
            result: Some(json!({
                "content": [
                    {
                        "type": "text",
                        "text": "Sunny, 72F"
                    }
                ],
                "isError": false
            })),
            error: None,
        };
        transport.send(&call_resp).await.unwrap();
    }

    #[tokio::test]
    async fn client_protocol_flow() {
        let (client_transport, server_transport) = InMemoryTransport::pair();

        let server_handle = tokio::spawn(mock_server(server_transport));

        let mut client = MCPClient::new(Box::new(client_transport), "test-server");

        // Initialize
        let caps = client.initialize().await.unwrap();
        assert!(caps.tools);

        // List tools
        let tools = client.list_tools().await.unwrap();
        assert_eq!(tools.len(), 1);
        assert_eq!(tools[0].name, "get_weather");
        assert_eq!(tools[0].description, "Get the weather for a city");

        // Call tool
        let result = client
            .call_tool("get_weather", json!({"city": "London"}))
            .await
            .unwrap();
        assert!(result.get("content").is_some());

        client.disconnect().await.unwrap();
        server_handle.await.unwrap();
    }

    #[tokio::test]
    async fn client_handles_server_error() {
        let (client_transport, mut server_transport) = InMemoryTransport::pair();

        let server_handle = tokio::spawn(async move {
            let req = server_transport.receive().await.unwrap();
            let resp = JsonRpcMessage {
                jsonrpc: "2.0".to_string(),
                id: req.id.clone(),
                method: None,
                params: None,
                result: None,
                error: Some(crate::transport::JsonRpcError {
                    code: -32600,
                    message: "bad request".to_string(),
                    data: None,
                }),
            };
            server_transport.send(&resp).await.unwrap();
        });

        let mut client = MCPClient::new(Box::new(client_transport), "error-server");
        let result = client.initialize().await;
        assert!(result.is_err());
        let err_msg = result.unwrap_err().to_string();
        assert!(err_msg.contains("bad request"), "got: {err_msg}");

        server_handle.await.unwrap();
    }

    #[test]
    fn mcp_tool_to_ava_tool() {
        let mcp_tool = MCPTool {
            name: "test_tool".to_string(),
            description: "A test tool".to_string(),
            parameters: json!({"type": "object"}),
        };
        let ava_tool = mcp_tool.to_ava_tool();
        assert_eq!(ava_tool.name, "test_tool");
        assert_eq!(ava_tool.description, "A test tool");
    }

    #[test]
    fn tool_call_from_request_parses() {
        let params = json!({"name": "foo", "arguments": {"bar": 1}});
        let call = tool_call_from_request("id-1", &params).unwrap();
        assert_eq!(call.name, "foo");
        assert_eq!(call.arguments, json!({"bar": 1}));
    }

    // --- F40: Connection health tracking tests ---

    #[test]
    fn connection_health_three_errors_triggers_reconnect() {
        let mut health = ConnectionHealth::new();
        assert!(!health.record_error("connection reset by peer"));
        assert_eq!(health.consecutive_errors(), 1);
        assert!(!health.record_error("connection reset"));
        assert_eq!(health.consecutive_errors(), 2);
        assert!(health.record_error("connection reset"));
        assert_eq!(health.consecutive_errors(), 3);
        assert!(health.last_error_time().is_some());
    }

    #[test]
    fn connection_health_success_resets_counter() {
        let mut health = ConnectionHealth::new();
        health.record_error("timed out");
        health.record_error("broken pipe");
        assert_eq!(health.consecutive_errors(), 2);

        health.record_success();
        assert_eq!(health.consecutive_errors(), 0);
        assert!(health.last_error_time().is_none());

        // Should need 3 more errors now
        assert!(!health.record_error("timed out"));
        assert!(!health.record_error("timed out"));
        assert!(health.record_error("timed out"));
    }

    #[test]
    fn connection_health_non_terminal_errors_ignored() {
        let mut health = ConnectionHealth::new();
        assert!(!health.record_error("invalid JSON response"));
        assert!(!health.record_error("tool not found"));
        assert!(!health.record_error("permission denied"));
        assert!(!health.record_error("rate limited"));
        assert_eq!(health.consecutive_errors(), 0);
    }

    #[test]
    fn connection_health_terminal_patterns() {
        let mut health = ConnectionHealth::new();

        // Each pattern should count
        for pattern in &[
            "connection reset",
            "timed out",
            "broken pipe",
            "connection refused",
            "host unreachable",
        ] {
            let mut h = ConnectionHealth::new();
            assert!(!h.record_error(pattern));
            assert_eq!(h.consecutive_errors(), 1);
        }

        // Case insensitive
        assert!(!health.record_error("Connection Reset"));
        assert_eq!(health.consecutive_errors(), 1);
    }

    #[test]
    fn connection_health_mixed_errors() {
        let mut health = ConnectionHealth::new();
        assert!(!health.record_error("connection reset"));
        assert!(!health.record_error("invalid JSON")); // non-terminal, should NOT reset
        assert!(!health.record_error("broken pipe"));
        // 2 consecutive terminal errors, non-terminal didn't reset
        assert_eq!(health.consecutive_errors(), 2);
        assert!(health.record_error("timed out")); // 3rd terminal → reconnect
    }
}
