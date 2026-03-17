<!-- Last verified: 2026-03-16 -->
# AI Coding Agent Instructions (v3)

> Instructions for AI assistants working on AVA. This file is auto-injected into the AVA agent's system prompt.

## Quick Start

```bash
# All-in-one check
just check                      # fmt + clippy + nextest

# Or raw cargo
cargo test --workspace
cargo clippy --workspace

# Desktop
npm run tauri dev
npm run lint && npx tsc --noEmit
```

Read first: `CLAUDE.md`

## What AVA Is

AVA is a Rust-first AI coding assistant (CLI/TUI + Tauri desktop). 20 Rust crates, ~104K LOC, 1,798 tests.

- **CLI/TUI**: `crates/ava-tui/` (Ratatui + Crossterm + Tokio)
- **Agent runtime**: `crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-praxis/`
- **Desktop**: Tauri 2 -- SolidJS frontend calls Rust via Tauri IPC (`src-tauri/src/commands/`)

**All new features MUST be Rust.** No TypeScript backend logic.

## Key Counts

- 8 LLM providers (Anthropic, OpenAI, Gemini, Ollama, OpenRouter, Copilot, Inception, Mock)
- 6 default tools: `read`, `write`, `edit`, `bash`, `glob`, `grep`
- 8 extended tools: `apply_patch`, `web_fetch`, `web_search`, `multiedit`, `ast_ops`, `lsp_ops`, `code_search`, `git_read`
- Dynamic: MCP servers + TOML custom tools

## Where To Put New Code

- New tools: `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers: `crates/ava-llm/src/providers/`
- New agent features: `crates/ava-agent/` or `crates/ava-praxis/`
- TUI features: `crates/ava-tui/`
- Desktop commands: `src-tauri/src/commands/`
- Configuration: `crates/ava-config/`

## Tool Surface Policy

Keep the default set capped at 6. New tools should default to Extended, MCP, plugin, or custom-tool delivery. Only promote to default with strong justification.

## Common Tasks

### Add Tool (Rust)

1. Tier decision: default to Extended
2. Create `crates/ava-tools/src/core/{tool_name}.rs`
3. Implement `Tool` trait (`name`, `description`, `parameters`, `execute`)
4. Register in `register_core_tools()` with appropriate tiering
5. Tests: `cargo test -p ava-tools`

### Add LLM Provider (Rust)

1. Create `crates/ava-llm/src/providers/{provider}.rs`
2. Implement `LLMProvider` trait (generate, generate_stream, estimate_tokens, estimate_cost, model_name)
3. Tests: `cargo test -p ava-llm`

### Add Desktop Feature

1. Rust command in `src-tauri/src/commands/{feature}.rs`
2. Register in `mod.rs` and `lib.rs`
3. Call from SolidJS via `invoke()`

## Code Standards

### Rust
- Keep error strings actionable and deterministic
- `serde rename_all = "camelCase"` for Tauri IPC
- Register new Tauri commands in `src-tauri/src/commands/mod.rs` and `lib.rs`

### TypeScript (desktop frontend only)
- Strict mode, no `any`
- SolidJS only in `src/` (no React patterns)

## Before Committing

```bash
just check
npm run lint && npm run format:check && npx tsc --noEmit
```

## After Every Significant Change

**This is mandatory.** After completing a feature, fix, or refactor:

1. **Update `docs/development/CHANGELOG.md`** — add entry under current version section
2. **Update `docs/development/backlog.md`** — check off completed items
3. **Update `CLAUDE.md`** if crate count, tool count, or architecture changed
4. **Update `docs/architecture/crate-map.md`** if crates were added or removed
5. **Run `just check`** before committing

Docs must always reflect the current codebase. Never let them drift.

## Do Not

- Commit secrets or credentials
- Add TypeScript backend logic — all backend code is Rust
- Expand the default tool surface without strong justification
- Use React patterns in `src/`
- Build features without wiring them in — no dead code modules
- Skip doc updates after changes

## Documentation

1. `CLAUDE.md` — primary architecture reference
2. `AGENTS.md` — this file
3. `docs/README.md` — documentation entry point
4. `docs/plugins.md` — TOML custom tools and MCP guide
5. `docs/architecture/crate-map.md` — crate dependency map
6. `docs/architecture/plugin-system.md` — power plugin design
7. `docs/development/CHANGELOG.md` — version history
8. `docs/development/backlog.md` — current backlog
9. `docs/ideas/` — archived feature designs (reference only)
