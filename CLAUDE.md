<!-- Last verified: 2026-03-17. Run 'just check' to revalidate. -->

# AVA Architecture & Conventions (v3)

## Quick Commands

```bash
# Rust CLI/agent (primary) -- via just (see Justfile)
just check                      # fmt + clippy + nextest (all-in-one)
just test                       # cargo nextest run --workspace
just lint                       # cargo clippy --workspace
just fmt                        # cargo fmt --all
just run                        # interactive TUI
just headless "goal"            # headless mode

# Or raw cargo
cargo test --workspace
cargo clippy --workspace
cargo run --bin ava             # interactive TUI
cargo run --bin ava -- --help   # see all flags
cargo run --bin ava -- serve --port 8080  # web browser mode

# Desktop app (SolidJS + Tauri)
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
```

Release verification: `just check && npm run tauri build`

## Architecture

AVA uses a **Rust-first architecture**. All agent, CLI, and backend code is Rust.

- **CLI/TUI**: Pure Rust binary (`crates/ava-tui/`) -- Ratatui + Crossterm + Tokio
- **Web mode**: `ava serve` -- HTTP API + WebSocket server (axum), serves SolidJS frontend
- **Agent runtime**: Rust (`crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-praxis/`)
- **Desktop frontend**: SolidJS + TypeScript (`src/`) -- calls Rust directly via Tauri IPC
  - Key components: `TitleBar`, `AppShell`, `ActivityBar`, `MainArea`, `SidebarPanel`, `RightPanel`
  - Chat components: `ApprovalDock`, `QuestionDock`, `ToolListDialog`
  - Screens: `ProjectHub` (`project-hub/`), `OnboardingFlow` (`onboarding/`)
- **Desktop backend**: Rust via Tauri commands (`src-tauri/src/commands/`)

### Codebase Stats

- **21 Rust crates**, ~40K LOC, 1,513 tests
- **8 LLM providers**: Anthropic, OpenAI-compatible, Gemini, Ollama, OpenRouter, Copilot, Inception, Mock
- **6 default tools**: `read`, `write`, `edit`, `bash`, `glob`, `grep`
- **8 extended tools**: `apply_patch`, `web_fetch`, `web_search`, `multiedit`, `ast_ops`, `lsp_ops`, `code_search`, `git_read`
- **Dynamic tools**: MCP servers + TOML custom tools (`~/.ava/tools/`, `.ava/tools/`)

### Mid-Stream Messaging

Three-tier message queue for interacting with the agent while it runs:

| Tier | TUI Trigger | Headless Flag | Injection Point |
|------|-------------|---------------|-----------------|
| **Steering** | Enter | (stdin) | After current tool -- skips remaining tools |
| **Follow-up** | Alt+Enter | `--follow-up` | After agent completes current task |
| **Post-complete** | Ctrl+Alt+Enter | `--later` / `--later-group` | After agent stops -- grouped pipeline (G1, G2, G3...) |

Commands: `/later` (add post-complete message), `/queue` (view/manage pending messages).

## Project Structure

```text
AVA/
+-- crates/                   # 21 Rust crates (agent stack + TUI + services)
|   +-- ava-tui/              # CLI/TUI binary (Ratatui) -- THE primary interface
|   +-- ava-agent/            # Agent execution loop + reflection
|   +-- ava-llm/              # LLM providers (8 built-in)
|   +-- ava-tools/            # Tool trait + registry + 6 default + 8 extended tools
|   +-- ava-praxis/           # Multi-agent orchestration (Praxis)
|   +-- ava-permissions/      # Permission rules + bash command classifier
|   +-- ava-config/           # Config, credentials, model catalog
|   +-- ava-context/          # Token tracking + context condensation
|   +-- ava-mcp/              # Model Context Protocol client/server
|   +-- ava-plugin/           # Power plugin system (JSON-RPC, subprocess isolation)
|   +-- ava-session/          # Session persistence (SQLite)
|   +-- ava-memory/           # Persistent memory (SQLite + FTS5)
|   +-- ava-codebase/         # Code indexing (BM25 + PageRank)
|   +-- ava-auth/             # OAuth + credential flows
|   +-- ava-platform/         # File system + shell abstractions
|   +-- ava-sandbox/          # Command sandboxing (bwrap/sandbox-exec)
|   +-- ava-cli-providers/    # External CLI agent integration
|   +-- ava-extensions/       # Extension system (hooks, native/WASM)
|   +-- ava-db/               # SQLite connection pool
|   +-- ava-types/            # Shared types
|   +-- ava-validator/        # Validation pipeline
+-- src/                      # Desktop frontend (SolidJS -> Tauri IPC -> Rust)
+-- src-tauri/                # Tauri host + Rust IPC commands
+-- docs/                     # Documentation (see docs/README.md)
```

