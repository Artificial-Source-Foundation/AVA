# Sprint 39: MCP Expansion & Plugin System

> Combines Sprints 39 + 40 from the roadmap.

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Make AVA fully extensible. Users should be able to add new tools without recompiling — via MCP servers or simple YAML/TOML config files. After this sprint, AVA can load external tools at startup and show them in the TUI.

## Key Files to Read

```
crates/ava-mcp/src/lib.rs
crates/ava-mcp/src/client.rs           # MCPClient (functional)
crates/ava-mcp/src/config.rs           # MCPServerConfig, TransportType, load_mcp_config()
crates/ava-mcp/src/manager.rs          # ExtensionManager (functional)
crates/ava-mcp/src/transport.rs        # StdioTransport, HttpTransport, InMemoryTransport
crates/ava-tools/src/mcp_bridge.rs     # MCPBridgeTool, MCPToolCaller trait
crates/ava-tools/src/registry.rs       # ToolRegistry, Tool trait
crates/ava-tools/src/core/mod.rs       # register_core_tools()
crates/ava-agent/src/stack.rs          # AgentStack — MCP loading point
crates/ava-tui/src/app.rs             # AppState, ModalType
crates/ava-tui/src/widgets/command_palette.rs
crates/ava-tui/src/ui/status_bar.rs
crates/ava-config/src/lib.rs           # Config loading
```

## What Already Exists

- **MCP Client**: Full protocol handshake, list_tools, call_tool — functional
- **ExtensionManager**: Connects to servers, aggregates tools, routes calls
- **MCPBridgeTool**: Implements Tool trait, bridges to MCP servers
- **Config**: `~/.ava/mcp.json` with stdio/HTTP transport types
- **AgentStack**: Loads MCP config and registers bridge tools on startup

## Theme 1: MCP Tool Discovery & Management

### Story 1.1: MCP Status in TUI

Show MCP server status in the TUI so users know what external tools are available.

**Implementation:**
- Add MCP server count to status bar: `MCP: 3 servers │ 12 tools`
- Pass server/tool counts from AgentStack to TUI state
- Update on connect/disconnect

**Acceptance criteria:**
- Status bar shows MCP server and tool counts
- Updates when servers connect/disconnect
- Shows 0/0 when no MCP configured (don't hide)

### Story 1.2: Tool List Command

Add a `/tools` command to the command palette that lists all available tools (built-in + MCP).

**Implementation:**
- Register `/tools` in command palette
- When invoked, show an overlay dialog listing all tools
- Group by source: "Built-in" vs "MCP: server-name"
- Show: tool name, description (truncated), source
- Scrollable list for many tools

**Acceptance criteria:**
- `/tools` command shows all registered tools
- Grouped by source
- Scrollable
- Esc closes

### Story 1.3: MCP Server Management Commands

Add commands to manage MCP servers at runtime.

**Commands:**
- `/mcp list` — show connected servers with status
- `/mcp reload` — reload `~/.ava/mcp.json` and reconnect

**Implementation:**
- Add to command palette
- `/mcp list` shows a dialog with server name, transport type, tool count, connection status
- `/mcp reload` re-reads config, disconnects old servers, connects new ones

**Acceptance criteria:**
- Both commands work from palette
- List shows accurate status
- Reload doesn't crash if config is invalid
- Reload preserves conversation state

### Story 1.4: Per-Project MCP Config

Support project-local MCP config in addition to global.

**Config resolution order:**
1. `.ava/mcp.json` (project-local, in CWD or parent dirs)
2. `~/.ava/mcp.json` (global)
3. Merge: project-local overrides global by server name

**Implementation:**
- In `crates/ava-mcp/src/config.rs`, add `load_merged_config(project_root, home_dir)`
- Walk up from CWD looking for `.ava/mcp.json`
- Merge with global config

**Acceptance criteria:**
- Project-local config found and loaded
- Merges with global (project overrides global by name)
- Works if only global exists
- Works if only project-local exists
- Works if neither exists (empty)
- Add tests

## Theme 2: Simple Plugin System

### Story 2.1: TOML Tool Definitions

Let users define custom tools via simple TOML files without writing Rust.

**Config location:** `~/.ava/tools/` (global) and `.ava/tools/` (project-local)

**Example** `~/.ava/tools/deploy.toml`:
```toml
name = "deploy"
description = "Deploy the application to production"

[parameters]
environment = { type = "string", description = "Target environment", required = true }
dry_run = { type = "boolean", description = "Dry run mode", default = false }

[execution]
command = "bash"
args = ["-c", "./scripts/deploy.sh {{environment}} {{dry_run}}"]
timeout = 300
working_dir = "."
```

**Implementation:**
- File: `crates/ava-tools/src/custom_tool.rs` (NEW)
- `CustomTool` struct implementing `Tool` trait
- Parse TOML, substitute `{{param}}` placeholders in command/args
- Execute via `tokio::process::Command`
- Capture stdout/stderr as tool result
- `load_custom_tools(dirs: &[PathBuf]) -> Vec<CustomTool>`

**Integration:**
- `AgentStack::new()` loads custom tools from `~/.ava/tools/` and `.ava/tools/`
- Register in ToolRegistry alongside core and MCP tools

**Acceptance criteria:**
- TOML tool definitions parsed and loaded
- Parameter substitution works
- Execution via subprocess with timeout
- Tools appear in `/tools` list as "Custom"
- Invalid TOML files are skipped with warning (don't crash)
- Add tests

### Story 2.2: Tool Hot-Reload

When tool TOML files change, reload them without restarting AVA.

**Implementation:**
- Add `/tools reload` command to palette
- Re-scans `~/.ava/tools/` and `.ava/tools/`
- Unregisters old custom tools, registers new ones
- Does NOT affect core or MCP tools

**Acceptance criteria:**
- `/tools reload` picks up new/changed/deleted TOML files
- Core tools unaffected
- MCP tools unaffected
- Status message: "Reloaded N custom tools"

### Story 2.3: Tool Templates

Provide a few example TOML templates that users can copy and customize.

**Implementation:**
- Add `/tools init` command
- Creates `~/.ava/tools/` directory if it doesn't exist
- Copies 3 template files:
  - `example-deploy.toml` — deployment script
  - `example-format.toml` — code formatter
  - `example-db-query.toml` — database query tool
- Each template is well-commented explaining all fields

**Acceptance criteria:**
- `/tools init` creates directory and templates
- Templates are valid TOML
- Templates have helpful comments
- Doesn't overwrite existing files

## Implementation Order

1. Story 1.1 (MCP status in TUI) — quick, visible
2. Story 1.2 (tool list command) — foundation for discovery
3. Story 2.1 (TOML tool definitions) — core plugin feature
4. Story 1.4 (per-project MCP config) — important for multi-project
5. Story 1.3 (MCP management commands) — management UX
6. Story 2.2 (hot-reload) — depends on 2.1
7. Story 2.3 (templates) — polish, last

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Custom tools must go through the permission system (same approval as core tools)
- TOML parsing via `toml` crate (already in workspace deps, check)
- Don't break existing MCP or tool registration

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-mcp -- --nocapture
cargo test -p ava-tools -- --nocapture
```
