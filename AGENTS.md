# AI Coding Agent Instructions

> Universal instructions for AI assistants working on AVA

---

## Quick Start

```bash
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test
```

Read first: `CLAUDE.md`

---

## Project Overview

AVA is a multi-agent AI coding assistant with desktop app and CLI.

- **CLI/TUI**: Pure Rust binary (`crates/ava-tui/`) — Ratatui + Crossterm + Tokio
- **Agent runtime**: Rust crates (`crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-commander/`)
- **Desktop**: Tauri 2 (Rust backend + SolidJS frontend)
- **Desktop orchestration**: `packages/core-v2/` + `packages/extensions/` (TypeScript, desktop only)

**IMPORTANT: All new CLI/agent features MUST be Rust. Do not add new features to `packages/` (TypeScript). The TypeScript layer is retained only for the Tauri desktop webview.**

---

## Architecture (Current)

### Repository Layout

```text
AVA/
├── crates/              # Rust crates — PRIMARY codebase for CLI/agent
│   ├── ava-tui/         # CLI/TUI binary (Ratatui)
│   ├── ava-agent/       # Agent loop + reflection
│   ├── ava-llm/         # LLM providers (6 built-in)
│   ├── ava-tools/       # Tool trait + registry + core tools
│   ├── ava-commander/   # Multi-agent orchestration
│   ├── ava-session/     # Session persistence
│   ├── ava-memory/      # Persistent memory/recall
│   └── ...              # 12 more crates
├── packages/            # TypeScript — DESKTOP ONLY
│   ├── core-v2/         # desktop orchestration kernel
│   ├── extensions/      # desktop extension modules (20)
│   └── ...
├── src/                 # desktop frontend (SolidJS)
├── src-tauri/           # desktop Tauri host
└── cli/                 # legacy TS CLI (being replaced by crates/ava-tui)
```

### Rust-First Rule

**All new CLI/agent features MUST be Rust.** The `dispatchCompute` pattern is deprecated for new work.

- New tools → `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers → `crates/ava-llm/src/providers/`
- New agent features → `crates/ava-agent/` or `crates/ava-commander/`
- TUI features → `crates/ava-tui/`

### Important Counts

- Rust crates: ~20
- Tool surface: ~41

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

1. Create `crates/ava-tools/src/core/{tool_name}.rs`
2. Implement `Tool` trait (`name`, `description`, `parameters`, `execute`)
3. Register in `register_core_tools()`
4. Add tests, run `cargo test -p ava-tools`

### Add LLM Provider (Rust)

1. Create `crates/ava-llm/src/providers/{provider}.rs`
2. Implement `LLMProvider` trait (generate, generate_stream, estimate_tokens, estimate_cost, model_name)
3. Add tests, run `cargo test -p ava-llm`

### Add Desktop Feature (TypeScript — desktop only)

1. Implement in `packages/extensions/`
2. Register on activation
3. Optionally add Rust hotpath via `src-tauri/src/commands/`

---

## Before Committing

- `npm run lint`
- `npm run format:check`
- `npx tsc --noEmit`
- `npm run test`
- `npm run tauri dev` (or `npm run tauri build` for release verification)

---

## CLI Testing with OpenRouter

An OpenRouter API key is configured at `~/.ava/credentials.json` for testing.

### Running CLI tests

```bash
# Build CLI first
cd cli && npm run build && cd ..

# Run with a goal
node cli/dist/index.js run "your goal here" --provider openrouter --model <model> --max-turns 5
```

### Recommended models (via OpenRouter)

| Model | ID | Best for | Cost |
|-------|-----|----------|------|
| Codex 5.3 | `openai/gpt-5.3-codex` | Best OpenAI coding model | $1.75/M |
| Claude Sonnet 4.6 | `anthropic/claude-sonnet-4.6` | General coding, tool use | $3/M |
| Claude Opus 4.6 | `anthropic/claude-opus-4.6` | Complex tasks, code review | $5/M |
| Kimi K2.5 | `moonshotai/kimi-k2.5` | Budget bulk work, 262K context | $0.45/M |
| GLM 5 | `z-ai/glm-5` | Budget bulk work, 200K context | $0.80/M |
| GLM 4.5 Air | `z-ai/glm-4.5-air` | Free tier testing | FREE |

For smoke tests, use `anthropic/claude-sonnet-4.6` or `openai/gpt-5.3-codex`.
For quality verification, use `openai/gpt-5.3-codex` or `anthropic/claude-opus-4.6`.
For budget/bulk work, use `moonshotai/kimi-k2.5` or `z-ai/glm-5`.

---

## Do Not

- Commit secrets or credentials
- Add parent-directory imports
- Rely on migration-era `packages/core/src/*` paths for new work
- Use React patterns in `src/`

---

## Documentation Priority

1. `CLAUDE.md`
2. `docs/README.md`
3. `docs/ROADMAP.md`
4. `docs/VISION.md`
5. `docs/development/opencode-comparison.md`
6. `docs/reference-code/`
