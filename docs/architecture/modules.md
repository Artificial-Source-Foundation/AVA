<!-- Last verified: 2026-03-16. Run 'cargo test --workspace' to revalidate. -->

# Backend Modules

> **Pure Rust backend**: All backend logic lives in `crates/`. The SolidJS frontend (`src/`) communicates with Rust via Tauri IPC commands (`src-tauri/`).

## Rust Crates (`crates/`)

~22 Rust crates make up the CLI, agent runtime, and desktop backend:

1. `ava-agent` — Agent execution loop + reflection
2. `ava-auth` — OAuth and credential flows
3. `ava-cli-providers` — CLI provider management
4. `ava-codebase` — Code indexing (BM25 + PageRank)
5. `ava-config` — Configuration management
6. `ava-context` — Context window management
7. `ava-db` — SQLite connection pool
8. `ava-extensions` — Extension system
9. `ava-llm` — LLM providers (Anthropic, Copilot, Gemini, Inception, Ollama, OpenAI, OpenRouter, mock)
10. `ava-mcp` — Model Context Protocol
11. `ava-memory` — Persistent memory/recall
12. `ava-permissions` — Permission system
13. `ava-platform` — Platform abstractions (fs, shell)
14. `ava-praxis` — Multi-agent orchestration (Praxis)
15. `ava-sandbox` — Command sandboxing (bwrap/sandbox-exec)
16. `ava-session` — Session persistence (SQLite + FTS5)
17. `ava-tools` — Tool trait + registry + tiered core tools
18. `ava-tui` — CLI/TUI binary (Ratatui + Crossterm)
19. `ava-types` — Shared types
20. `ava-validator` — Validation pipeline

**Tool surface**: 6 built-in tools by default, 8 extended tools when enabled, plus separately-registered task/todo/question helpers and dynamic MCP/custom tools.

## Desktop Frontend (`src/`)

SolidJS application served by Tauri. Communicates with the Rust backend exclusively through Tauri IPC commands defined in `src-tauri/src/commands/`.

## Tauri Commands (`src-tauri/`)

Rust command modules that bridge the SolidJS frontend to the Rust crate ecosystem. Each command is registered in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`.

---

*See also: [architecture-guide.md](architecture-guide.md), [CLAUDE.md](../../CLAUDE.md)*
