use std::sync::Arc;

use ava_tools::registry::ToolRegistry;
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
            "tools/list" => Ok(json!({
                "jsonrpc": "2.0",
                "id": id,
                "result": {
                    "tools": self.tool_registry.list_tools()
                }
            })),
            "tools/call" => {
                let call_id = format!("mcp-call-{id}");
                let tool_call = tool_call_from_request(&call_id, &params)?;
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
