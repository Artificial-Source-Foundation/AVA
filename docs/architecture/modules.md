<!-- Last verified: 2026-03-16. Run 'cargo test --workspace' to revalidate. -->

# Backend Modules

> **Pure Rust backend**: All backend logic lives in `crates/`. The SolidJS frontend (`src/`) communicates with Rust via Tauri IPC commands (`src-tauri/`).
>
> For detailed crate descriptions, dependencies, and key types, see [crate-map.md](crate-map.md).

## Rust Crates (`crates/`)

20 Rust crates (358 files, ~104K LOC, 1,798 tests):

| Crate | Purpose |
|-------|---------|
| `ava-agent` | Agent execution loop, tool calling, stuck detection, mid-stream messaging |
| `ava-auth` | OAuth (PKCE, device code), Copilot token exchange |
| `ava-cli-providers` | External CLI agent integration (Claude Code, etc.) |
| `ava-codebase` | Code indexing (BM25 + PageRank), dependency graph, semantic search |
| `ava-config` | Config management, credentials, model catalog, agent configs |
| `ava-context` | Token tracking, context condensation |
| `ava-db` | SQLite connection pool, data models |
| `ava-extensions` | Extension system (hooks, native/WASM loaders) |
| `ava-llm` | LLM providers (8), connection pool, circuit breaker, retry, routing |
| `ava-mcp` | Model Context Protocol client/server (stdio + HTTP) |
| `ava-memory` | Persistent key-value memory (SQLite + FTS5) |
| `ava-permissions` | Permission rules, bash command classifier, risk levels |
| `ava-platform` | File system and shell abstractions |
| `ava-praxis` | Multi-agent orchestration (Director pattern), ACP, artifacts |
| `ava-sandbox` | OS-level sandboxing (bwrap/sandbox-exec) |
| `ava-session` | Session persistence (SQLite), bookmarks, conversation tree |
| `ava-tools` | Tool trait, registry, 6 default + 8 extended tools, custom tools |
| `ava-tui` | CLI/TUI binary (Ratatui + Crossterm) |
| `ava-types` | Shared types (Message, Session, ToolCall, AvaError) |
| `ava-validator` | Code validation pipeline with retry |

## Desktop Frontend (`src/`)

SolidJS application served by Tauri. Communicates with the Rust backend exclusively through Tauri IPC commands defined in `src-tauri/src/commands/`.

## Tauri Commands (`src-tauri/`)

Rust command modules that bridge the SolidJS frontend to the Rust crate ecosystem. Each command is registered in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`.

---

*See also: [crate-map.md](crate-map.md), [architecture-guide.md](architecture-guide.md), [CLAUDE.md](../../CLAUDE.md)*
