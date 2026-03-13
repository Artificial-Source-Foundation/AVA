use std::collections::HashMap;
use std::sync::Arc;

use ava_types::{AvaError, Result, ToolResult};
use serde_json::Value;
use tokio::sync::Mutex;
use tracing::{info, warn};

use crate::client::{MCPClient, MCPTool};
use crate::config::{MCPServerConfig, TransportType};
use crate::transport::{HttpTransport, StdioTransport};

// ---------------------------------------------------------------------------
// ExtensionManager — connects to MCP servers and aggregates their tools
// ---------------------------------------------------------------------------

pub struct ExtensionManager {
    /// MCP clients keyed by server name, each behind a Mutex for interior mutability.
    clients: HashMap<String, Arc<Mutex<MCPClient>>>,
    /// All discovered tools, each tagged with the server name that owns it.
    tools: Vec<(String, MCPTool)>,
}

impl ExtensionManager {
    /// Create a new extension manager (does not connect yet).
    pub fn new() -> Self {
        Self {
            clients: HashMap::new(),
            tools: Vec::new(),
        }
    }

    /// Connect to all enabled servers and discover their tools.
    pub async fn initialize(&mut self, configs: Vec<MCPServerConfig>) -> Result<()> {
        for config in configs {
            if !config.enabled {
                info!(server = %config.name, "MCP server disabled, skipping");
                continue;
            }

            match self.connect_server(&config).await {
                Ok(()) => {
                    info!(server = %config.name, "MCP server connected");
                }
                Err(e) => {
                    warn!(
                        server = %config.name,
                        error = %e,
                        "Failed to connect MCP server, skipping"
                    );
                }
            }
        }
        Ok(())
    }

    async fn connect_server(&mut self, config: &MCPServerConfig) -> Result<()> {
        let transport: Box<dyn crate::transport::MCPTransport> = match &config.transport {
            TransportType::Stdio { command, args, env } => {
                Box::new(StdioTransport::spawn(command, args, env).await?)
            }
            TransportType::Http { url } => Box::new(HttpTransport::new(url)),
        };

        let mut client = MCPClient::new(transport, &config.name);
        client.initialize().await?;

        let mcp_tools = client.list_tools().await?;
        for tool in &mcp_tools {
            self.tools.push((config.name.clone(), tool.clone()));
        }

        info!(
            server = %config.name,
            tool_count = mcp_tools.len(),
            "Discovered MCP tools"
        );

        self.clients
            .insert(config.name.clone(), Arc::new(Mutex::new(client)));
        Ok(())
    }

    /// Get all discovered tools as `ava_types::Tool` definitions.
    pub fn list_tools(&self) -> Vec<ava_types::Tool> {
        self.tools
            .iter()
            .map(|(_, tool)| tool.to_ava_tool())
            .collect()
    }

    /// Get all discovered tools with their server names.
    pub fn list_tools_with_server(&self) -> &[(String, MCPTool)] {
        &self.tools
    }

    /// Find which server owns a tool by name.
    pub fn server_for_tool(&self, tool_name: &str) -> Option<&str> {
        self.tools
            .iter()
            .find(|(_, t)| t.name == tool_name)
            .map(|(server, _)| server.as_str())
    }

    /// Execute a tool call, routing to the correct server.
    /// This method takes `&self` and uses interior mutability via client Mutexes.
    pub async fn call_tool(&self, name: &str, arguments: Value) -> Result<ToolResult> {
        let server_name = self
            .tools
            .iter()
            .find(|(_, t)| t.name == name)
            .map(|(s, _)| s.clone())
            .ok_or_else(|| AvaError::ToolNotFound {
                tool: name.to_string(),
                available: self
                    .tools
                    .iter()
                    .map(|(_, t)| t.name.as_str())
                    .collect::<Vec<_>>()
                    .join(", "),
            })?;

        let client = self.clients.get(&server_name).ok_or_else(|| {
            AvaError::ToolError(format!("MCP server '{server_name}' is not connected"))
        })?;

        let result = client.lock().await.call_tool(name, arguments).await?;

        // Parse the MCP result into a ToolResult
        let content = extract_text_content(&result);
        let is_error = result
            .get("isError")
            .and_then(Value::as_bool)
            .unwrap_or(false);

        Ok(ToolResult {
            call_id: format!("mcp-{server_name}-{name}"),
            content,
            is_error,
        })
    }

    /// Disconnect all servers.
    pub async fn shutdown(&mut self) -> Result<()> {
        for (name, client) in self.clients.drain() {
            if let Err(e) = client.lock().await.disconnect().await {
                warn!(server = %name, error = %e, "Error disconnecting MCP server");
            }
        }
        self.tools.clear();
        Ok(())
    }

    /// Number of connected servers.
    pub fn server_count(&self) -> usize {
        self.clients.len()
    }

    /// Number of discovered tools.
    pub fn tool_count(&self) -> usize {
        self.tools.len()
    }
}

impl Default for ExtensionManager {
    fn default() -> Self {
        Self::new()
    }
}