## Tool Surface

Keep the default set capped at 6. New capabilities should ship as Extended, MCP, plugin, or custom tools.

| Tier | Count | Tools |
|------|------:|-------|
| Default | 6 | read, write, edit, bash, glob, grep |
| Extended | 8 | apply_patch, web_fetch, web_search, multiedit, ast_ops, lsp_ops, code_search, git_read |

Additional helpers (todo_read/write, question, task, codebase_search, memory/session tools) are always available when initialized. Dynamic MCP tools and TOML custom tools load at runtime from `~/.ava/tools/`, `.ava/tools/`, `~/.ava/mcp.json`, `.ava/mcp.json`.

## Project Instructions

Auto-discovered and injected into the agent's system prompt (`crates/ava-agent/src/instructions.rs`):

1. `~/.ava/AGENTS.md` -- global rules
2. Ancestor walk: `AGENTS.md`/`CLAUDE.md` from outermost ancestor to `.git` boundary
3. Project root: `AGENTS.md`, `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md`
4. `.ava/AGENTS.md`, `.ava/rules/*.md` -- project-local rules (alphabetical)
5. `config.yaml` `instructions:` paths/globs
6. Skill files from `.claude/skills/`, `.agents/skills/`, `.ava/skills/`

## Workspace Trust

Project-local config requires explicit trust before loading:

- `.ava/mcp.json`, `.ava/hooks/*.toml`, `.ava/tools/*.toml`, `.ava/commands/`, `AGENTS.md`, `.ava/rules/*.md`, `.ava/agents.toml`, `.ava/skills/`

Trust a project: `ava --trust`. Global config (`~/.ava/`) always loads.

## Rust-First Rule

**All code is Rust.** The desktop frontend (`src/`) calls Rust crates via Tauri IPC.

