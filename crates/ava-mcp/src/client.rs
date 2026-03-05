use std::collections::HashMap;

use ava_types::{AvaError, Result, Tool, ToolCall, ToolResult};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tokio::process::{Child, ChildStdin, ChildStdout, Command};

use crate::transport::MCPTransport;

type ProcessTransport = MCPTransport<ChildStdout, ChildStdin>;

pub struct MCPClient {
    servers: HashMap<String, MCPServer>,
    next_request_id: u64,
}

pub struct MCPServer {
    pub name: String,
    pub process: Child,
    pub transport: ProcessTransport,
    pub tools: Vec<Tool>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ServerConfig {
    pub name: String,
    pub command: String,
    pub args: Vec<String>,
    pub env: HashMap<String, String>,
}

impl MCPClient {
    pub fn new() -> Self {
        Self {
            servers: HashMap::new(),
            next_request_id: 1,
        }
    }

    pub async fn connect(&mut self, config: ServerConfig) -> Result<()> {
        let mut command = Command::new(&config.command);
        command
            .args(&config.args)
            .stdin(std::process::Stdio::piped())
            .stdout(std::process::Stdio::piped())
            .stderr(std::process::Stdio::piped());

        for (key, value) in &config.env {
            command.env(key, value);
        }

        let mut process = command
            .spawn()
            .map_err(|error| AvaError::ToolError(format!("failed to spawn MCP server: {error}")))?;

        let stdin = process
            .stdin
            .take()
            .ok_or_else(|| AvaError::ToolError("missing MCP server stdin".to_string()))?;
        let stdout = process
            .stdout
            .take()
            .ok_or_else(|| AvaError::ToolError("missing MCP server stdout".to_string()))?;

        let mut transport = MCPTransport::new(stdout, stdin);
        let init_id = self.next_id();
        transport
            .send(json!({
                "jsonrpc": "2.0",
                "id": init_id,
                "method": "initialize",
                "params": {}
            }))
            .await?;
        let init_response = transport.receive().await?;
        if init_response.get("error").is_some() || init_response.get("result").is_none() {
            return Err(AvaError::ToolError(
                "MCP initialize request failed".to_string(),
            ));
        }

        let list_id = self.next_id();
        transport
            .send(json!({
                "jsonrpc": "2.0",
                "id": list_id,
                "method": "tools/list",
                "params": {}
            }))
            .await?;

        let tools_response = transport.receive().await?;
        let tools = parse_tools_list_response(&tools_response)?;

        self.servers.insert(
            config.name.clone(),
            MCPServer {
                name: config.name,
                process,
                transport,
                tools,
            },
        );

        Ok(())
    }

    pub async fn call_tool(&mut self, server: &str, tool: &str, args: Value) -> Result<ToolResult> {
        let rpc_id = self.next_id();
        let mcp_server = self
            .servers
            .get_mut(server)
            .ok_or_else(|| AvaError::NotFound(format!("unknown MCP server: {server}")))?;

        let call_id = format!("call-{server}-{tool}");
        let request = json!({
            "jsonrpc": "2.0",
            "id": rpc_id,
            "method": "tools/call",
            "params": {
                "name": tool,
                "arguments": args
            }
        });

        mcp_server.transport.send(request).await?;
        let response = mcp_server.transport.receive().await?;

        if let Some(error) = response.get("error") {
            return Err(AvaError::ToolError(format!("MCP call failed: {error}")));
        }

        let content = response
            .get("result")
            .and_then(Value::as_object)
            .and_then(|object| object.get("content"))
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::SerializationError("missing tool response content".to_string()))?
            .to_string();

        let is_error = response
            .get("result")
            .and_then(Value::as_object)
            .and_then(|object| object.get("is_error"))
            .and_then(Value::as_bool)
            .unwrap_or(false);

        Ok(ToolResult {
            call_id,
            content,
            is_error,
        })
    }

    pub fn list_all_tools(&self) -> Vec<(String, Tool)> {
        self.servers
            .iter()
            .flat_map(|(name, server)| {
                server
                    .tools
                    .iter()
                    .cloned()
                    .map(|tool| (name.clone(), tool))
                    .collect::<Vec<_>>()
            })
            .collect()
    }

    pub async fn disconnect(&mut self, server: &str) -> Result<()> {
        let mut mcp_server = self
            .servers
            .remove(server)
            .ok_or_else(|| AvaError::NotFound(format!("unknown MCP server: {server}")))?;

        let shutdown_id = self.next_id();
        let _ = mcp_server
            .transport
            .send(json!({
                "jsonrpc": "2.0",
                "id": shutdown_id,
                "method": "shutdown",
                "params": {}
            }))
            .await;

        let _ = mcp_server.transport.receive().await;

        mcp_server
            .process
            .kill()
            .await
            .map_err(|error| AvaError::ToolError(format!("failed to kill server: {error}")))?;
        Ok(())
    }
}

impl MCPClient {
    fn next_id(&mut self) -> u64 {
        let current = self.next_request_id;
        self.next_request_id = self.next_request_id.saturating_add(1);
        current
    }
}

impl Default for MCPClient {
    fn default() -> Self {
        Self::new()
    }
}

fn parse_tools_list_response(response: &Value) -> Result<Vec<Tool>> {
    response
        .get("result")
        .and_then(Value::as_object)
        .and_then(|object| object.get("tools"))
        .cloned()
        .ok_or_else(|| AvaError::SerializationError("missing tools in tools/list response".to_string()))
        .and_then(|tools| {
            serde_json::from_value::<Vec<Tool>>(tools)
                .map_err(|error| AvaError::SerializationError(error.to_string()))
        })
}

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
