<!-- Last verified: 2026-03-05. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

# AVA Architecture & Conventions

## Quick Commands

```bash
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

If `npm run tauri dev` fails with `ENOSPC` watcher errors on Linux, see `docs/troubleshooting.md`.

Release verification:

```bash
npm run tauri build
cargo test --workspace
```

## Architecture

AVA is migrating to a **Rust-first architecture**. All new CLI/agent code MUST be Rust.

- **CLI/TUI**: Pure Rust binary (`crates/ava-tui/`) — Ratatui + Crossterm + Tokio
- **Agent runtime**: Rust (`crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-commander/`)
- **Desktop frontend**: SolidJS + TypeScript (stays — Tauri requires web frontend)
- **Desktop backend**: Rust via Tauri commands (`src-tauri/`)
- **Legacy TS packages**: `packages/core-v2/` and `packages/extensions/` are desktop-only; do NOT add new features here

The TypeScript layer (`packages/`) is retained only for the Tauri desktop webview. The CLI is 100% Rust — no Node.js dependency.

## Project Structure

```text
AVA/
├── crates/                   # ~21 Rust crates (agent stack + TUI + services)
│   ├── ava-tui/              # CLI/TUI binary (Ratatui) — THE primary interface
│   ├── ava-agent/            # Agent execution loop + reflection
│   ├── ava-llm/              # LLM providers (Anthropic, OpenAI, Gemini, Ollama, OpenRouter)
│   ├── ava-tools/            # Tool trait + registry + core tools (read/write/edit/bash/glob/grep)
│   ├── ava-commander/        # Multi-agent orchestration (Praxis)
│   ├── ava-session/          # Session persistence (SQLite + FTS5)
│   ├── ava-memory/           # Persistent memory/recall
│   ├── ava-config/           # Configuration management
│   ├── ava-permissions/      # Tool permission checks
│   ├── ava-sandbox/          # Command sandboxing (bwrap/sandbox-exec)
│   ├── ava-platform/         # File system + shell abstractions
│   ├── ava-context/          # Context window management + condensation
│   ├── ava-codebase/         # Code indexing (BM25 + PageRank)
│   ├── ava-db/               # SQLite connection pool
│   ├── ava-types/            # Shared types
│   ├── ava-logger/           # Structured logging
│   └── ...                   # ava-extensions, ava-validator, ava-mcp, ava-lsp
├── packages/                 # TypeScript — DESKTOP ONLY (do not use for CLI)
│   ├── core-v2/              # desktop orchestration kernel
│   ├── extensions/           # desktop extension modules
│   ├── core/                 # compatibility shim
│   ├── platform-node/
│   └── platform-tauri/
├── src/                      # desktop frontend (SolidJS)
├── src-tauri/                # desktop native host + Tauri commands
├── cli/                      # legacy TS CLI (being replaced by crates/ava-tui)
└── tests/
```

## Tool Surface (~41)

| Group | Count | Notes |
|---|---:|---|
| Core tools | 6 | read, write, edit, bash, glob, grep |
| Extended tools | ~16 | multiedit, apply-patch, task, webfetch/search, question, completion, plan_enter, plan_exit |
| Git tools | 4 | status/diff/commit helper flows |
| Memory tools | 4 | remember/recall/search/recent |
| LSP tools | 9 | diagnostics, definition, references, rename, hover, symbols, format |
| Recall tools | 1 | recall |
| Delegate tools | 4 | delegate_coder, delegate_reviewer, delegate_researcher, delegate_explorer |

Total: ~41 static tools (plus dynamic MCP and custom tools)

## Extensions Map (20)

1. `agent-modes`
2. `commander`
3. `context`
4. `diff`
5. `git`
6. `hooks`
7. `instructions`
8. `lsp`
9. `mcp`
10. `memory`
11. `models`
12. `permissions`
13. `plugins`
14. `prompts`
15. `providers`
16. `recall`
17. `server`
18. `slash-commands`
19. `tools-extended`
20. `validator`

Runtime extension count explanation:
- Feature extensions: 20 total (always loaded)
- Provider extensions: ~15 at runtime (sub-extensions within providers/)
- Disabled in CLI: `lsp`, `mcp`, `server`, `litellm` (4)
- Typical CLI activation: ~31 extensions (20 + 15 - 4)

## Rust-First Rule

**All new agent/CLI features MUST be implemented in Rust.** Do not add new features to `packages/` (TypeScript).

- New tools → `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers → `crates/ava-llm/src/providers/`
- New agent features → `crates/ava-agent/` or `crates/ava-commander/`
- TUI features → `crates/ava-tui/`
- Configuration → `crates/ava-config/`

The `dispatchCompute` pattern is **deprecated for new work**. It remains in `packages/` for the desktop app only. The CLI calls Rust crates directly — no IPC, no bridge.

### Legacy dispatchCompute (desktop only)

Still used in `packages/extensions/` for Tauri desktop features:

```typescript
dispatchCompute<T>(rustCommand, rustArgs, tsFallback)
```

Do NOT use this pattern for new CLI/agent features. Write Rust directly.

## Middleware Priority

Middleware runs in priority order (lower number = earlier execution):

| Middleware | Priority | Purpose |
|------------|----------|---------|
| sandbox | 3 | Route install-class commands through sandbox |
| reliability | 5 | Detect stuck loops, recovery handling |
| error-recovery | 15 | Checkpoint recovery before destructive actions |
| lsp-diagnostics | 20 | LSP-based diagnostics validation |

Register middleware via `api.addToolMiddleware({ priority, before, after })`.

## Code Style

### TypeScript / SolidJS

- strict mode, no `any`
- explicit exported return types
- SolidJS only in `src/` (no React patterns)
- use `.js` import suffix where package config requires it
- Biome for formatting, ESLint + oxlint for linting

### Rust

- prefer small command modules in `src-tauri/src/commands/`
- add serde `rename_all = "camelCase"` for TS IPC compatibility
- register every new command in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`
- keep error strings actionable and deterministic

## Common Workflows

### Add a Tool (Rust)

1. Create `crates/ava-tools/src/core/{tool_name}.rs`
2. Implement `Tool` trait (`name`, `description`, `parameters`, `execute`)
3. Register in `crates/ava-tools/src/core/mod.rs` → `register_core_tools()`
4. Add tests in `crates/ava-tools/tests/`
5. `cargo test -p ava-tools`

### Add an LLM Provider (Rust)

1. Create `crates/ava-llm/src/providers/{provider_name}.rs`
2. Implement `LLMProvider` trait (5 methods: generate, generate_stream, estimate_tokens, estimate_cost, model_name)
3. Register in provider module
4. Add tests
5. `cargo test -p ava-llm`

### Add Middleware (Rust)

1. Implement `Middleware` trait in `crates/ava-tools/src/`
2. Set explicit priority
3. Register via `ToolRegistry::add_middleware()`
4. Add ordering/behavior tests

### Add Desktop Feature (TypeScript — desktop only)

1. Implement in `packages/extensions/`
2. Register on activation
3. Optionally add Rust hotpath via `src-tauri/src/commands/` + `dispatchCompute`

## Documentation Priority

1. `CLAUDE.md` (this file)
2. `docs/backend.md`
3. `docs/troubleshooting.md`
4. `docs/plugins/PLUGIN_SDK.md`
5. `docs/reference-code/`
