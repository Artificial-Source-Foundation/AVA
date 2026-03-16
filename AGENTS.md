<!-- Last verified: 2026-03-16 -->
# AI Coding Agent Instructions (v3)

> Universal instructions for AI assistants working on AVA

---

## Quick Start

```bash
# Rust (primary) — via just (see Justfile)
just check                      # fmt + clippy + nextest (all-in-one)
just test                       # cargo nextest run --workspace
just lint                       # cargo clippy --workspace

# Or raw cargo
cargo test --workspace
cargo clippy --workspace

# Desktop (SolidJS + Tauri)
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

Read first: `CLAUDE.md`

---

## Project Overview

AVA is a multi-agent AI coding assistant with desktop app and CLI.

- **CLI/TUI**: Pure Rust binary (`crates/ava-tui/`) — Ratatui + Crossterm + Tokio
- **Agent runtime**: Rust crates (`crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-praxis/`)
- **Desktop**: Tauri 2 — SolidJS frontend → Tauri IPC → Rust crates

**IMPORTANT: All new features MUST be Rust. The desktop app calls Rust crates directly via Tauri IPC commands (`src-tauri/src/commands/`).**

---

## Architecture (Current)

### Repository Layout

```text
AVA/
├── crates/              # Rust crates — ALL backend logic (CLI + agent + desktop)
│   ├── ava-tui/         # CLI/TUI binary (Ratatui)
│   ├── ava-agent/       # Agent loop + reflection
│   ├── ava-llm/         # LLM providers (Anthropic, OpenAI-compatible, Gemini, Ollama, OpenRouter, Copilot, Inception)
│   ├── ava-tools/       # Tool trait + registry + core tools
│   ├── ava-praxis/      # Multi-agent orchestration
│   ├── ava-session/     # Session persistence
│   ├── ava-memory/      # Persistent memory/recall
│   └── ...              # 13 more crates
├── src/                 # Desktop frontend (SolidJS → Tauri IPC → Rust)
└── src-tauri/           # Tauri host + IPC commands (calls into crates/)
```

### Rust Backend

**All features are Rust.** Both the CLI and the desktop app use the same Rust crates.

- New tools → `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers → `crates/ava-llm/src/providers/`
- New agent features → `crates/ava-agent/` or `crates/ava-praxis/`
- TUI features → `crates/ava-tui/`
- Desktop commands → `src-tauri/src/commands/` (Tauri IPC into crates)

### Important Counts

- Rust crates: 20
- LLM providers: 7 (Anthropic, OpenAI, Gemini, Ollama, OpenRouter, Copilot, Inception)
- Built-in tools by default: 6 (`read`, `write`, `edit`, `bash`, `glob`, `grep`)
- Extended opt-in tools: 8 (`apply_patch`, `web_fetch`, `web_search`, `multiedit`, `ast_ops`, `lsp_ops`, `code_search`, `git_read`)
- Dynamic tools: MCP servers + TOML custom tools
- Project instructions: auto-discovered from `AGENTS.md`, `CLAUDE.md`, `.ava/rules/*.md`, etc.

### Lean Tool Surface

AVA should stay lean by default.

- Keep the out-of-the-box default tool set capped at 6: `read`, `write`, `edit`, `bash`, `glob`, `grep`
- New tool ideas should default to `Extended`, MCP, plugin, or custom-tool delivery
- Only promote a tool into the default 6 when it is broadly useful, low-risk, and clearly worth the extra prompt/tool-surface cost
- Prefer configuration and opt-in capability over shipping a large built-in default tool catalog

---

## Data Flow

```text
AgentExecutor.run(goal, context)
  -> prepare history/context
  -> model response
  -> parse + execute tools
  -> middleware before/after hooks
  -> emit events/usage
  -> completion/termination
```

Middleware priorities are contract-sensitive. Lower numeric priority runs earlier.

---

## Code Standards

### TypeScript

- Strict mode; no `any`
- Explicit return types for exported functions
- ESM imports with `.js` where required by package config

### Files

- Max 300 lines per file
- kebab-case filenames for non-components
- camelCase functions
- PascalCase component/types

### Components

- SolidJS only (no React patterns)
- Functional components
- Props as `{Name}Props`
- Use Solid primitives (`createSignal`, `Show`, `For`, `onCleanup`)

---

## Common Tasks

### Add Tool (Rust)

