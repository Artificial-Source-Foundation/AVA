<!-- Last verified: 2026-03-08. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

# AVA Architecture & Conventions (v2.1)

## Quick Commands

```bash
# Rust CLI/agent (primary)
cargo test --workspace
cargo clippy --workspace
cargo run --bin ava             # interactive TUI
cargo run --bin ava -- --help   # see all flags

# Desktop app (TypeScript + Tauri)
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

If `npm run tauri dev` fails with `ENOSPC` watcher errors on Linux, see `docs/troubleshooting/`.

Release verification:

```bash
cargo test --workspace
cargo clippy --workspace
npm run tauri build
```

## Architecture

AVA uses a **Rust-first architecture**. All new CLI/agent code MUST be Rust.

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
│   ├── ava-validator/        # Validation utilities
│   ├── ava-lsp/              # LSP client integration
│   ├── ava-mcp/              # MCP (Model Context Protocol) support
│   ├── ava-extensions/       # Extension system
│   └── ava-cli-providers/    # CLI provider resolution
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

## Tool Surface (19 built-in)

| Group | Count | Tools |
|---|---:|---|
| Core | 11 | read, write, edit, bash, glob, grep, multiedit, apply_patch, test_runner, lint, diagnostics |
| Memory | 3 | remember, recall, memory_search |
| Session | 3 | session_search, session_list, session_load |
| Codebase | 1 | codebase_search |
| Git | 1 | git_read (review subcommand only) |

Total: 19 built-in tools + dynamic MCP tools + TOML custom tools (`~/.ava/tools/`, `.ava/tools/`)

## Extensions Map (Desktop Only)

The following 20 extensions are part of the **TypeScript desktop layer** (`packages/extensions/`). They do NOT apply to the Rust CLI.

<details>
<summary>Desktop extensions list</summary>

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

</details>

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

Register middleware via `ToolRegistry::add_middleware()` (Rust) or `api.addToolMiddleware({ priority, before, after })` (desktop TS).

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

## CLI Testing

Use OpenRouter for smoke tests. Credentials at `~/.ava/credentials.json`.

```bash
# Smoke test (cheapest Western SOTA)
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3

# Quality test
cargo run --bin ava -- "goal" --headless --provider openrouter --model anthropic/claude-sonnet-4

# Multi-agent / commander
cargo run --bin ava -- "goal" --headless --multi-agent --provider openrouter --model anthropic/claude-haiku-4.5

# Workflow pipeline
cargo run --bin ava -- "goal" --headless --workflow plan-code-review --provider openrouter --model anthropic/claude-haiku-4.5
```

**Default test model**: `anthropic/claude-haiku-4.5` ($1/$5 per M tokens — cheapest Western SOTA with full tool use support).

## Documentation Priority

1. `CLAUDE.md` (this file) — architecture, conventions, commands
2. `AGENTS.md` — AI agent instructions
3. `docs/development/roadmap.md` — sprint roadmap (11-50+)
4. `docs/development/test-matrix.md` — E2E test verification
5. `docs/development/sprints/` — sprint prompts
6. `docs/development/research/` — competitor analysis
7. `docs/architecture/` — system design docs
8. `docs/reference-code/` — competitor source code notes (12 projects)
