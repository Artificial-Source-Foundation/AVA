<!-- Last verified: 2026-03-12. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

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
- **Agent runtime**: Rust (`crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-praxis/`)
- **Desktop frontend**: SolidJS + TypeScript (stays — Tauri requires web frontend)
- **Desktop backend**: Rust via Tauri commands (`src-tauri/`)
- **Legacy TS packages**: `packages/core-v2/` and `packages/extensions/` are desktop-only; do NOT add new features here

The TypeScript layer (`packages/`) is retained only for the Tauri desktop webview. The CLI is 100% Rust — no Node.js dependency.

### Mid-Stream Messaging

Three-tier message queue for interacting with the agent while it runs:

| Tier | TUI Trigger | Headless Flag | Injection Point |
|------|-------------|---------------|-----------------|
| **Steering** | Enter | (stdin) | After current tool — skips remaining tools |
| **Follow-up** | Alt+Enter | `--follow-up` | After agent completes current task |
| **Post-complete** | Ctrl+Alt+Enter | `--later` / `--later-group` | After agent stops — grouped pipeline (G1, G2, G3...) |

Commands: `/later` (add post-complete message), `/queue` (view/manage pending messages). Status bar shows `[N queued]`.

## Project Structure

```text
AVA/
├── crates/                   # ~20 Rust crates (agent stack + TUI + services)
│   ├── ava-tui/              # CLI/TUI binary (Ratatui) — THE primary interface
│   ├── ava-agent/            # Agent execution loop + reflection
│   ├── ava-llm/              # LLM providers (Anthropic, OpenAI-compatible, Gemini, Ollama, OpenRouter, Copilot, Inception)
│   ├── ava-tools/            # Tool trait + registry + core tools (read/write/edit/bash/glob/grep)
│   ├── ava-praxis/        # Multi-agent orchestration (Praxis)
│   ├── ava-session/          # Session persistence (SQLite + FTS5)
│   ├── ava-memory/           # Persistent memory/recall
│   ├── ava-auth/             # Credential and auth flows
│   ├── ava-config/           # Configuration management
│   ├── ava-permissions/      # Tool permission checks
│   ├── ava-sandbox/          # Command sandboxing (bwrap/sandbox-exec)
│   ├── ava-platform/         # File system + shell abstractions
│   ├── ava-context/          # Context window management + condensation
│   ├── ava-codebase/         # Code indexing (BM25 + PageRank)
│   ├── ava-db/               # SQLite connection pool
│   ├── ava-types/            # Shared types
│   ├── ava-validator/        # Validation utilities
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

## Tool Surface (6 built-in + 7 extended)

Tools are organized into tiers. Only **Default** tools are sent to the LLM by default; Extended tools are registered and executable but not included in the system prompt unless `extended_tools` is enabled.

Tool philosophy: keep the default surface as lean as possible. AVA's out-of-the-box default set should stay capped at 6 tools (`read`, `write`, `edit`, `bash`, `glob`, `grep`). New tool capabilities should normally ship as **Extended**, MCP, plugin, or custom tools rather than expanding the default set.

| Tier | Count | Tools |
|---|---:|---|
| Default | 6 | read, write, edit, bash, glob, grep |
| Extended | 7 | apply_patch, web_fetch, multiedit, test_runner, lint, diagnostics, git |

Plugin-tier capabilities should normally ship via MCP servers or TOML custom tools, not by expanding the compiled default tool surface.

Additional tools are registered separately (todo_read/write, question, task, codebase_search, memory tools, session tools) and always available when their dependencies are initialized.

Total: 6 built-in tools by default, 7 extended opt-in tools, plus always-available tasking/session helpers + dynamic MCP tools + TOML custom tools (`~/.ava/tools/`, `.ava/tools/`)

## Project Instructions

AVA auto-discovers instruction files and injects them into the agent's system prompt (`crates/ava-agent/src/instructions.rs`):

1. `~/.ava/AGENTS.md` — global rules (all projects)
2. `AGENTS.md` — project-level rules (root)
3. `.ava/rules/*.md` — modular rule files (alphabetical)

Also reads: `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md` for cross-tool compatibility.

Format: plain markdown. No special syntax needed. Each file is prefixed with `# From: <filepath>` in the prompt. Paths are deduplicated by canonical path. Both main agent and sub-agents receive the instructions.

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
- New agent features → `crates/ava-agent/` or `crates/ava-praxis/`
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

1. Decide the tier first. New tools should normally be `Extended`; only expand the default 6 with a strong justification.
2. Create `crates/ava-tools/src/core/{tool_name}.rs`
3. Implement `Tool` trait (`name`, `description`, `parameters`, `execute`)
4. Register in `crates/ava-tools/src/core/mod.rs` → `register_core_tools()` using the appropriate tiering path
5. Add tests in `crates/ava-tools/tests/`
6. `cargo test -p ava-tools`

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

# Multi-agent / director (Praxis)
cargo run --bin ava -- "goal" --headless --multi-agent --provider openrouter --model anthropic/claude-haiku-4.5

# Workflow pipeline
cargo run --bin ava -- "goal" --headless --workflow plan-code-review --provider openrouter --model anthropic/claude-haiku-4.5

# Mid-stream messaging (headless)
cargo run --bin ava -- "goal" --headless --follow-up "also run tests" --provider openrouter --model anthropic/claude-haiku-4.5
cargo run --bin ava -- "goal" --headless --later "commit when done" --provider openrouter --model anthropic/claude-haiku-4.5
cargo run --bin ava -- "goal" --headless --later "review" --later-group 2 "commit" --provider openrouter --model anthropic/claude-haiku-4.5
```

**Default test model**: `anthropic/claude-haiku-4.5` ($1/$5 per M tokens — cheapest Western SOTA with full tool use support).

## Documentation Priority

1. `CLAUDE.md` (this file) — architecture, conventions, commands
2. `AGENTS.md` — AI agent instructions
3. `docs/development/roadmap.md` — sprint roadmap (11-66+) and active delivery lanes
4. `docs/development/backlog.md` — active backlog and validation status
5. `docs/development/epics.md` — completed and planned epics
6. `docs/development/v3-plan.md` — paired backend/frontend plan toward v3
7. `docs/development/test-matrix.md` — E2E test verification
8. `docs/development/sprints/` — sprint prompts
9. `docs/development/research/` — competitor analysis
10. `docs/architecture/` — system design docs
11. `docs/reference-code/` — competitor source code notes (12 projects)