1. Decide the tier first. New tools should default to `Extended`; only propose joining the default 6 when the tool is broadly useful, low-risk, and worth the prompt cost.
2. Create `crates/ava-tools/src/core/{tool_name}.rs`
3. Implement `Tool` trait (`name`, `description`, `parameters`, `execute`)
4. Register in `register_core_tools()` using the appropriate tiering path
5. Add tests, run `cargo test -p ava-tools`

### Add LLM Provider (Rust)

1. Create `crates/ava-llm/src/providers/{provider}.rs`
2. Implement `LLMProvider` trait (generate, generate_stream, estimate_tokens, estimate_cost, model_name)
3. Add tests, run `cargo test -p ava-llm`

### Add Desktop Feature

1. Implement logic in the appropriate Rust crate under `crates/`
2. Expose via Tauri command in `src-tauri/src/commands/`
3. Register command in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`
4. Call from SolidJS frontend via `invoke()`

---

## Project Instructions

AVA auto-discovers instruction files at startup and injects them into the agent's system prompt. This means the contents of this file (`AGENTS.md`) are automatically loaded when AVA runs in this project.

**Discovery order:**

1. `~/.ava/AGENTS.md` — global user-level instructions (all projects)
2. Ancestor walk (monorepo support): `AGENTS.md` and `CLAUDE.md` from outermost ancestor down to the repo boundary (`.git`), then stop
3. Project root: `AGENTS.md`, `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md`
4. Project-local `.ava/AGENTS.md`
5. `.ava/rules/*.md` — modular rule files, sorted alphabetically
6. Config extras from `config.yaml` `instructions:` (supports explicit paths and glob patterns)
7. Skill files from `.claude/skills/`, `.agents/skills/`, `.ava/skills/` in both global (`~/`) and project scopes (global first)

Files are plain markdown (no special syntax). Each is prefixed with `# From: <filepath>` in the prompt. Duplicate paths are deduplicated. Both the main agent and sub-agents receive these instructions.

Cross-tool compatible: works with instruction files from Cursor, Claude Code, and GitHub Copilot.

Implementation: `crates/ava-agent/src/instructions.rs`

---

## Before Committing

- `just check` (or `cargo fmt --all --check && cargo clippy --workspace && cargo nextest run --workspace`)
- `npm run lint`
- `npm run format:check`
- `npx tsc --noEmit`
- `npm run test:run`

---

## CLI Testing with OpenRouter

An OpenRouter API key is configured at `~/.ava/credentials.json` for testing.

### Running CLI tests

```bash
cargo run --bin ava -- "your goal here" --headless --provider openrouter --model <model>
```

### Recommended models (via OpenRouter)

| Model | ID | Best for | Cost (input/output per M) |
|-------|-----|----------|------|
| **Gemini 3 Flash** | `google/gemini-3-flash-preview` | **Smoke tests, cheap SOTA** | $2 / $12 |
| Codex 5.3 | `openai/gpt-5.3-codex` | Best OpenAI coding model | $1.75/M |
| Claude Sonnet 4.6 | `anthropic/claude-sonnet-4.6` | General coding, tool use | $3/M |
| Claude Opus 4.6 | `anthropic/claude-opus-4.6` | Complex tasks, code review | $5/M |
| Kimi K2.5 | `moonshotai/kimi-k2.5` | Budget bulk work, 262K context | $0.45/M |
| GLM 5 | `z-ai/glm-5` | Budget bulk work, 200K context | $0.80/M |
| GLM 4.5 Air | `z-ai/glm-4.5-air` | Free tier testing | FREE |

For smoke tests, use `anthropic/claude-haiku-4.5` ($1/$5, cheapest Western SOTA with full tool use).
For quality verification, use `openai/gpt-5.3-codex` or `anthropic/claude-opus-4.6`.
For budget/bulk work, use `moonshotai/kimi-k2.5` or `z-ai/glm-5`.

---

## Do Not

- Commit secrets or credentials
- Add parent-directory imports
- Add TypeScript backend logic — all backend code is Rust
- Use React patterns in `src/`

---

## Documentation Priority

1. `CLAUDE.md` — primary architecture reference
2. `AGENTS.md` — AI agent instructions
3. `docs/development/roadmap.md` — sprint roadmap (11-66+) and active delivery lanes
4. `docs/development/backlog.md` — active backlog and validation status
5. `docs/development/epics.md` — completed and planned epics
6. `docs/development/v3-plan.md` — paired backend/frontend plan toward v3
7. `docs/development/sprints/` — current sprint prompts
8. `docs/development/research/` — competitor analysis
9. `docs/architecture/` — system design docs
10. `docs/reference-code/` — competitor source code notes (12 projects)
11. `docs/archives/` — historical only, don't reference for current work
