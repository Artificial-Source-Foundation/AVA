<!-- Last verified: 2026-04-02. Run 'just check' to revalidate. -->

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
pnpm tauri dev
pnpm lint
pnpm format:check
npx tsc --noEmit
```

Release verification: `just check && pnpm tauri build`

# Desktop release (signed build + publish)
TAURI_SIGNING_PRIVATE_KEY=$(cat ~/.tauri/ava.key) pnpm tauri build
# Then: gh release create v{VERSION} ... (see docs/releasing.md)

## Architecture

AVA uses a **Rust-first architecture**. All agent, CLI, and backend code is Rust.

### Two Products, One Backend

AVA ships as **two products** sharing one Rust agent runtime:

| | **TUI / CLI / Headless** | **Desktop / Web** |
|---|---|---|
| Binary | `ava` (pure Rust) | `ava` Tauri app / `ava serve` |
| UI | Ratatui + Crossterm | SolidJS + TypeScript |
| Config | `~/.ava/config.yaml` | localStorage + `config.yaml` via Tauri IPC |
| Themes | `~/.ava/themes/*.toml` (29 built-in) | CSS variables, accent presets |
| Keybindings | `~/.ava/keybindings.json` | `src/stores/shortcut-defaults.ts` |
| Settings count | ~70 fields + 15 keybind actions | ~100+ fields across 16 tabs |

**Configuration strategy (OpenCode-inspired):**
- **Shared config** (`config.yaml`): providers, models, features, permissions, HQ, voice, MCP, instructions — applies to both products
- **TUI-only config**: keybindings.json, themes/*.toml, TUI display prefs
- **Desktop-only config**: appearance (fonts, accent, density, border radius, dark style), notification sounds, sidebar order

Both products read `~/.ava/credentials.json` for provider API keys and `.ava/state.json` for per-project model history.

### Components

- **CLI/TUI**: Pure Rust binary (`crates/ava-tui/`) -- Ratatui + Crossterm + Tokio
- **Web mode**: `ava serve` -- HTTP API + WebSocket server (axum), serves SolidJS frontend
- **Agent runtime**: Rust (`crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-hq/`)
- **Desktop frontend**: SolidJS + TypeScript (`src/`) -- calls Rust directly via Tauri IPC
  - Key components: `TitleBar`, `AppShell`, `ActivityBar`, `MainArea`, `SidebarPanel`, `RightPanel`
  - Chat components: `ApprovalDock`, `QuestionDock`, `ToolListDialog`
  - Screens: `ProjectHub` (`project-hub/`), `OnboardingFlow` (`onboarding/`)
- **Desktop backend**: Rust via Tauri commands (`src-tauri/src/commands/`)

### Codebase Stats

- **22 Rust crates**, ~40K LOC, 1,962+ tests (0 failures)
- **22 LLM providers**: Anthropic (with prompt caching), OpenAI, ChatGPT (OAuth), Gemini, Ollama, OpenRouter, Copilot, Inception, Alibaba, Alibaba CN, ZAI, ZhipuAI, Kimi, MiniMax, MiniMax CN, Azure OpenAI, AWS Bedrock, xAI, Mistral, Groq, DeepSeek, Mock
- **9 default tools**: `read`, `write`, `edit` (15 strategies incl. ellipsis handling, 3-way merge + diff-match-patch), `bash`, `glob`, `grep`, `web_fetch`, `web_search`, `git_read`
- **Extended tools** (not auto-registered): `apply_patch`, `multiedit`, `ast_ops`, `lsp_ops`, `code_search`, `lint`, `test_runner` — available as plugins
- **1 agent tool**: `plan` (Plannotator-style inline plan editing via PlanBridge)
- **Dynamic tools**: MCP servers + TOML custom tools (`~/.ava/tools/`, `.ava/tools/`)
- **File snapshots**: Shadow git snapshots before file edits, `revert_file` capability for undoing changes
- **13 model families** with per-model system prompt tuning: Claude (Opus/Sonnet/Haiku), Codex (GPT-5.3), GPT (5.4/o3/o4), Gemini (3.1 Pro/3 Flash), DeepSeek (V3.2/R1), Mercury, Grok (3/4), GLM (4.7/5/5.1), Kimi (K2/K2.5), MiniMax (M2/M2.5), Qwen (3-Coder), Mistral (Large/Codestral), Local (llama/phi/gemma). Provider routing quirks (Copilot rate limits, OpenRouter backend variance) appended separately.
- **Key capabilities**: Anthropic prompt caching (`cache_control` on system + tools), auto-retry middleware (2x exponential backoff for read-only tools), stream silence timeout (90s configurable per-chunk reset), tiktoken-rs BPE token counting, tool schema pre-validation, persistent audit log (SQLite, opt-out), auto-compaction settings (toggle + threshold slider + compaction model), JSONL session logging (`~/.ava/log/`, on by default, 7-day rotation), rich edit error feedback (similar lines + "did you mean?"), SBPL injection hardening, env scrubbing in bash, rm -rf and find -delete blocking, context overflow auto-compact (12 overflow patterns with auto-retry), manual `/compact` summaries with collapsible desktop context cards, conversation repair, symlink escape detection in path guard, shadow git snapshots for file edit backups, incremental message persistence, retry-after header parsing, quota error classification, 100+ security patterns in command classifier, dual compaction visibility

### Mid-Stream Messaging

Three-tier message queue for interacting with the agent while it runs:

| Tier | TUI Trigger | Headless Flag | Injection Point |
|------|-------------|---------------|-----------------|
| **Queue** | Enter | (stdin) | Agent finishes current turn, then processes as new turn |
| **Interrupt** | Ctrl+Enter | `--follow-up` | Stops at tool boundary, sends immediately |
| **Post-complete** | Alt+Enter | `--later` / `--later-group` | After agent stops -- grouped pipeline (G1, G2, G3...) |

Cancel: Double-Escape aborts everything.

Commands: `/later` (add post-complete message), `/queue` (view/manage pending messages).

## Project Structure

```text
AVA/
+-- crates/                   # 22 Rust crates (agent stack + TUI + services)
|   +-- ava-tui/              # CLI/TUI binary (Ratatui) -- THE primary interface
|   +-- ava-agent/            # Agent execution loop + reflection
|   +-- ava-llm/              # LLM providers (22 built-in)
|   +-- ava-tools/            # Tool trait + registry + 9 default tools (extended available as plugins)
|   +-- ava-hq/               # Multi-agent orchestration (HQ)
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
|   +-- ava-acp/              # Agent Client Protocol (external agent integration)
|   +-- ava-extensions/       # Extension system (hooks, native/WASM)
|   +-- ava-lsp/              # On-demand LSP runtime scaffolding
|   +-- ava-db/               # SQLite connection pool
|   +-- ava-types/            # Shared types
|   +-- ava-validator/        # Validation pipeline
+-- src/                      # Desktop frontend (SolidJS -> Tauri IPC -> Rust)
+-- src-tauri/                # Tauri host + Rust IPC commands
+-- docs/                     # Documentation (see docs/README.md)
```

## Tool Surface

New capabilities should ship as Extended (available as plugins), MCP, or custom tools. The default set is now 9.

`ava-acp` is the external-agent bridge: Claude Code uses the Agent SDK stream format, while Codex and OpenCode are normalized from their JSONL event streams with resume-aware stale-session retry, typed external-session metadata, and structured block preservation so hidden subagents and HQ workers can switch runtimes without custom glue at each call site.

| Tier | Count | Tools |
|------|------:|-------|
| Default | 9 | read, write, edit, bash, glob, grep, web_fetch, web_search, git_read |
| Extended (plugin) | 7 | apply_patch, multiedit, ast_ops, lsp_ops, code_search, lint, test_runner |
| Agent | 1 | plan (HQ plan tool with PlanBridge for agent-to-TUI communication) |

Extended tools are **not auto-registered**; they must be explicitly loaded via plugin/MCP configuration. Additional helpers (todo_read/write, question, task, codebase_search, memory/session tools) are always available when initialized. Dynamic MCP tools and TOML custom tools load at runtime from `~/.ava/tools/`, `.ava/tools/`, `~/.ava/mcp.json`, `.ava/mcp.json`. File edits create shadow git snapshots enabling `revert_file` capability.

## Project Instructions

Auto-discovered and injected into the agent's system prompt (`crates/ava-agent/src/instructions.rs`):

1. `~/.ava/AGENTS.md` -- global rules
2. Ancestor walk: `AGENTS.md`/`CLAUDE.md` from outermost ancestor to `.git` boundary
3. Project root: `AGENTS.md`, `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md`
4. `.ava/AGENTS.md` eagerly; `.ava/rules/*.md` lazily on direct file touches (activate once per session, reset after compaction)
5. `config.yaml` `instructions:` paths/globs
6. Skill files from `.claude/skills/`, `.agents/skills/`, `.ava/skills/`

Path-scoped `.ava/rules/*.md` are intended to feel like Claude Code rules: keep them small, file-focused, and loaded only when AVA actually reads or edits matching files.

## Solo Hidden Delegation

Outside HQ team mode, the main agent can still delegate quietly through the `subagent` tool:

- Small single-file work keeps the main thread only (no hidden helper swarm).
- Broader tasks can unlock a bounded helper budget (typically 1-2 hidden subagents, 3 only on explicit delegation requests).
- `scout`, `explore`, `plan`, and `review` helpers run in enforced read-only specialist mode; `worker`, `build`, and `subagent` keep full editing access.

## Workspace Trust

Project-local config requires explicit trust before loading:

- `.ava/mcp.json`, `.ava/hooks/*.toml`, `.ava/tools/*.toml`, `.ava/commands/`, `AGENTS.md`, `.ava/rules/*.md`, `.ava/agents.toml`, `.ava/skills/`

Trust a project: `ava --trust`. Global config (`~/.ava/`) always loads.

## Rust-First Rule

**All code is Rust.** The desktop frontend (`src/`) calls Rust crates via Tauri IPC.

- New tools: `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers: `crates/ava-llm/src/providers/`
- New agent features: `crates/ava-agent/` or `crates/ava-hq/`
- TUI features: `crates/ava-tui/`
- Desktop commands: `src-tauri/src/commands/`
- Configuration: `crates/ava-config/`

## HQ (Multi-Agent Orchestration) — v2

HQ is AVA's multi-agent system in `crates/ava-hq/`. Uses a **Director -> Scouts -> Leads -> Workers** hierarchy with LLM-powered planning. See `CODEBASE_STRUCTURE.md` and the Project Structure section in this file for the current workspace map.

### Director Intelligence Levels

The Director is **LLM-powered** (not a code-driven router). It analyzes task complexity and adapts:

| Level | Complexity | Behavior |
|-------|-----------|----------|
| **1** | Simple (one-file fix) | Spawns one worker + one QA worker. No leads needed. |
| **2** | Medium (multi-file, clear scope) | Sends scouts, creates plan, user reviews. Spawns 2-3 leads with workers. |
| **3** | Complex (major refactor, architecture) | Scouts + Board of Directors (3 SOTA models vote on approach). User approves plan. |

### Key Concepts

- **Scouts**: Lightweight agents (cheap model: Haiku/Flash/Mercury) that read codebase sections and produce summaries for the Director. Used before planning.
- **Board of Directors**: Opt-in for Level 3. Three different SOTA models (e.g., Opus, Gemini, GPT-5.4) each with a distinct analytical personality. One round of opinions based on scout reports, then vote. Director synthesizes.
- **Plan System**: Plannotator-style -- plan shown as structured message in chat, steps are clickable/reorderable/commentable. Works in both solo (regular AI) and Director modes. Plans saved to `.ava/plans/`.
- **Sequential Execution**: Lead manages worker order (workers do NOT self-claim). Parallel when tasks are independent (different files), sequential when dependencies exist.
- **QA at Every Level**: Each lead has QA workers. QA Lead for cross-lead merge verification. Workers verify their changes compile + pass tests.

### Smart Model Routing

| Role | Model Tier | Examples |
|------|-----------|----------|
| Scouts | Cheapest | Haiku, Flash, Mercury |
| Workers | Mid-tier | Sonnet, GPT-5.3 |
| Leads | Strong | Sonnet, Opus (complex) |
| Director | Strongest available | Opus, GPT-5.4 |
| Board | Top per provider | Best from each configured provider |

### Hierarchy & Naming

- **Director**: Crown icon, amber. IS the main chat.
- **Leads**: Professional role names -- "Backend Lead", "QA Lead", etc. (7 domains)
- **Workers**: Fun first names -- "Pedro (Jr. Backend)", "Sofia (Jr. Backend)", etc.
- **Scouts**: Ephemeral, no names needed.
- **Board members**: Named by model -- "Opus (Board)", "Gemini (Board)", "GPT (Board)"

### Team Mode & Worktrees

- Solo/Team switching via Team button. Mode switches preserved in session.
- Each Lead gets its own git worktree; workers share their lead's worktree.
- Merge Worker integrates lead worktrees. QA Lead reviews merged result.
- Artifacts saved to `.ava/hq/{session-id}/{lead-name}/`.

### Error Handling (Tiered)

1. Tool error → Worker retries (auto, up to 2x)
2. LLM error → Lead switches to fallback model
3. Logic error → Lead reviews, spawns fix worker
4. Worker budget exhausted → Lead asks Director → Director asks user
5. Catastrophic → Director asks user

> **Note:** HQ desktop wiring now extends past raw event forwarding: Tauri commands persist HQ epics/issues/comments/plans/agents/activity/director-chat state in SQLite, the frontend HQ store loads that live data instead of mock fixtures, and HQ settings are stored in `ava-config` under `config.hq`.

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

1. Decide tier: default to Extended (plugin); only expand the default 9 with strong justification
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
| `Ctrl+Enter` | Interrupt & send (while agent running) |
| `Alt+Enter` | Submit post-complete message |
| `Double-Escape` | Cancel agent (abort everything) |

## CLI Testing

**Always use `--provider openai --model gpt-5.4` for testing.** Never use gpt-4o or other old models.

```bash
# Smoke test
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openai --model gpt-5.4 --max-turns 3

# Multi-agent
cargo run --bin ava -- "goal" --headless --multi-agent --provider openai --model gpt-5.4

# Mid-stream messaging
cargo run --bin ava -- "goal" --headless --follow-up "also run tests" --provider openai --model gpt-5.4
cargo run --bin ava -- "goal" --headless --later "commit when done" --provider openai --model gpt-5.4

# Post-completion code review
cargo run --bin ava -- "goal" --headless --provider openai --model gpt-5.4 --auto-approve --review

# Verbose logging (stderr): -v info, -vv debug, -vvv trace
cargo run --bin ava -- -v "goal" --headless --provider openai --model gpt-5.4

# Focused benchmark slice (single task)
cargo run --bin ava --features benchmark -- --benchmark --provider openai --model gpt-5.4 --suite frontier --language rust --task-filter delegated_config_bugfix --max-turns 8
```

## Benchmarking Against OpenCode

```bash
# OpenCode headless (uses `run` subcommand + `--format json` for non-interactive)
opencode run "goal" --model openai/gpt-5.4 --format json --dir /path/to/project

# AVA headless equivalent
cargo run --bin ava -- "goal" --headless --provider openai --model gpt-5.4 --auto-approve --json

# Timed comparison
START=$(date +%s%N) && ava "goal" --headless --provider openai --model gpt-5.4 --auto-approve 2>&1; echo "MS=$(( ($(date +%s%N) - START) / 1000000 ))"
START=$(date +%s%N) && opencode run "goal" --model openai/gpt-5.4 --format json 2>&1; echo "MS=$(( ($(date +%s%N) - START) / 1000000 ))"
```

## After Making Changes

When you complete a significant feature, bug fix, or refactor:

1. **Update `CHANGELOG.md`** — add entry under current version
2. **Update `docs/backlog.md`** — mark completed items, add new ones
3. **Update this file (`CLAUDE.md`)** if architecture, crate count, tool count, or conventions changed
4. **Update `CODEBASE_STRUCTURE.md`** if the top-level repo structure or crate inventory changed materially
5. **Run `just check`** (or `cargo test --workspace && cargo clippy --workspace`) before committing

Do NOT let docs drift from code. Every PR-worthy change should include doc updates.

## Desktop Releases & Auto-Update

Signing key: `~/.tauri/ava.key` (private), pubkey in `src-tauri/tauri.conf.json`.
Full guide: `docs/releasing.md`.

```bash
# Build signed release
TAURI_SIGNING_PRIVATE_KEY=$(cat ~/.tauri/ava.key) pnpm tauri build

# Publish (uploads bundles + updater manifest)
VERSION=$(grep '"version"' src-tauri/tauri.conf.json | head -1 | grep -oP '[\d.]+')
git tag -a "v${VERSION}" -m "Release v${VERSION}" && git push origin "v${VERSION}"
gh release create "v${VERSION}" --title "AVA v${VERSION}" --generate-notes \
  src-tauri/target/release/bundle/deb/*.deb \
  src-tauri/target/release/bundle/appimage/*.AppImage.tar.gz \
  src-tauri/target/release/bundle/appimage/*.AppImage.tar.gz.sig
find src-tauri/target/release/bundle -name "latest.json" -exec gh release upload "v${VERSION}" {} \;
```

Users get auto-update prompts via `tauri-plugin-updater` checking GitHub Releases.

## Documentation

1. `CLAUDE.md` (this file) -- architecture, conventions, commands
2. `AGENTS.md` -- AI agent instructions for working on AVA
3. `docs/README.md` -- documentation entry point
4. `CHANGELOG.md` -- version history
5. `docs/backlog.md` -- open backlog items
6. `CODEBASE_STRUCTURE.md` -- lightweight repo map
7. `docs/plugins.md` -- TOML custom tools and MCP server guide
8. `docs/hq/README.md` -- HQ architecture and UX notes
9. `docs/releasing.md` -- desktop release & auto-update guide
10. `docs/troubleshooting/` -- platform-specific fixes
