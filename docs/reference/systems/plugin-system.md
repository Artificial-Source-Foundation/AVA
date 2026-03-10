# Plugin & Extension System

AVA supports three kinds of extensible tools beyond its 19 built-in ones:
custom TOML tools, MCP (Model Context Protocol) server tools, and a native/WASM
extension system. This document covers how each works and how a developer would
extend AVA today.

## ToolSource Enum

Every tool in the registry is tagged with its provenance
(`crates/ava-tools/src/registry.rs:50`):

```rust
pub enum ToolSource {
    BuiltIn,                    // Core tools registered by register_core_tools()
    MCP { server: String },     // Tools discovered from an MCP server
    Custom { path: String },    // Tools loaded from .toml files
}
```

This enum enables selective reload: `reload_mcp()` removes only
`ToolSource::MCP` tools and re-discovers them, while `reload_custom_tools()`
removes only `ToolSource::Custom` tools.

## Custom TOML Tools

### File Format

Custom tools are defined as `.toml` files in either:
- `~/.ava/tools/` (global, user-level)
- `.ava/tools/` (project-level, relative to working directory)

Both directories are scanned at startup
(`crates/ava-agent/src/stack.rs:187`).

A tool definition file (`crates/ava-tools/src/core/custom_tool.rs:12`):

```toml
name = "hello"
description = "A simple greeting tool"

[[params]]
name = "name"
type = "string"
required = true
description = "Name to greet"

[execution]
type = "shell"
command = "echo 'Hello, {{name}}!'"
timeout_secs = 5
```

### Execution Types

Two execution modes are supported (`crates/ava-tools/src/core/custom_tool.rs:36`):

**Shell** -- Runs a command via `sh -c`:
```toml
[execution]
type = "shell"
command = "echo {{input}}"
timeout_secs = 5
```

**Script** -- Runs a script via a specified interpreter:
```toml
[execution]
type = "script"
interpreter = "python3"
script = "print('hello')"
timeout_secs = 10
```

### Template Substitution

Parameter placeholders use `{{param_name}}` syntax. The
`CustomTool::substitute_args()` method (`crates/ava-tools/src/core/custom_tool.rs:94`)
replaces all `{{key}}` occurrences with their values from the tool call arguments.
String values are inserted directly; other JSON types are serialized.

### Discovery and Registration

1. `load_custom_tools(dir)` reads all `.toml` files from a directory
   (`crates/ava-tools/src/core/custom_tool.rs:184`)
2. Each file is parsed into a `CustomToolDef` struct
3. `register_custom_tools(registry, dirs)` wraps each def in a `CustomTool`
   and registers it with `ToolSource::Custom { path }` (`line 218`)

### Tool Templates

Running `create_tool_templates(dir)` generates three example `.toml` files
(`line 229`): `hello.toml`, `git-stats.toml`, and `file-count.toml`.

### Hot Reload

Custom tools can be reloaded at runtime via `AgentStack::reload_custom_tools()`
(`crates/ava-agent/src/stack.rs:285`). This removes all `ToolSource::Custom`
tools from the registry and re-scans the tool directories. The `/tools reload`
TUI command triggers this.

Note: tools are also fully rebuilt at the start of each `AgentStack::run()` call
(`line 472`), so changes to `.toml` files take effect on the next agent
invocation even without an explicit reload.

## MCP Tools

### Configuration

MCP servers are configured via JSON files:
- `~/.ava/mcp.json` (global)
- `.ava/mcp.json` (project-level)

Project configs override global configs by server name
(`crates/ava-mcp/src/config.rs:71`).

Config file format (`crates/ava-mcp/src/config.rs:11`):

```json
{
  "servers": [
    {
      "name": "filesystem",
      "transport": {
        "type": "stdio",
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"],
        "env": { "API_KEY": "optional-secret" }
      },
      "enabled": true
    },
    {
      "name": "remote-server",
      "transport": {
        "type": "http",
        "url": "http://localhost:8080"
      }
    }
  ]
}
```

### Transport Types

Two transports are supported (`crates/ava-mcp/src/config.rs:23`):

- **stdio** -- Spawns a child process, communicates via stdin/stdout JSON-RPC.
  Supports `command`, `args`, and `env`.
- **http** -- Connects to an HTTP endpoint. Supports `url`.

### Server Lifecycle

1. `load_merged_mcp_config()` reads and merges global + project configs
2. `ExtensionManager::initialize()` connects to each enabled server
   (`crates/ava-mcp/src/manager.rs:34`)
3. For each server, it initializes the MCP protocol handshake and calls
   `list_tools()` to discover available tools
4. Discovered tools are stored as `(server_name, MCPTool)` pairs

### Bridging into the Tool Registry

MCP tools are bridged into AVA's tool system via `MCPBridgeTool`
(`crates/ava-tools/src/mcp_bridge.rs`). Each MCP tool is wrapped in an
adapter that implements the `Tool` trait, forwarding `execute()` calls to
`ExtensionManager::call_tool()` via the `MCPToolCaller` trait.

Registration happens in `init_mcp()` (`crates/ava-agent/src/stack.rs:710`):

```rust
for (server_name, tool_def) in &tools_with_source {
    let source = ToolSource::MCP { server: server_name.clone() };
    registry.register_with_source(
        MCPBridgeTool::new(tool_def.clone(), caller.clone()),
        source,
    );
}
```

### MCP Runtime Persistence

