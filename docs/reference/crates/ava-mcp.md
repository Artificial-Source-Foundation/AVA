# ava-mcp

Model Context Protocol (MCP) implementation. Provides JSON-RPC transport, client/server abstractions, and a manager that aggregates tools from multiple MCP servers.

## How It Works

### Configuration (`src/config.rs`)

```rust
pub struct MCPServerConfig {
    pub name: String,
    pub transport: TransportType,  // Stdio or Http
    pub command: Option<String>,
    pub args: Option<Vec<String>>,
    pub url: Option<String>,
    pub env: Option<HashMap<String, String>>,
}
```

`load_mcp_config(path)` loads from a JSON file. `load_merged_mcp_config(global, project)` merges global (`~/.ava/mcp.json`) and project-level (`.ava/mcp.json`) configs, with project overrides winning.

### Transport (`src/transport.rs`)

`MCPTransport` trait defines `send(request) -> response` and `close()`. Implementations:

- **`StdioTransport`** -- spawns a subprocess, communicates via Content-Length framed JSON-RPC over stdin/stdout
- **`HttpTransport`** -- stub for HTTP-based MCP servers
- **`InMemoryTransport`** -- for testing, uses a channel pair
- **`FramedTransport`** -- generic Content-Length framing over any AsyncRead/AsyncWrite

`JsonRpcMessage` wraps JSON-RPC 2.0 requests and responses:

```rust
pub struct JsonRpcMessage {
    pub jsonrpc: String,       // "2.0"
    pub id: Option<Value>,
    pub method: Option<String>,
    pub params: Option<Value>,
    pub result: Option<Value>,
    pub error: Option<Value>,
}
```

**File**: `crates/ava-mcp/src/transport.rs`

### Client (`src/client.rs`)

`MCPClient` wraps a transport and implements the MCP protocol handshake:

| Method | Description |
|--------|-------------|
| `initialize()` | Sends `initialize` request, receives server capabilities |
| `list_tools()` | Retrieves available tools from the server |
| `call_tool(name, args)` | Invokes a tool and returns the result |

`ServerCapabilities` and `MCPTool` hold the parsed server metadata.

### Manager (`src/manager.rs`)

`ExtensionManager` connects to multiple MCP servers, aggregates their tools into a unified registry, and routes `call_tool()` to the correct server based on tool name.

### Server (`src/server.rs`)

`AVAMCPServer` wraps a `ToolRegistry` and handles inbound MCP protocol requests:
- `initialize` -- returns server capabilities
- `tools/list` -- lists available tools
- `tools/call` -- executes a tool
- `shutdown` -- graceful shutdown

This allows AVA itself to act as an MCP server, exposing its tools to other MCP clients.

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/config.rs` | -- | MCPServerConfig, JSON loading, merge |
| `src/lib.rs` | -- | Crate root, re-exports |
| `src/transport.rs` | -- | MCPTransport trait, Stdio/Http/InMemory/Framed |
| `src/client.rs` | -- | MCPClient, initialize/list_tools/call_tool |
| `src/manager.rs` | -- | ExtensionManager, multi-server aggregation |
| `src/server.rs` | -- | AVAMCPServer, inbound protocol handling |
