use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_mcp::client::MCPClient;
use ava_mcp::config::parse_mcp_config;
use ava_mcp::server::AVAMCPServer;
use ava_mcp::transport::{
    decode_message, encode_message, FramedTransport, InMemoryTransport, JsonRpcMessage,
    MCPTransport,
};
use ava_tools::registry::{Tool, ToolRegistry};
use ava_types::ToolResult;
use serde_json::{json, Value};

struct EchoTool;

#[async_trait]
impl Tool for EchoTool {
    fn name(&self) -> &str {
        "echo"
    }

    fn description(&self) -> &str {
        "Echo test tool"
    }

    fn parameters(&self) -> Value {
        json!({"type": "object"})
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let input = args
            .get("text")
            .and_then(Value::as_str)
            .unwrap_or("empty")
            .to_string();

        Ok(ToolResult {
            call_id: "tool-call".to_string(),
            content: input,
            is_error: false,
        })
    }
}

#[test]
fn transport_encode_decode_roundtrip() {
    let payload = r#"{"jsonrpc":"2.0","id":1,"method":"initialize"}"#;
    let frame = encode_message(payload);
    let decoded = decode_message(&frame).expect("frame should decode");
    assert_eq!(decoded, payload);
}

#[test]
fn transport_decode_rejects_invalid_frame() {
    let invalid = "Content-Length: 10\r\n\r\n{}";
    let error = decode_message(invalid).expect_err("invalid frame should fail");
    assert!(error.to_string().contains("body length mismatch"));
}

#[tokio::test]
async fn server_handles_initialize_tools_list_and_call() {
    let mut registry = ToolRegistry::new();
    registry.register(EchoTool);

    let server = AVAMCPServer::new(Arc::new(registry));

    let init = server
        .handle_request(json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}))
        .await
        .expect("initialize should succeed");
    assert_eq!(init["result"]["server"], "ava-mcp");

    let tools = server
        .handle_request(json!({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}))
        .await
        .expect("tools/list should succeed");
    assert_eq!(tools["result"]["tools"][0]["name"], "echo");

    let call = server
        .handle_request(json!({
            "jsonrpc":"2.0",
            "id":3,
            "method":"tools/call",
            "params":{"name":"echo","arguments":{"text":"hello"}}
        }))
        .await
        .expect("tools/call should succeed");
    assert_eq!(call["result"]["content"], "hello");
}

#[tokio::test]
async fn round_trip_framed_transport() {
    let (left, right) = tokio::io::duplex(2048);
    let (left_reader, left_writer) = tokio::io::split(left);
    let (right_reader, right_writer) = tokio::io::split(right);

    let mut sender = FramedTransport::new(left_reader, left_writer);
    let mut receiver = FramedTransport::new(right_reader, right_writer);

    sender
        .send(json!({"jsonrpc":"2.0","id":1,"method":"ping","params":{}}))
        .await
        .expect("send should succeed");

    let message = receiver.receive().await.expect("receive should succeed");
    assert_eq!(message["method"], "ping");
}

#[tokio::test]
async fn in_memory_transport_pair() {
    let (mut a, mut b) = InMemoryTransport::pair();
    let msg = JsonRpcMessage::request(1, "hello", json!({}));
    a.send(&msg).await.unwrap();
    let received = b.receive().await.unwrap();
    assert_eq!(received.method.as_deref(), Some("hello"));
}

