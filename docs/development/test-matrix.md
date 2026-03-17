# AVA Test Matrix

> Last updated: 2026-03-16 | All tests passing

## Per-Crate Test Counts

Total: **1,466 tests** across 21 crates. All passing, 0 failures, 1 ignored.

| Crate | Tests | Focus |
|-------|------:|-------|
| ava-llm | 277 | Provider implementations, streaming, model catalog, registry |
| ava-tui | 217 | TUI rendering, widgets, slash commands, headless mode |
| ava-tools | 196 | Tool trait, core tools, registry, middleware |
| ava-agent | 134 | Agent loop, reflection, stack, instructions, E2E |
| ava-permissions | 115 | Command classification, safety tags, risk levels, inspector |
| ava-config | 79 | Configuration, credentials, model catalog |
| ava-smoke | 67 | Smoke tests (mock provider, no network) |
| ava-context | 60 | Context management, condensation, compaction |
| ava-types | 58 | Shared types, serialization |
| ava-praxis | 50 | Multi-agent orchestration, workflows |
| ava-session | 35 | Session persistence, SQLite, FTS5 |
| ava-cli-providers | 32 | CLI provider resolution |
| ava-mcp | 31 | MCP transport, client, config |
| ava-auth | 31 | OAuth flows, token exchange, PKCE |
| ava-codebase | 22 | BM25 indexing, PageRank, import graph |
| ava-platform | 16 | File system, shell abstractions |
| ava-validator | 13 | Validation pipelines |
| ava-memory | 11 | Memory persistence, recall |
| ava-extensions | 9 | Extension management |
| ava-sandbox | 7 | Command sandboxing |
| ava-db | 6 | SQLite connection pool |
| ava-plugin | 0 | Plugin system (new, no tests yet) |

## E2E Tool Tests (13 tools)

Verified on `anthropic/claude-haiku-4.5` via OpenRouter.

| # | Tool | Tier | Status |
|---|------|------|--------|
| 1 | read | Default | PASS |
| 2 | write | Default | PASS |
| 3 | edit | Default | PASS |
| 4 | bash | Default | PASS |
| 5 | glob | Default | PASS |
| 6 | grep | Default | PASS |
| 7 | multiedit | Extended | PASS |
| 8 | apply_patch | Extended | PASS |
| 9 | test_runner | Extended | PASS |
| 10 | lint | Extended | PASS |
| 11 | diagnostics | Extended | PASS |
| 12 | web_fetch | Extended | PASS |
| 13 | git | Extended | PASS |

## Mode Tests

| Mode | Status | Flag |
|------|--------|------|
| Headless | PASS | `--headless` |
| JSON output | PASS | `--headless --json` |
| Multi-agent | PASS | `--multi-agent` |
| Workflow pipeline | PASS | `--workflow plan-code-review` |
| Review subcommand | PASS | `ava review --working` |

## Provider Tests

| Provider | Model | Status |
|----------|-------|--------|
| OpenRouter -> Anthropic | `anthropic/claude-haiku-4.5` | PASS |
| OpenRouter -> OpenAI | `openai/gpt-5.3-codex` | PASS |
| OpenRouter -> Google | `google/gemini-3-flash-preview` | PASS |

## Test Commands

```bash
# Full workspace
cargo test --workspace

# Single crate
cargo test -p ava-tools

# With output
cargo test --workspace -- --nocapture

# Smoke test (requires OpenRouter key)
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3
```
