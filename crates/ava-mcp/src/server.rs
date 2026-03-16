use std::sync::Arc;

use ava_tools::registry::{ToolRegistry, ToolSource};
use ava_types::Result;
use serde_json::{json, Value};

use crate::client::tool_call_from_request;
use crate::transport::FramedTransport;

pub struct AVAMCPServer {
    tool_registry: Arc<ToolRegistry>,
}

impl AVAMCPServer {
    pub fn new(tool_registry: Arc<ToolRegistry>) -> Self {
        Self { tool_registry }
    }

    pub async fn run(&self) -> Result<()> {
        let stdin = tokio::io::stdin();
        let stdout = tokio::io::stdout();
        let mut transport = FramedTransport::new(stdin, stdout);

        loop {
            let request = transport.receive().await?;
            let should_stop = request
                .get("method")
                .and_then(Value::as_str)
                .is_some_and(|method| method == "shutdown");
            let response = self.handle_request(request).await?;
            transport.send(response).await?;
            if should_stop {
                return Ok(());
            }
        }
    }

    pub async fn handle_request(&self, request: Value) -> Result<Value> {
        let id = request.get("id").cloned().unwrap_or(Value::Null);
        let Some(method) = request.get("method").and_then(Value::as_str) else {
            return Ok(json!({
                "jsonrpc": "2.0",
                "id": id,
                "error": {
                    "code": -32600,
                    "message": "missing RPC method"
                }
            }));
        };

        let params = request.get("params").cloned().unwrap_or_else(|| json!({}));
        match method {
            "initialize" => Ok(json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "capabilities": {
                        "tools": true
                    },
                    "server": "ava-mcp"
                }
            })),
            "tools/list" => {
                // Only export built-in tools via MCP — do not re-export MCP or custom tools
                // to prevent tool leakage across trust boundaries.
                let exportable: Vec<_> = self
                    .tool_registry
                    .list_tools_with_source()
                    .into_iter()
                    .filter(|(_, source)| *source == ToolSource::BuiltIn)
                    .map(|(def, _)| def)
                    .collect();
                Ok(json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "tools": exportable
                    }
                }))
            }
            "tools/call" => {
                let call_id = format!("mcp-call-{id}");
                let tool_call = tool_call_from_request(&call_id, &params)?;

                // Only allow execution of built-in tools via MCP
                let source = self.tool_registry.tool_source(&tool_call.name);
                if source.as_ref() != Some(&ToolSource::BuiltIn) {
                    return Ok(json!({
                        "jsonrpc": "2.0",
                        "id": id,
                        "error": {
                            "code": -32601,
                            "message": format!("tool '{}' is not exportable via MCP", tool_call.name)
                        }
                    }));
                }

                let result = self.tool_registry.execute(tool_call).await?;

                Ok(json!({
                    "jsonrpc": "2.0",
                    "id": id,
                    "result": {
                        "content": result.content,
                        "is_error": result.is_error
                    }
                }))
            }
            "shutdown" => Ok(json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "ok": true
                }
            })),
            _ => Ok(json!({
                "jsonrpc": "2.0",
                "id": id,
                "error": {
                    "code": -32601,
                    "message": format!("unknown method: {method}")
                }
            })),
        }
    }
}