#[tokio::test]
async fn client_protocol_with_mock_transport() {
    let (client_transport, mut server_transport) = InMemoryTransport::pair();

    let server = tokio::spawn(async move {
        // initialize
        let req = server_transport.receive().await.unwrap();
        assert_eq!(req.method.as_deref(), Some("initialize"));
        let resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: req.id,
            method: None,
            params: None,
            result: Some(json!({
                "protocolVersion": "2024-11-05",
                "capabilities": { "tools": {} },
                "serverInfo": { "name": "mock", "version": "1.0" }
            })),
            error: None,
        };
        server_transport.send(&resp).await.unwrap();

        // initialized notification
        let notif = server_transport.receive().await.unwrap();
        assert_eq!(notif.method.as_deref(), Some("notifications/initialized"));

        // tools/list
        let req = server_transport.receive().await.unwrap();
        assert_eq!(req.method.as_deref(), Some("tools/list"));
        let resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: req.id,
            method: None,
            params: None,
            result: Some(json!({
                "tools": [
                    {
                        "name": "echo",
                        "description": "Echo tool",
                        "inputSchema": { "type": "object" }
                    }
                ]
            })),
            error: None,
        };
        server_transport.send(&resp).await.unwrap();

        // tools/call
        let req = server_transport.receive().await.unwrap();
        assert_eq!(req.method.as_deref(), Some("tools/call"));
        let params = req.params.unwrap();
        let text = params["arguments"]["text"].as_str().unwrap();
        let resp = JsonRpcMessage {
            jsonrpc: "2.0".to_string(),
            id: req.id,
            method: None,
            params: None,
            result: Some(json!({
                "content": [{ "type": "text", "text": text }],
                "isError": false
            })),
            error: None,
        };
        server_transport.send(&resp).await.unwrap();
    });

    let mut client = MCPClient::new(Box::new(client_transport), "mock");

    let caps = client
        .initialize()
        .await
        .expect("initialize should succeed");
    assert!(caps.tools);

    let tools = client
        .list_tools()
        .await
        .expect("list_tools should succeed");
    assert_eq!(tools.len(), 1);
    assert_eq!(tools[0].name, "echo");

    let result = client
        .call_tool("echo", json!({"text": "from-client"}))
        .await
        .expect("call_tool should succeed");
    let text = result["content"][0]["text"].as_str().unwrap();
    assert_eq!(text, "from-client");

    client
        .disconnect()
        .await
        .expect("disconnect should succeed");
    server.await.unwrap();
}

#[tokio::test]
async fn client_connects_to_mock_process_and_calls_tool() {
    let script = r#"
import sys, json

def read_msg():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line == b"\r\n":
            break
        key, value = line.decode().split(":", 1)
        headers[key.lower().strip()] = value.strip()
    length = int(headers["content-length"])
    body = sys.stdin.buffer.read(length)
    return json.loads(body.decode())

def write_msg(payload):
    encoded = json.dumps(payload).encode()
    sys.stdout.buffer.write(f"Content-Length: {len(encoded)}\r\n\r\n".encode())
    sys.stdout.buffer.write(encoded)
    sys.stdout.buffer.flush()

while True:
    req = read_msg()
    if req is None:
        break
    method = req.get("method")
    if method == "initialize":
        write_msg({"jsonrpc":"2.0","id":req.get("id"),"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"mock","version":"1.0"}}})
    elif method == "notifications/initialized":
        pass  # notification, no response
    elif method == "tools/list":
        write_msg({"jsonrpc":"2.0","id":req.get("id"),"result":{"tools":[{"name":"echo","description":"Echo","inputSchema":{"type":"object"}}]}})
    elif method == "tools/call":
        text = req.get("params",{}).get("arguments",{}).get("text","")
        write_msg({"jsonrpc":"2.0","id":req.get("id"),"result":{"content":[{"type":"text","text":text}],"isError":False}})
    else:
        break
"#;

    // Check python3 is available
    if std::process::Command::new("python3")
        .arg("--version")
        .output()
        .is_err()
    {
        eprintln!("python3 not found, skipping test");
        return;
    }

    let transport = ava_mcp::StdioTransport::spawn(
        "python3",
        &["-u".to_string(), "-c".to_string(), script.to_string()],
        &HashMap::new(),
    )
    .await
    .expect("spawn should succeed");

    let mut client = MCPClient::new(Box::new(transport), "mock");

    let caps = client
        .initialize()
        .await
        .expect("initialize should succeed");
    assert!(caps.tools);

    let tools = client
        .list_tools()
        .await
        .expect("list_tools should succeed");
    assert_eq!(tools.len(), 1);
    assert_eq!(tools[0].name, "echo");

    let result = client
        .call_tool("echo", json!({"text": "from-client"}))
        .await
        .expect("call_tool should succeed");
    let text = result["content"][0]["text"].as_str().unwrap();
    assert_eq!(text, "from-client");

    client
        .disconnect()
        .await
        .expect("disconnect should succeed");
}

#[test]
fn config_parsing() {
    let json = r#"{
        "servers": [
            {
                "name": "fs",
                "transport": {
                    "type": "stdio",
                    "command": "npx",
                    "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
                },
                "enabled": true
            },
            {
                "name": "web",
                "transport": {
                    "type": "http",
                    "url": "http://localhost:9000"
                },
                "enabled": false
            }
        ]
    }"#;

    let configs = parse_mcp_config(json).unwrap();
    assert_eq!(configs.len(), 2);
    assert!(configs[0].enabled);
    assert!(!configs[1].enabled);
}
