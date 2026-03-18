# ava-tools

> Tool system for file operations, shell commands, and git operations.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `Tool` | Core trait for tools (name, description, parameters, execute) |
| `ToolRegistry` | Central registry with middleware pipeline and source tracking |
| `ToolSource` | Tool provenance (BuiltIn, MCP {server}, Custom {path}) |
| `ToolTier` | Tool visibility tier (Default, Extended, Plugin) |
| `ToolResult` | Tool execution result with content and error flag |
| `ToolCall` | LLM tool call with id, name, and arguments |
| `Middleware` | Before/after hooks for cross-cutting concerns |
| `ToolMonitor` | Tracks tool usage patterns and detects repetition loops |
| `ToolStats` | Aggregate statistics (total_calls, unique_tools, errors, duration) |
| `PermissionMiddleware` | Permission checking middleware with approval bridge |
| `ApprovalBridge` | Bridge for interactive tool approval via TUI |
| `ToolApproval` | Approval decision (Allowed, AllowedForSession, AllowAlways, Rejected) |
| `register_core_tools()` | Registers default + extended tools |
| `register_default_tools()` | Registers 6 default tools (read, write, edit, bash, glob, grep) |
| `register_extended_tools()` | Registers extended-tier tools (apply_patch, web_fetch, etc.) |
| `register_todo_tools()` | Registers todo_read/todo_write with shared state |
| `register_custom_tools()` | Registers TOML-defined custom tools from directories |
| `hash_arguments()` | Hashes tool arguments for deduplication detection |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports core, registry, browser, edit, git, mcp_bridge, monitor, permission_middleware |
| `registry.rs` | ToolRegistry with register, unregister, execute, middleware pipeline, tier tracking |
| `core/mod.rs` | Registration functions for default, extended, todo, question, custom tools |
| `core/read.rs` | ReadTool with line numbers, offset/limit, hashline caching |
| `core/write.rs` | WriteTool for creating/overwriting files |
| `core/edit.rs` | EditTool for precise string replacement with hash-anchored edits |
| `core/multiedit.rs` | MultiEditTool for batching multiple edits |
| `core/bash.rs` | BashTool for shell command execution |
| `core/glob.rs` | GlobTool for file pattern matching |
| `core/grep.rs` | GrepTool for content search with regex |
| `core/apply_patch.rs` | ApplyPatchTool for unified diff application |
| `core/web_fetch.rs` | WebFetchTool for HTTP GET requests |
| `core/web_search.rs` | WebSearchTool for web search (Brave API) |
| `core/ast_ops.rs` | AstOpsTool for AST-based operations |
| `core/lsp_ops.rs` | LspOpsTool for LSP hover/definition/references |
| `core/code_search.rs` | CodeSearchTool for semantic codebase search |
| `core/git_read.rs` | GitReadTool for git log, diff, show operations |
| `core/todo.rs` | TodoWriteTool/TodoReadTool with shared TodoState |
| `core/task.rs` | TaskTool for spawning sub-agents |
| `core/question.rs` | QuestionTool for agent-to-user questions |
| `core/hashline.rs` | HashlineCache for hash-anchored edits |
| `core/path_guard.rs` | Workspace path validation and enforcement |
| `core/secret_redaction.rs` | Automatic secret redaction from tool output |
| `core/output_fallback.rs` | Large output fallback to disk |
| `edit/mod.rs` | Edit strategies and request handling |
| `edit/strategies/` | Edit matching strategies (relative_indent, advanced, fuzzy) |
| `git/mod.rs` | Git operations and snapshotting |
| `mcp_bridge.rs` | MCPBridgeTool for MCP server integration |
| `monitor.rs` | ToolMonitor with repetition detection (ExactRepeat, ToolLoop, AlternatingLoop) |
| `permission_middleware.rs` | PermissionMiddleware with ApprovalBridge and SharedToolSources |
| `browser.rs` | Browser automation tool (placeholder) |

## Dependencies

Uses: ava-types, ava-config, ava-platform, ava-sandbox, ava-permissions, ava-codebase

Used by: ava-agent, ava-tui, src-tauri, ava-mcp, ava-praxis

## Key Patterns

- **Tool trait**: Async trait requiring name, description, JSON parameters schema, execute method
- **Tier system**: Default (6 tools in LLM prompt), Extended (additional tools), Plugin (MCP/custom)
- **Source tracking**: BuiltIn, MCP {server}, Custom {path} for grouping and selective reload
- **Middleware pipeline**: Before/after hooks run in insertion order for all tool executions
- **Repetition detection**: Detects ExactRepeat (same args), ToolLoop (same tool), AlternatingLoop (A-B-A-B)
- **Permission middleware**: Integrates with ava-permissions for Allow/Deny/Ask with approval bridge
- **Hash-anchored edits**: HashlineCache enables stable edits even when line numbers shift
- **Custom tools**: TOML-defined tools loaded from `~/.ava/tools/` and `.ava/tools/`
- **Tool shadowing protection**: External tools cannot shadow built-in tools (SEC-3)
- **Secret redaction**: Automatic redaction of API keys, tokens, passwords from tool output