- New tools: `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers: `crates/ava-llm/src/providers/`
- New agent features: `crates/ava-agent/` or `crates/ava-praxis/`
- TUI features: `crates/ava-tui/`
- Desktop commands: `src-tauri/src/commands/`
- Configuration: `crates/ava-config/`

## Praxis (Multi-Agent Orchestration)

Praxis is AVA's multi-agent system in `crates/ava-praxis/`. It uses a **Director → Leads → Workers** hierarchy.

### Hierarchy

- **Director**: IS the main chat. Crown icon, amber color. Orchestrates leads, relays user steering, shows plan inline.
- **Leads**: Domain-specific agents -- Backend, Frontend, QA, Research, Debug, DevOps, Fullstack. Professional role names ("Backend Lead", "QA Lead"). Split tasks across workers, review output before reporting.
- **Workers**: Jr. agents with fun first names -- Pedro, Sofia, Luna, Kai, Mira, Rio, Ash, Nico, Ivy, Juno, Zara, Leo. Named as "Pedro (Jr. Backend)", etc.

### Team Mode

- Activated via **Team button** in status bar.
- **Solo → Team**: Director creates plan, spawns leads and workers.
- **Team → Solo**: Only when all agents are stopped. "Stop All" → Director asks "What's on your mind?" → user switches.
- **Resume Team**: Director reviews progress, asks "Continue or replan?"
- Mode switches preserved in session history.

### Worktree Strategy

- Each Lead gets its own **git worktree** (workers share their lead's worktree).
- Leads assign specific files to workers to avoid intra-lead conflicts.
- When all leads finish → Director spawns a **Merge Worker**.
- Clean merge: automatic. Minor conflicts: Merge Worker resolves. Hard conflicts: Director shows user diffs.

### Error Handling (Tiered)

1. Tool error → Worker retries (auto, up to 2x)
2. LLM error → Lead switches to fallback model
3. Logic error → Lead reviews, spawns fix worker
4. Worker budget exhausted → Lead asks Director → Director asks user
5. Catastrophic → Director asks user

### Frontend Components

- `TeamPanel` — right sidebar: Director → Leads → Workers hierarchy with status, progress, stop buttons
- `TeamChatView` — read-only lead chat (workers, tool calls, review actions)
- `TeamStatusStrip` — status bar integration for team mode
- `SubagentCard` — individual agent status card
- `agent-team-bridge` — Rust PraxisEvent → Tauri → useAgent → team store → UI

### Key Tauri Commands (Planned)

- `start_delegation` — activate team mode, begin Praxis orchestration
- `get_praxis_status` — current hierarchy state, progress, budgets
- `cancel_praxis` — stop all agents, return to solo mode
- `steer_lead` — relay user input to a specific lead via Director

### Session Persistence

Artifacts saved to `.ava/praxis/{session-id}/{lead-name}/`. Sessions are resumable if interrupted.

## Middleware Priority

Lower number = earlier execution. Register via `ToolRegistry::add_middleware()`.

| Middleware | Priority | Purpose |
|------------|----------|---------|
| sandbox | 3 | Route install-class commands through sandbox |
| reliability | 5 | Detect stuck loops, recovery handling |
| error-recovery | 15 | Checkpoint recovery before destructive actions |
| lsp-diagnostics | 20 | LSP-based diagnostics validation |

## Code Style

### Rust

- keep error strings actionable and deterministic
- add serde `rename_all = "camelCase"` for Tauri IPC compatibility
- register new Tauri commands in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`

### TypeScript / SolidJS (desktop frontend only)

- strict mode, no `any`
- explicit exported return types
- SolidJS only in `src/` (no React patterns)
- Biome for formatting, ESLint + oxlint for linting

## Common Workflows

### Add a Tool (Rust)

1. Decide tier: default to Extended; only expand the default 6 with strong justification
2. Create `crates/ava-tools/src/core/{tool_name}.rs`
3. Implement `Tool` trait (`name`, `description`, `parameters`, `execute`)
4. Register in `register_core_tools()` with appropriate tiering
5. Add tests, run `cargo test -p ava-tools`

### Add an LLM Provider (Rust)

1. Create `crates/ava-llm/src/providers/{provider_name}.rs`
2. Implement `LLMProvider` trait (generate, generate_stream, estimate_tokens, estimate_cost, model_name)
3. Add tests, run `cargo test -p ava-llm`

### Add Middleware (Rust)

1. Implement `Middleware` trait in `crates/ava-tools/src/`
2. Set explicit priority
3. Register via `ToolRegistry::add_middleware()`

### Add Desktop Feature

