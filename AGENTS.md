<!-- Last verified: 2026-04-08 -->
# AI Coding Agent Instructions (v3)

> Instructions for AI assistants working on AVA. This file is auto-injected into the AVA agent's system prompt.

## Quick Start

```bash
# All-in-one check
just check                      # fmt + clippy + targeted nextest

# Or raw cargo
cargo test --workspace
cargo clippy --workspace

# Desktop
pnpm tauri dev
pnpm lint && pnpm typecheck
```

This file is the primary source of truth for repo workflow and architecture.

## What AVA Is

AVA is a Rust-first AI coding assistant (CLI/TUI + Tauri desktop + web mode) with a 22-crate Rust workspace.

- **CLI/TUI**: `crates/ava-tui/` (Ratatui + Crossterm + Tokio)
- **Agent runtime**: `crates/ava-agent/`, `ava-llm/`, `ava-tools/`, `ava-review/`
- **Desktop**: Tauri 2 -- SolidJS frontend calls Rust via Tauri IPC (`src-tauri/src/commands/`)
- **Web**: `ava serve` from `crates/ava-tui/`

**All new features MUST be Rust.** No TypeScript backend logic.

## Key Counts

- 22 Rust crates in the root workspace (`src-tauri/` remains outside the workspace)
- 9 default tools: `read`, `write`, `edit`, `bash`, `glob`, `grep`, `web_fetch`, `web_search`, `git_read`
- Additional tools load separately at runtime (for example `subagent`, `todo_*`, `question`, `plan`, MCP, and TOML custom tools)

## Where To Put New Code

- New tools: `crates/ava-tools/src/core/` (implement `Tool` trait)
- New providers: `crates/ava-llm/src/providers/`
- New agent features: `crates/ava-agent/` or `crates/ava-review/`
- External agent integration: `crates/ava-acp/` (Agent Client Protocol)
- TUI features: `crates/ava-tui/`
- Desktop commands: `src-tauri/src/commands/`
- Configuration: `crates/ava-config/`

## Tool Surface Policy

Keep the default set capped at 9. New tools should default to opt-in delivery (plugin, MCP, or custom-tool). Only promote to default with strong justification.

Power plugins are part of the core architecture. The current 3.3 direction is to grow advanced capability behind plugin seams instead of expanding core product surfaces.

## Common Tasks

### Add Tool (Rust)

1. Tier decision: default to opt-in delivery unless it truly belongs in the default 9
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
pnpm lint && pnpm format:check && pnpm typecheck
```

## After Every Significant Change

**This is mandatory.** After completing a feature, fix, or refactor:

1. **Update `CHANGELOG.md`** — add entry under current version section
2. **Update the relevant docs** — at minimum `docs/project/backlog.md`, plus any architecture, reference, contributor, or README docs affected by the change
3. **Update `AGENTS.md`** if the source-of-truth architecture, workflow, or conventions changed
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

1. `AGENTS.md` — primary source of truth for architecture, workflow, and conventions
2. `docs/README.md` — documentation entry point
3. `docs/project/roadmap.md` — source of truth for product direction
4. `docs/project/backlog.md` — current backlog
5. `CLAUDE.md` — compatibility reference that redirects back to the active docs
6. `docs/extend/README.md` — plugin, MCP, command, skill, and custom-tool reference
7. `docs/architecture/plugin-boundary.md` — first concrete core-to-plugin migration checklist
8. `docs/architecture/crate-map.md` — crate dependency map
9. `CHANGELOG.md` — version history

`AGENTS.md` owns workflow, conventions, and architectural guidance. `docs/project/roadmap.md` owns product direction.

For the full docs index including reference material, see `README.md` and `docs/README.md`.
