# AVA Development Hub

> 21 Rust crates | Rust-first backend + pnpm-managed desktop frontend

## Quick Start

```bash
# Build and test
just check
just test

# Run the TUI
cargo run --bin ava

# Run headless
cargo run --bin ava -- "goal" --headless --provider openrouter --model anthropic/claude-haiku-4.5

# Desktop app
pnpm tauri dev
```

## Documentation Index

| Document | Purpose |
|----------|---------|
| [CHANGELOG.md](CHANGELOG.md) | Version history and release notes |
| [roadmap.md](roadmap.md) | Version roadmap (v2.1.x, v3.0, future) |
| [backlog.md](backlog.md) | Open work items with priority and status |
| [epics.md](epics.md) | Epic-level feature groupings (completed and planned) |
| [test-matrix.md](test-matrix.md) | Per-crate test counts and E2E verification |
| [v3-plan.md](v3-plan.md) | v3 plan (complete, retained for reference) |
| [dev-tooling.md](dev-tooling.md) | Dev tooling setup (nextest, coverage, hooks) |

### Subdirectories

| Directory | Contents |
|-----------|----------|
| [sprints/archive/](sprints/archive/) | Completed sprint docs (53-66) |
| [benchmarks/](benchmarks/) | Performance and model benchmark results |
| [research/](research/) | Competitive analysis and architecture research |

## Crate Map

| Crate | Purpose |
|-------|---------|
| ava-tui | TUI binary plus headless/web entrypoints |
| ava-agent | Agent execution loop, reflection, instructions |
| ava-llm | LLM providers, routing, retries, streaming |
| ava-tools | Tool trait, registry, 9 default tools, custom tool loading |
| ava-praxis | Multi-agent orchestration |
| ava-config | Configuration, credentials, model catalog |
| ava-permissions | Safety tags, risk levels, command classification |
| ava-context | Context window management and condensation |
| ava-session | Session persistence |
| ava-types | Shared types |
| ava-auth | OAuth and credential flows |
| ava-cli-providers | External CLI provider integration |
| ava-mcp | Model Context Protocol support |
| ava-codebase | Code indexing and search |
| ava-platform | File system and shell abstractions |
| ava-validator | Validation utilities |
| ava-memory | Persistent memory/recall |
| ava-extensions | Extension system |
| ava-sandbox | Command sandboxing |
| ava-db | SQLite connection pool |
| ava-plugin | Plugin runtime and hooks |

## How to Contribute

1. All new CLI/agent code MUST be Rust. TypeScript is desktop-only.
2. Default tool surface stays at 9. New tools should usually ship as opt-in helpers, plugins, MCP tools, or custom tools.
3. Run `just check` before submitting; for desktop changes also run `pnpm lint && pnpm typecheck`.
4. Read `CLAUDE.md` at the project root for full conventions.

## Test Commands

```bash
# Fast project check
just check

# Full workspace
cargo test --workspace

# Single crate
cargo test -p ava-tools

# Smoke test with real provider
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3

# Desktop
pnpm test:run
pnpm typecheck
```
