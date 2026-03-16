<!-- Last verified: 2026-03-16. Run 'just check && npm run tauri build' to revalidate. -->

# AVA Architecture & Conventions (v3)

## Quick Commands

```bash
# Rust CLI/agent (primary) — via just (see Justfile)
just check                      # fmt + clippy + nextest (all-in-one)
just test                       # cargo nextest run --workspace
just lint                       # cargo clippy --workspace
just fmt                        # cargo fmt --all
just run                        # interactive TUI
just headless "goal"            # headless mode
just build-release              # optimized binary
just doc                        # check doc builds
just cov                        # coverage report (requires cargo-llvm-cov)

# Or raw cargo
cargo test --workspace
cargo clippy --workspace
cargo run --bin ava             # interactive TUI
cargo run --bin ava -- --help   # see all flags

# Desktop app (SolidJS + Tauri)
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
```

If `npm run tauri dev` fails with `ENOSPC` watcher errors on Linux, see `docs/troubleshooting/`.

Release verification:

```bash
just check
npm run tauri build
```

## Architecture

AVA uses a **Rust-first architecture**. All agent, CLI, and backend code is Rust.

- **CLI/TUI**: Pure Rust binary (`crates/ava-tui/`) — Ratatui + Crossterm + Tokio
- **Agent runtime**: Rust (`crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-praxis/`)
- **Desktop frontend**: SolidJS + TypeScript (`src/`) — calls Rust directly via Tauri IPC
- **Desktop backend**: Rust via Tauri commands (`src-tauri/src/commands/`)

