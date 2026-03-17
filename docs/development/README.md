# AVA Development Hub

> v2.1.x | 21 Rust crates | 452 source files | ~128K lines of Rust | 1,466 tests | All passing

## Quick Start

```bash
# Build and test
cargo test --workspace
cargo clippy --workspace

# Run the TUI
cargo run --bin ava

# Run headless
cargo run --bin ava -- "goal" --headless --provider openrouter --model anthropic/claude-haiku-4.5

# Desktop app
npm run tauri dev
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

| Crate | Tests | Purpose |
|-------|------:|---------|
| ava-tui | 217 | TUI binary (Ratatui + Crossterm) |
| ava-agent | 134 | Agent execution loop, reflection, instructions |
| ava-llm | 277 | LLM providers (Anthropic, OpenAI, Gemini, Ollama, OpenRouter, Copilot, Inception) |
| ava-tools | 196 | Tool trait, registry, 6 default + 7 extended tools |
| ava-praxis | 50 | Multi-agent orchestration (Director) |
| ava-config | 79 | Configuration, credentials, model catalog |
| ava-permissions | 115 | Safety tags, risk levels, command classification |
| ava-context | 60 | Context window management, condensation |
| ava-session | 35 | Session persistence (SQLite + FTS5) |
| ava-types | 58 | Shared types (TokenUsage, StreamChunk) |
| ava-auth | 31 | OAuth, Copilot token exchange, PKCE |
| ava-cli-providers | 32 | CLI provider resolution |
| ava-mcp | 31 | Model Context Protocol support |
| ava-codebase | 22 | Code indexing (BM25 + PageRank) |
| ava-platform | 16 | File system + shell abstractions |
| ava-validator | 13 | Validation utilities |
| ava-memory | 11 | Persistent memory/recall |
| ava-extensions | 9 | Extension system |
| ava-sandbox | 7 | Command sandboxing (bwrap/sandbox-exec) |
| ava-db | 6 | SQLite connection pool |
| ava-plugin | 0 | Plugin system (new) |

## How to Contribute

1. All new CLI/agent code MUST be Rust. TypeScript is desktop-only.
2. Default tool surface stays at 6. New tools go to Extended, MCP, or plugin tier.
3. Run `cargo test --workspace && cargo clippy --workspace` before submitting.
4. Read `CLAUDE.md` at the project root for full conventions.

## Test Commands

```bash
# Full workspace
cargo test --workspace

# Single crate
cargo test -p ava-tools

# Smoke test with real provider
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3

# Desktop
npm run test:run
npx tsc --noEmit
```