1. Add Rust command in `src-tauri/src/commands/{feature}.rs`
2. Register in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`
3. Invoke from SolidJS via `invoke()`

## Slash Commands

| Command | Description |
|---------|-------------|
| `/model [provider/model]` | Show or switch model |
| `/think [show\|hide]` | Toggle thinking visibility |
| `/theme [name]` | Cycle or switch theme |
| `/permissions` | Toggle permission level |
| `/connect [provider]` | Add provider credentials |
| `/providers` | Show provider status |
| `/disconnect <provider>` | Remove provider credentials |
| `/mcp [list\|reload\|enable\|disable]` | Manage MCP servers |
| `/new [title]` | Start a new session |
| `/sessions` | Session picker (Ctrl+L) |
| `/commit` | Inspect commit readiness |
| `/export [filename]` | Export conversation |
| `/copy [all]` | Copy last response (Ctrl+Y) |
| `/btw [question]` | Side conversation branch |
| `/hooks [list\|reload\|dry-run]` | Manage lifecycle hooks |
| `/tasks` | Show background tasks |
| `/later <message>` | Queue post-complete message |
| `/queue` | Show queued messages |
| `/shortcuts` | Show keyboard shortcuts (Ctrl+?) |
| `/compact [focus]` | Compact conversation |
| `/clear` | Clear chat |
| `/help` | Show help |
| `/init` | Create project templates |
| `/rewind` | Conversation checkpoint history |

## Keyboard Shortcuts (Desktop App)

> **Note:** Desktop app shortcuts (below) differ from the TUI in some cases. TUI shortcuts are defined in `crates/ava-tui/`.

| Shortcut | Action |
|----------|--------|
| `Ctrl+/` or `Ctrl+K` | Command palette |
| `Ctrl+N` | New chat |
| `Ctrl+L` | Session switcher |
| `Ctrl+M` | Quick model picker |
| `Ctrl+Shift+M` | Model browser |
| `Ctrl+S` | Toggle sidebar |
| `Ctrl+T` | Cycle thinking level |
| `Ctrl+J` | Toggle bottom panel |
| `Ctrl+,` | Open settings |
| `Ctrl+R` | Voice toggle |
| `Ctrl+E` | Expanded editor |
| `Ctrl+F` | Search chat |
| `Ctrl+Y` | Copy last response |
| `Ctrl+\`` | Toggle terminal |
| `Ctrl+Shift+E` | Export chat |
| `Ctrl+Shift+Z` | Undo file change |
| `Ctrl+Shift+Y` | Redo file change |
| `Ctrl+Shift+S` | Stash prompt |
| `Ctrl+Shift+R` | Restore prompt |
| `Ctrl+Shift+C` | Save checkpoint |
| `Tab` / `Shift+Tab` | Cycle Plan/Act mode (when composer not focused) |
| `Alt+Enter` | Submit follow-up (Tier 2) |
| `Ctrl+Alt+Enter` | Submit post-complete (Tier 3) |

## CLI Testing

```bash
# Smoke test
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3

# Multi-agent
cargo run --bin ava -- "goal" --headless --multi-agent --provider openrouter --model anthropic/claude-haiku-4.5

# Mid-stream messaging
cargo run --bin ava -- "goal" --headless --follow-up "also run tests" --provider openrouter --model anthropic/claude-haiku-4.5
cargo run --bin ava -- "goal" --headless --later "commit when done" --provider openrouter --model anthropic/claude-haiku-4.5
```

## After Making Changes

When you complete a significant feature, bug fix, or refactor:

1. **Update `docs/development/CHANGELOG.md`** — add entry under current version
2. **Update `docs/development/backlog.md`** — mark completed items, add new ones
3. **Update this file (`CLAUDE.md`)** if architecture, crate count, tool count, or conventions changed
4. **Update `docs/architecture/crate-map.md`** if crates were added/removed
5. **Run `just check`** (or `cargo test --workspace && cargo clippy --workspace`) before committing

Do NOT let docs drift from code. Every PR-worthy change should include doc updates.

## Documentation

1. `CLAUDE.md` (this file) -- architecture, conventions, commands
2. `AGENTS.md` -- AI agent instructions for working on AVA
3. `docs/README.md` -- documentation entry point with crate map
4. `docs/codebase/` -- **complete codebase reference for all 21 crates, frontend, and plugins**
5. `docs/plugins.md` -- TOML custom tools and MCP server guide
6. `docs/architecture/crate-map.md` -- detailed crate dependency map
7. `docs/architecture/plugin-system.md` -- power plugin system design
8. `docs/development/CHANGELOG.md` -- version history
9. `docs/development/roadmap.md` -- roadmap and sprint history
10. `docs/development/backlog.md` -- open backlog items
11. `docs/ideas/` -- archived feature designs (not implemented)

**Quick links:** [Codebase docs](docs/codebase/README.md) • [Add a tool](docs/codebase/ava-tools.md) • [Add a provider](docs/codebase/ava-llm.md) • [Tauri commands](docs/codebase/tauri-commands.md) • [Plugins](docs/codebase/plugins.md)