/// Extract text content from an MCP tools/call result.
///
/// MCP spec says result.content is an array of content blocks.
/// Each block has a "type" and "text" (for text blocks).
fn extract_text_content(result: &Value) -> String {
    if let Some(content_array) = result.get("content").and_then(Value::as_array) {
        content_array
            .iter()
            .filter_map(|block| {
                if block.get("type").and_then(Value::as_str) == Some("text") {
                    block.get("text").and_then(Value::as_str).map(String::from)
                } else {
                    // For non-text blocks, serialize them
                    Some(serde_json::to_string(block).unwrap_or_default())
                }
            })
            .collect::<Vec<_>>()
            .join("\n")
    } else if let Some(text) = result.get("content").and_then(Value::as_str) {
        // Some servers return content as a plain string
        text.to_string()
    } else {
        serde_json::to_string(result).unwrap_or_default()
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::transport::{InMemoryTransport, JsonRpcMessage, MCPTransport};
    use serde_json::json;

    /// Spawn a mock MCP server that responds to initialize, tools/list, and tools/call.
    async fn run_mock_server(mut transport: InMemoryTransport, tools: Vec<MCPTool>) {
        // initialize
        let req = transport.receive().await.unwrap();
        let resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: req.id.clone(),
            method: None,
            params: None,
            result: Some(json!({
                "protocolVersion": "2024-11-05",
                "capabilities": { "tools": {} },
                "serverInfo": { "name": "mock", "version": "1.0" }
            })),
            error: None,
        };
        transport.send(&resp).await.unwrap();

        // initialized notification
        let _notif = transport.receive().await.unwrap();

        // tools/list
        let req = transport.receive().await.unwrap();
        let tools_json: Vec<Value> = tools
            .iter()
            .map(|t| {
                json!({
                    "name": t.name,
                    "description": t.description,
                    "inputSchema": t.parameters
                })
            })
            .collect();
        let resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: req.id.clone(),
            method: None,
            params: None,
            result: Some(json!({ "tools": tools_json })),
            error: None,
        };
        transport.send(&resp).await.unwrap();

        // tools/call (loop to handle multiple calls)
        loop {
            let req = match transport.receive().await {
                Ok(r) => r,
                Err(_) => break,
            };

            let resp = JsonRpcMessage {
                jsonrpc: "2.0".to_string(),
                id: req.id.clone(),
                method: None,
                params: None,
                result: Some(json!({
                    "content": [{ "type": "text", "text": "mock result" }],
                    "isError": false
                })),
                error: None,
            };
            if transport.send(&resp).await.is_err() {
                break;
            }
        }
    }

    #[tokio::test]
    async fn extension_manager_with_mock_server() {
        let (client_transport, server_transport) = InMemoryTransport::pair();

        let tools = vec![
            MCPTool {
                name: "tool_a".to_string(),
                description: "Tool A".to_string(),
                parameters: json!({"type": "object"}),
            },
            MCPTool {
                name: "tool_b".to_string(),
                description: "Tool B".to_string(),
                parameters: json!({"type": "object"}),
            },
        ];

        let server_handle = tokio::spawn(run_mock_server(server_transport, tools));

        let mut manager = ExtensionManager::new();

        // Manually connect using the in-memory transport
        let mut client = MCPClient::new(Box::new(client_transport), "mock-server");
        client.initialize().await.unwrap();
        let mcp_tools = client.list_tools().await.unwrap();
        for tool in &mcp_tools {
            manager
                .tools
                .push(("mock-server".to_string(), tool.clone()));
        }
        manager
            .clients
            .insert("mock-server".to_string(), Arc::new(Mutex::new(client)));

        assert_eq!(manager.tool_count(), 2);
        assert_eq!(manager.server_count(), 1);

        let ava_tools = manager.list_tools();
        assert_eq!(ava_tools.len(), 2);
        assert_eq!(ava_tools[0].name, "tool_a");

        assert_eq!(manager.server_for_tool("tool_a"), Some("mock-server"));
        assert_eq!(manager.server_for_tool("nonexistent"), None);

        // Call a tool
        let result = manager.call_tool("tool_a", json!({})).await.unwrap();
        assert_eq!(result.content, "mock result");
        assert!(!result.is_error);

        manager.shutdown().await.unwrap();
        assert_eq!(manager.tool_count(), 0);
        assert_eq!(manager.server_count(), 0);

        // Server task should end when transport closes
        let _ = server_handle.await;
    }

    #[test]
    fn extract_text_from_content_array() {
        let result = json!({
            "content": [
                { "type": "text", "text": "line 1" },
                { "type": "text", "text": "line 2" }
            ]
        });
        assert_eq!(extract_text_content(&result), "line 1\nline 2");
    }

    #[test]
    fn extract_text_from_string_content() {
        let result = json!({ "content": "plain text" });
        assert_eq!(extract_text_content(&result), "plain text");
    }

    #[test]
    fn extract_text_empty() {
        let result = json!({});
        let text = extract_text_content(&result);
        assert_eq!(text, "{}");
    }
}