The `MCPRuntime` struct (`crates/ava-agent/src/stack.rs:58`) stores the
`MCPToolCaller`, server count, tool count, and the list of tools with their
server names. This is persisted across agent runs so that MCP connections
are not re-established on every `AgentStack::run()` call -- the same
`MCPRuntime` is reused, and its tools are re-registered into each fresh
`ToolRegistry`.

### Hot Reload

`AgentStack::reload_mcp()` (`crates/ava-agent/src/stack.rs:273`) removes all
MCP tools from the registry, re-reads config files, and re-initializes all
servers. The `/tools reload` TUI command triggers this.

## Extension System (ava-extensions)

The `ava-extensions` crate (`crates/ava-extensions/src/lib.rs`) provides
a lower-level hook and extension management system:

### Components

- **HookRegistry** -- Register callbacks at defined lifecycle points
  (`crates/ava-extensions/src/hook.rs`)
- **ExtensionManager** -- Manages extension descriptors and lifecycle
  (`crates/ava-extensions/src/manager.rs`)
- **NativeLoader** -- Loads shared libraries (.so/.dylib) as extensions
  (`crates/ava-extensions/src/native_loader.rs`)
- **WasmLoader** -- API surface for WASM extension loading
  (`crates/ava-extensions/src/wasm_loader.rs`)

### Hook Points

Extensions can register hooks at defined lifecycle points (`HookPoint` enum).
Hooks receive a `HookContext` and run in registration order.

### Current Status

The extension system provides the infrastructure but is not yet widely used in
the CLI. The MCP system and custom TOML tools cover most extensibility needs
today. Native and WASM loaders exist as API surfaces but are not exercised by
the production binary.

## Tool Registry Architecture

The `ToolRegistry` (`crates/ava-tools/src/registry.rs:72`) is the central
registry:

```rust
pub struct ToolRegistry {
    tools: HashMap<String, Box<dyn Tool>>,
    sources: HashMap<String, ToolSource>,
    middleware: Vec<Box<dyn Middleware>>,
}
```

### Middleware

Middleware runs before and after every tool execution
(`crates/ava-tools/src/registry.rs:41`):

```rust
pub trait Middleware: Send + Sync {
    async fn before(&self, tool_call: &ToolCall) -> Result<()>;
    async fn after(&self, tool_call: &ToolCall, result: &ToolResult) -> Result<ToolResult>;
}
```

Middleware is registered via `add_middleware()` and runs in insertion order.
Use cases include sandboxing, reliability checks, and error recovery.

## Creating a Plugin Today

### Custom TOML Tool (Simplest)

1. Create a `.toml` file in `~/.ava/tools/` or `.ava/tools/`:
   ```toml
   name = "my_tool"
   description = "Does something useful"

   [[params]]
   name = "input"
   type = "string"
   required = true
   description = "The input to process"

   [execution]
   type = "shell"
   command = "my-command {{input}}"
   timeout_secs = 30
   ```
2. The tool is available immediately on the next agent run.
3. Run `/tools reload` in the TUI to pick up changes without restarting.

### MCP Server

1. Create an MCP server (any language) that speaks the MCP JSON-RPC protocol
2. Add it to `~/.ava/mcp.json` or `.ava/mcp.json`:
   ```json
   {
     "servers": [{
       "name": "my-server",
       "transport": {
         "type": "stdio",
         "command": "my-mcp-server",
         "args": ["--verbose"]
       }
     }]
   }
   ```
3. Restart AVA or run `/tools reload` to discover the server's tools.

### Built-in Tool (Rust)

1. Create `crates/ava-tools/src/core/my_tool.rs`
2. Implement the `Tool` trait (name, description, parameters, execute)
3. Register in `crates/ava-tools/src/core/mod.rs` via `register_core_tools()`
4. Add tests in `crates/ava-tools/tests/`

## What is Missing for a Proper Plugin Ecosystem

- **Package format**: No standardized packaging beyond individual `.toml` files
  or MCP server binaries. No manifest, dependency declaration, or versioning.
- **Registry/marketplace**: No central repository for discovering or installing
  community plugins. Users must manually copy files.
- **Versioning**: No version constraints or compatibility checking between
  plugins and AVA versions.
- **Sandboxing for plugins**: Custom TOML tools execute shell commands with full
  user permissions. No per-tool sandboxing or capability restrictions.
- **Configuration UI**: No TUI interface for managing installed plugins, enabling/
  disabling them, or configuring their parameters.
- **WASM runtime**: The `WasmLoader` API surface exists but has no production
  WASM runtime integration. WASM plugins would provide better sandboxing than
  shell-based custom tools.
- **Native plugin ABI**: The `NativeLoader` can load shared libraries but there
  is no stable ABI contract for native plugins.

## Key Files

| File | Role |
|---|---|
| `crates/ava-tools/src/registry.rs` | `ToolSource`, `ToolRegistry`, `Tool` trait, `Middleware` trait |
| `crates/ava-tools/src/core/custom_tool.rs` | TOML tool loading, `CustomToolDef`, template substitution |
| `crates/ava-mcp/src/config.rs` | MCP server config format, loading, merging |
| `crates/ava-mcp/src/manager.rs` | `ExtensionManager` -- MCP server lifecycle |
| `crates/ava-agent/src/stack.rs` | `init_mcp()`, `reload_mcp()`, `reload_custom_tools()` |
| `crates/ava-extensions/src/lib.rs` | Hook system, extension descriptors |
