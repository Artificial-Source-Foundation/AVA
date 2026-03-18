# AVA Codebase Documentation

> Complete reference for all 21 Rust crates, frontend, plugin system, and Tauri commands

## Quick Navigation

| I want to... | See |
|--------------|-----|
| Add a tool | [ava-tools.md](ava-tools.md) |
| Add an LLM provider | [ava-llm.md](ava-llm.md) |
| Add a Tauri command | [tauri-commands.md](tauri-commands.md), [frontend.md](frontend.md) |
| Work on the TUI | [ava-tui.md](ava-tui.md) |
| Work on multi-agent | [ava-praxis.md](ava-praxis.md), [ava-agent.md](ava-agent.md) |
| Add authentication | [ava-auth.md](ava-auth.md) |
| Work on permissions | [ava-permissions.md](ava-permissions.md) |
| Create a plugin | [plugins.md](plugins.md) |
| Work on memory/context | [ava-memory.md](ava-memory.md), [ava-context.md](ava-context.md), [ava-session.md](ava-session.md) |

## Crate Reference (21 crates)

### Core Stack

| Crate | Purpose | Lines | Doc |
|-------|---------|------:|-----|
| `ava-tui` | CLI/TUI binary (Ratatui + Crossterm) | 33K | [ava-tui.md](ava-tui.md) |
| `ava-agent` | Agent execution loop, tool calling, stuck detection | 10K | [ava-agent.md](ava-agent.md) |
| `ava-llm` | LLM providers (8 built-in), circuit breaker, routing | 11K | [ava-llm.md](ava-llm.md) |
| `ava-tools` | Tool trait, registry, 6 default + 8 extended tools | 10K | [ava-tools.md](ava-tools.md) |
| `ava-types` | Shared types: Message, Session, ToolCall, etc. | 1.7K | [ava-types.md](ava-types.md) |

### Data & Persistence

| Crate | Purpose | Lines | Doc |
|-------|---------|------:|-----|
| `ava-session` | Session persistence, conversation trees | 1.6K | [ava-session.md](ava-session.md) |
| `ava-memory` | Persistent key-value memory with FTS5 | 778 | [ava-memory.md](ava-memory.md) |
| `ava-context` | Token tracking, context condensation | 3K | [ava-context.md](ava-context.md) |
| `ava-db` | SQLite connection pool (legacy) | 444 | [ava-db.md](ava-db.md) |

### Auth & Security

| Crate | Purpose | Lines | Doc |
|-------|---------|------:|-----|
| `ava-auth` | OAuth, PKCE, device code, API keys | 1.5K | [ava-auth.md](ava-auth.md) |
| `ava-permissions` | Permission rules, bash classifier, risk levels | 6.4K | [ava-permissions.md](ava-permissions.md) |
| `ava-validator` | Code validation pipeline | 299 | [ava-validator.md](ava-validator.md) |

### Configuration

| Crate | Purpose | Lines | Doc |
|-------|---------|------:|-----|
| `ava-config` | Config, credentials, model catalog | 5K | [ava-config.md](ava-config.md) |

### Extension & Integration

| Crate | Purpose | Lines | Doc |
|-------|---------|------:|-----|
| `ava-extensions` | Extension system: hooks, native/WASM | 509 | [ava-extensions.md](ava-extensions.md) |
| `ava-plugin` | Power plugin system (JSON-RPC) | 2.8K | [ava-plugin.md](ava-plugin.md) |
| `ava-mcp` | Model Context Protocol client/server | 1.7K | [ava-mcp.md](ava-mcp.md) |
| `ava-cli-providers` | External CLI agent integration | 1.5K | [ava-cli-providers.md](ava-cli-providers.md) |

### Infrastructure

| Crate | Purpose | Lines | Doc |
|-------|---------|------:|-----|
| `ava-praxis` | Multi-agent orchestration (Director pattern) | 3.7K | [ava-praxis.md](ava-praxis.md) |
| `ava-codebase` | Code indexing (BM25 + PageRank) | 1.3K | [ava-codebase.md](ava-codebase.md) |
| `ava-platform` | File system and shell abstractions | 989 | [ava-platform.md](ava-platform.md) |
| `ava-sandbox` | OS-level sandboxing | 670 | [ava-sandbox.md](ava-sandbox.md) |

## Frontend & Desktop

| Doc | Purpose |
|-----|---------|
| [frontend.md](frontend.md) | SolidJS frontend, hooks, stores, Tauri IPC |
| [tauri-commands.md](tauri-commands.md) | 70+ Rust commands exposed to frontend |

## Plugin System

| Doc | Purpose |
|-----|---------|
| [plugins.md](plugins.md) | JSON-RPC plugin architecture, SDKs, hooks |

## Dependency Graph

```
┌─────────────────────────────────────────────────────────────┐
│                        ava-tui                               │
│                    (CLI/TUI binary)                          │
└──────────────┬──────────────────────────────────┬───────────┘
               │                                  │
    ┌──────────┴──────────┐            ┌──────────┴──────────┐
    │      Desktop        │            │      Headless       │
    │ (src/ + src-tauri)  │            │  (--headless flag)  │
    └──────────┬──────────┘            └─────────────────────┘
               │
┌──────────────┴─────────────────────────────────────────────┐
│                      Core Stack                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ava-agent │──│ava-llm   │  │ava-tools │──│ava-types │    │
│  └────┬─────┘  └──────────┘  └────┬─────┘  └────┬─────┘    │
│       │                           │            │          │
│  ┌────┴─────┐              ┌──────┴──────┐    │          │
│  │ava-praxis│              │ava-permissions│   │          │
│  │(multi-  │              │ava-config     │   │          │
│  │ agent)  │              └───────────────┘   │          │
│  └──────────┘                                  │          │
└────────────────────────────────────────────────┼──────────┘
                                                 │
┌────────────────────────────────────────────────┴──────────┐
│                    Data & Persistence                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ava-session│  │ava-memory│  │ava-context│  │ava-db   │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────┐
│              Extension & Integration                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ava-plugin│  │ava-mcp   │  │ava-ext.  │  │ava-cli-p │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
└────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────┐
│              Auth, Security, Infrastructure                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ava-auth  │  │ava-perm. │  │ava-codeb.│  │ava-sandb.│   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                   │
│  │ava-val.  │  │ava-plat. │  │ava-plugin│                   │
│  └──────────┘  └──────────┘  └──────────┘                   │
└────────────────────────────────────────────────────────────┘
```

## Where to Find Things

| Task | Location |
|------|----------|
| Add default tool | [ava-tools.md](ava-tools.md) → `crates/ava-tools/src/core/` |
| Add extended tool | [ava-tools.md](ava-tools.md) → `crates/ava-tools/src/core/` |
| Add LLM provider | [ava-llm.md](ava-llm.md) → `crates/ava-llm/src/providers/` |
| Add Tauri command | [tauri-commands.md](tauri-commands.md) → `src-tauri/src/commands/` |
| Add frontend component | [frontend.md](frontend.md) → `src/components/` |
| Add hook | [frontend.md](frontend.md) → `src/hooks/` |
| Create plugin | [plugins.md](plugins.md) |
| Add MCP server support | [ava-mcp.md](ava-mcp.md) |
| Work on TUI | [ava-tui.md](ava-tui.md) → `crates/ava-tui/src/` |
| Add auth provider | [ava-auth.md](ava-auth.md) |
| Work on permissions | [ava-permissions.md](ava-permissions.md) |
| Multi-agent orchestration | [ava-praxis.md](ava-praxis.md) |
| Context/memory features | [ava-context.md](ava-context.md), [ava-memory.md](ava-memory.md) |
