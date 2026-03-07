use std::sync::atomic::{AtomicU64, Ordering};

use ava_types::{AvaError, Result, Tool, ToolCall};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::transport::{JsonRpcMessage, MCPTransport};

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
// MCPClient — communicates with a single MCP server
// ---------------------------------------------------------------------------

pub struct MCPClient {
    transport: Box<dyn MCPTransport>,
    server_capabilities: Option<ServerCapabilities>,
    request_id: AtomicU64,
    server_name: String,
}

impl MCPClient {
    /// Create a new client wrapping the given transport.
    pub fn new(transport: Box<dyn MCPTransport>, server_name: &str) -> Self {
        Self {
            transport,
            server_capabilities: None,
            request_id: AtomicU64::new(1),
            server_name: server_name.to_string(),
        }
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
        let response = self.transport.receive().await?;

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
        let response = self.transport.receive().await?;

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
        let response = self.transport.receive().await?;

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
}
