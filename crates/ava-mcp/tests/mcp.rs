use std::collections::HashMap;
use std::sync::Arc;

use async_trait::async_trait;
use ava_mcp::client::{MCPClient, ServerConfig};
use ava_mcp::server::AVAMCPServer;
use ava_mcp::transport::{decode_message, encode_message, MCPTransport};
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
async fn round_trip_transport_between_client_and_server() {
    let (left, right) = tokio::io::duplex(2048);
    let (left_reader, left_writer) = tokio::io::split(left);
    let (right_reader, right_writer) = tokio::io::split(right);

    let mut sender = MCPTransport::new(left_reader, left_writer);
    let mut receiver = MCPTransport::new(right_reader, right_writer);

    sender
        .send(json!({"jsonrpc":"2.0","id":1,"method":"ping","params":{}}))
        .await
        .expect("send should succeed");

    let message = receiver.receive().await.expect("receive should succeed");
    assert_eq!(message["method"], "ping");
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

for _ in range(3):
    req = read_msg()
    if req is None:
        break
    if req.get("method") == "initialize":
        write_msg({"jsonrpc":"2.0","id":req.get("id"),"result":{"capabilities":{"tools":True}}})
    elif req.get("method") == "tools/list":
        write_msg({"jsonrpc":"2.0","id":req.get("id"),"result":{"tools":[{"name":"echo","description":"Echo","parameters":{}}]}})
    elif req.get("method") == "tools/call":
        text = req.get("params",{}).get("arguments",{}).get("text","")
        write_msg({"jsonrpc":"2.0","id":req.get("id"),"result":{"content":text,"is_error":False}})
"#;

    let mut client = MCPClient::new();
    client
        .connect(ServerConfig {
            name: "mock".to_string(),
            command: "python3".to_string(),
            args: vec!["-u".to_string(), "-c".to_string(), script.to_string()],
            env: HashMap::new(),
        })
        .await
        .expect("connect should succeed");

    let tools = client.list_all_tools();
    assert_eq!(tools.len(), 1);
    assert_eq!(tools[0].0, "mock");
    assert_eq!(tools[0].1.name, "echo");

    let result = client
        .call_tool("mock", "echo", json!({"text": "from-client"}))
        .await
        .expect("call should succeed");
    assert_eq!(result.content, "from-client");

    client
        .disconnect("mock")
        .await
        .expect("disconnect should succeed");
}

#[tokio::test]
async fn client_errors_for_unknown_server() {
    let mut client = MCPClient::new();
    let call = client.call_tool("missing", "echo", json!({})).await;
    assert!(call.is_err());

    let disconnect = client.disconnect("missing").await;
    assert!(disconnect.is_err());
}
