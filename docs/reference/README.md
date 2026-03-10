# AVA Reference Documentation

Cross-cutting system documentation for the AVA AI coding assistant. These
documents explain how systems work across multiple crates and help developers
and AI agents understand the architecture.

## Architecture

- [Architecture Overview](overview.md) -- What AVA is, how the 22 crates
  relate, data flow from user input to response, key design decisions

## Systems

- [Token & Cost Tracking](systems/token-counting.md) -- How token usage flows
  from LLM providers through StreamChunk to the TUI, per-provider parsing
  differences, cache-aware cost calculation, sub-agent propagation

- [Plugin & Extension System](systems/plugin-system.md) -- Custom TOML tools,
  MCP server integration, the extension system, ToolSource provenance tracking,
  hot-reload, and what is needed for a proper plugin ecosystem

- [Project Instructions](systems/instructions.md) -- Discovery order for
  instruction files, cross-tool compatibility (CLAUDE.md, .cursorrules, etc.),
  glob-scoped rules with frontmatter, contextual per-file instructions,
  agents.toml configuration

- [Safety & Permissions](systems/permissions.md) -- SafetyTag types, RiskLevel
  ordering, bash command classification, permission policies (permissive /
  standard / strict), the 9-step inspection flow, tool approval in the TUI,
  sandbox integration

- [Session Management](systems/sessions.md) -- Session lifecycle (create, save,
  load, fork, delete, search), SQLite schema with FTS5, parent-child linking
  for sub-agents, auto-naming from first message, token usage tracking

## Quick Links

| Topic | Primary Crate | Entry Point |
|---|---|---|
| Agent execution | `ava-agent` | `crates/ava-agent/src/stack.rs` |
| LLM providers | `ava-llm` | `crates/ava-llm/src/providers/` |
| Tool system | `ava-tools` | `crates/ava-tools/src/registry.rs` |
| TUI application | `ava-tui` | `crates/ava-tui/src/app/mod.rs` |
| Configuration | `ava-config` | `crates/ava-config/src/lib.rs` |
| Session storage | `ava-session` | `crates/ava-session/src/lib.rs` |
| Permissions | `ava-permissions` | `crates/ava-permissions/src/inspector.rs` |
| MCP protocol | `ava-mcp` | `crates/ava-mcp/src/manager.rs` |

## Per-Crate Documentation

Detailed documentation for individual crates lives in `crates/`:

- [ava-agent](crates/ava-agent.md) -- Agent execution loop and stack
- [ava-llm](crates/ava-llm.md) -- LLM providers and connection pool
- [ava-tools](crates/ava-tools.md) -- Tool trait, registry, built-in tools
- [ava-commander](crates/ava-commander.md) -- Multi-agent orchestration
- [ava-tui](crates/ava-tui.md) -- TUI binary and widgets
- [ava-session](crates/ava-session.md) -- Session persistence
- [ava-permissions](crates/ava-permissions.md) -- Permission system
- [ava-config](crates/ava-config.md) -- Configuration management
- [ava-context](crates/ava-context.md) -- Context window management
- [ava-memory](crates/ava-memory.md) -- Persistent memory
- [ava-mcp](crates/ava-mcp.md) -- MCP protocol support
- [ava-sandbox](crates/ava-sandbox.md) -- Command sandboxing
- [ava-codebase](crates/ava-codebase.md) -- Code indexing
- [ava-platform](crates/ava-platform.md) -- Platform abstractions
- [ava-auth](crates/ava-auth.md) -- OAuth and token exchange
- [ava-types](crates/ava-types.md) -- Shared types
- [ava-db](crates/ava-db.md) -- Database connection pool

## See Also

- `CLAUDE.md` -- Architecture summary, quick commands, conventions
- `AGENTS.md` -- AI agent instructions
- `docs/development/roadmap.md` -- Sprint roadmap
- `docs/architecture/` -- Design documents