The desktop app follows the same backend path as the CLI: SolidJS frontend invokes Tauri IPC commands, which call into the shared Rust crates directly. There is no TypeScript orchestration layer — `packages/` has been deleted.

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
│   ├── ava-praxis/           # Multi-agent orchestration (Praxis)
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
├── src/                      # Desktop frontend (SolidJS → Tauri IPC → Rust)
├── src-tauri/                # Tauri host + Rust IPC commands
└── tests/
```

## Tool Surface (6 built-in + 8 extended)

Tools are organized into tiers. Only **Default** tools are sent to the LLM by default; Extended tools are registered and executable but not included in the system prompt unless `extended_tools` is enabled.

Tool philosophy: keep the default surface as lean as possible. AVA's out-of-the-box default set should stay capped at 6 tools (`read`, `write`, `edit`, `bash`, `glob`, `grep`). New tool capabilities should normally ship as **Extended**, MCP, plugin, or custom tools rather than expanding the default set.

| Tier | Count | Tools |
|---|---:|---|
| Default | 6 | read, write, edit, bash, glob, grep |
| Extended | 8 | apply_patch, web_fetch, web_search, multiedit, ast_ops, lsp_ops, code_search, git_read |

Plugin-tier capabilities should normally ship via MCP servers or TOML custom tools, not by expanding the compiled default tool surface.

Additional tools are registered separately (todo_read/write, question, task, codebase_search, memory tools, session tools) and always available when their dependencies are initialized.

Total: 6 built-in tools by default, 8 extended opt-in tools, plus always-available tasking/session helpers + dynamic MCP tools + TOML custom tools (`~/.ava/tools/`, `.ava/tools/`)

## Project Instructions

AVA auto-discovers instruction files and injects them into the agent's system prompt (`crates/ava-agent/src/instructions.rs`):

1. `~/.ava/AGENTS.md` — global rules (all projects)
2. `AGENTS.md` — project-level rules (root)
3. `.ava/rules/*.md` — modular rule files (alphabetical)

Also reads: `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md` for cross-tool compatibility.

Format: plain markdown. No special syntax needed. Each file is prefixed with `# From: <filepath>` in the prompt. Paths are deduplicated by canonical path. Both main agent and sub-agents receive the instructions.

## Workspace Trust

AVA requires explicit trust before loading project-local config from untrusted repos:
- `.ava/mcp.json` — MCP servers
- `.ava/hooks/*.toml` — Lifecycle hooks
- `.ava/tools/*.toml` — Custom tools
- `.ava/commands/` — Custom slash commands
- `AGENTS.md`, `.ava/rules/*.md` — Project instructions
- `.ava/agents.toml` — Agent configuration
- `.ava/skills/` — Skill files

Trust a project: run `ava --trust` to approve.
Global config (`~/.ava/`) always loads.

## Rust-First Rule

**All code is Rust.** The TypeScript orchestration layer (`packages/`) has been deleted. The desktop frontend (`src/`) calls Rust crates directly via Tauri IPC commands.

- New tools → `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers → `crates/ava-llm/src/providers/`
- New agent features → `crates/ava-agent/` or `crates/ava-praxis/`
- TUI features → `crates/ava-tui/`
- Desktop commands → `src-tauri/src/commands/`
- Configuration → `crates/ava-config/`

## Middleware Priority

Middleware runs in priority order (lower number = earlier execution):

| Middleware | Priority | Purpose |
|------------|----------|---------|
| sandbox | 3 | Route install-class commands through sandbox |
| reliability | 5 | Detect stuck loops, recovery handling |
| error-recovery | 15 | Checkpoint recovery before destructive actions |
| lsp-diagnostics | 20 | LSP-based diagnostics validation |

Register middleware via `ToolRegistry::add_middleware()`.

## Code Style

### TypeScript / SolidJS (desktop frontend only)

- strict mode, no `any`
- explicit exported return types
- SolidJS only in `src/` (no React patterns)
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

### Add Desktop Feature

1. Add Rust command in `src-tauri/src/commands/{feature}.rs`
2. Register in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`
3. Invoke from SolidJS frontend via `@tauri-apps/api` (`invoke` / `listen`)

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

## Slash Commands

Verified handlers in `crates/ava-tui/src/app/commands.rs`:

| Command | Description |
|---------|-------------|
| `/model [provider/model]` | Show or switch model (alias: `/models`) |
| `/think [show\|hide]` | Toggle thinking block visibility |
| `/theme [name]` | Cycle or switch theme |
| `/permissions` | Toggle permission level |
| `/connect [provider]` | Add provider credentials (alias: `/providers`) |
| `/disconnect <provider>` | Remove provider credentials |
| `/mcp [list\|reload\|enable\|disable]` | Manage MCP servers |
| `/new [title]` | Start a new session |
| `/sessions` | Session picker (Ctrl+L) |
| `/commit` | Inspect commit readiness |
| `/export [filename]` | Export conversation (.md or .json) |
| `/copy [all]` | Copy last response to clipboard (Ctrl+Y) |
| `/btw [question]` | Side conversation branch (`/btw end` to restore) |
| `/hooks [list\|reload\|dry-run]` | Manage lifecycle hooks |
| `/tasks` | Show background task list |
| `/later <message>` | Queue a post-complete message |
| `/queue` | Show queued messages |
| `/shortcuts` | Show keyboard shortcuts (Ctrl+?, alias: `/keys`, `/keybinds`) |
| `/compact [focus]` | Compact conversation to save context window |
| `/clear` | Clear chat |
| `/help` | Show help |

## Documentation Priority

1. `CLAUDE.md` (this file) — architecture, conventions, commands
2. `AGENTS.md` — AI agent instructions
3. `docs/development/roadmap.md` — sprint roadmap and delivery status
4. `docs/development/backlog.md` — active backlog and validation status
5. `docs/development/epics.md` — completed and planned epics
6. `docs/development/v3-plan.md` — v3 plan (complete)
7. `docs/development/test-matrix.md` — E2E test verification
8. `docs/development/sprints/` — sprint prompts
9. `docs/development/research/` — competitor analysis
10. `docs/architecture/` — system design docs
11. `docs/reference-code/` — competitor source code notes (12 projects)
