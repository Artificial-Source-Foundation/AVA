# ava-mcp

> Model Context Protocol client and server implementation

## Public API

| Type/Function | Description |
|--------------|-------------|
| `ExtensionManager` | Connects to MCP servers and aggregates their tools |
| `MCPClient` | Client for communicating with a single MCP server |
| `AVAMCPServer` | Local MCP server exporting AVA tools |
| `MCPTool` | Tool definition from MCP server (name, description, inputSchema) |
| `ServerCapabilities` | Server capabilities: tools, resources, prompts |
| `MCPServerConfig` | Server config: name, transport type, enabled flag |
| `TransportType` | Enum: Stdio {command, args, env} or Http {url} |
| `McpServerScope` | Enum: Global or Local config source |
| `MCPTransport` | Trait: send, receive, close for MCP communication |
| `StdioTransport` | Spawn subprocess and communicate via stdin/stdout |
| `HttpTransport` | HTTP transport (placeholder — unimplemented) |
| `InMemoryTransport` | Channel-based transport for testing |
| `FramedTransport` | Generic framed transport over async readers/writers |
| `JsonRpcMessage` | JSON-RPC 2.0 message type |
| `JsonRpcError` | JSON-RPC error with code, message, data |
| `load_mcp_config()` | Load MCP config from JSON file |
| `load_merged_mcp_config()` | Merge global and project configs (project wins) |
| `encode_message()` / `decode_message()` | Content-Length framing helpers |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports client, config, manager, server, transport modules |
| `client.rs` | MCPClient: initialize, list_tools, call_tool, disconnect |
| `config.rs` | Config loading: JSON parsing, global/local merging, scope tracking |
| `manager.rs` | ExtensionManager: connect servers, aggregate tools, route calls |
| `server.rs` | AVAMCPServer: local MCP server exporting built-in tools only |
| `transport.rs` | Transport trait, implementations (stdio, http, in-memory), framing |

## Dependencies

Uses: ava-tools, ava-types

Used by: ava-agent, ava-tui

## Key Patterns

- **Content-Length framing**: MCP uses HTTP-like headers `Content-Length: N\r\n\r\n{json}`
- **Transport abstraction**: `MCPTransport` trait with stdio, HTTP (stub), and in-memory impls
- **Server aggregation**: ExtensionManager connects to multiple servers, aggregates tools, routes calls by name
- **Tool ownership**: Tracks which server owns each tool; routes `call_tool` to correct server
- **Interior mutability**: Clients stored in `Arc<Mutex<MCPClient>>` for concurrent access
- **Result conversion**: MCP results converted to `ToolResult` with text extraction from content blocks
- **Security boundary**: Server only exports `ToolSource::BuiltIn` tools, not MCP or custom tools
- **Config merging**: Project `.ava/mcp.json` overrides global `~/.ava/mcp.json` by server name
- **Env sanitization**: Strips sensitive env vars before spawning MCP servers (same list as plugins)
- **10MB message limit**: Prevents memory exhaustion from malicious/buggy servers
