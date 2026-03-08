<!-- Last verified: 2026-03-07. Run 'cargo test --workspace' to revalidate. -->

# Backend Overview

> **Note**: AVA is migrating to a Rust-first architecture. This document describes the hybrid state during transition.
>
> - **New CLI/agent features**: Write in `crates/` (Rust)
> - **Desktop extensions**: Still in `packages/core-v2/` + `packages/extensions/` (TypeScript)
>
> See `CLAUDE.md` for current architecture guidance.

## Runtime Model

AVA runs a hybrid backend:

- `packages/core-v2/`: minimal execution kernel (agent loop, tools, extension API)
- `packages/extensions/`: feature modules layered onto core-v2
- Rust/Tauri commands (`src-tauri/src/commands/`) for compute and safety hotpaths

`packages/core/` remains as a compatibility re-export shim.

## Current Shape

- Built-in extensions: ~20
- Tool surface: ~41 tools

### Runtime Extension Count

Typical runtime extension activation count is ~31:
- 20 feature extensions (always enabled)
- ~16 provider extensions (varies by configuration)
- Minus 4 commonly disabled: `lsp`, `mcp`, `server`, `litellm`

### Extension Module Index (~20)

- agent-modes
- commander
- context
- diff
- git
- hooks
- instructions
- lsp
- mcp
- memory
- models
- permissions
- plugins
- prompts
- providers
- recall
- server
- slash-commands
- tools-extended
- validator

### Rust Crate Index (21)

1. `ava-agent` — Agent execution loop
2. `ava-cli-providers` — CLI provider management
3. `ava-codebase` — Code indexing and search
4. `ava-commander` — Multi-agent orchestration (Praxis)
5. `ava-config` — Configuration management
6. `ava-context` — Context window management
7. `ava-db` — SQLite connection pool
8. `ava-extensions` — Extension system
9. `ava-llm` — LLM providers and clients
10. `ava-logger` — Structured logging
11. `ava-lsp` — Language Server Protocol
12. `ava-mcp` — Model Context Protocol
13. `ava-memory` — Persistent memory/recall
14. `ava-permissions` — Permission system
15. `ava-platform` — Platform abstractions
16. `ava-sandbox` — Command sandboxing
17. `ava-session` — Session persistence
18. `ava-tools` — Tool trait and registry
19. `ava-tui` — CLI/TUI binary (Ratatui)
20. `ava-types` — Shared types
21. `ava-validator` — Validation pipeline

See `crates/` directory for source. Run `cargo test --workspace` for test status.

## Rust Hotpath Pattern

> **Note:** `dispatchCompute` is still used in the desktop Tauri app. It is **deprecated for new CLI/agent features** — write Rust directly in `crates/`.

Use `dispatchCompute<T>(rustCommand, rustArgs, tsFallback)` for performance-sensitive logic in the desktop app only.

- Tauri desktop runtime: execute Rust command
- Node/CLI runtime: use TypeScript fallback

Examples include edit/grep compute paths, permission evaluation, and validation checks.

## Extension Responsibilities

Common extension responsibilities include:

- middleware (`before` / `after`) with explicit priority
- tool registration
- settings registration and settings sync
- event subscriptions through the bus

## Safety and Reliability Layers

- sandbox routing for install-class shell commands
- dynamic permission learning with dangerous-command safeguards
- checkpoint refs for recovery before destructive actions
- agent reliability middleware for stuck-loop and recovery handling

## Where To Read Next

- `docs/backend/architecture-guide.md`
- `docs/troubleshooting.md`
- `CLAUDE.md`
