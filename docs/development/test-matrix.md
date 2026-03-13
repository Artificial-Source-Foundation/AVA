# AVA v2.1 — E2E Test Matrix

> Verified 2026-03-08 on `anthropic/claude-haiku-4.5` via OpenRouter

## Tool Tests (13 core tools)

This matrix tracks the default and extended tools exposed through the main tool registry. Runtime helpers such as `task`, `todo_*`, and `question` are registered separately, and historical references to memory/session/codebase tools reflect earlier architecture snapshots rather than the current default tool inventory.

| # | Tool | Status | Test Description |
|---|------|--------|------------------|
| 1 | read | PASS | Read file, report content |
| 2 | write | PASS | Create file, verify content |
| 3 | edit | PASS | Single string replacement |
| 4 | bash | PASS | Shell command execution |
| 5 | glob | PASS | File pattern matching |
| 6 | grep | PASS | Content search with line numbers |
| 7 | multiedit | PASS | Atomic multi-file edits |
| 8 | apply_patch | PASS | Unified diff application |
| 9 | test_runner | PASS | Cargo test execution |
| 10 | lint | PASS | Clippy lint results |
| 11 | diagnostics | PASS | Compiler diagnostics |
| 12 | web_fetch | PASS | Remote fetch with output limits |
| 13 | git | PASS | Review-only git access (via `ava review`) |

## Mode Tests (5/5)

| Mode | Status | Command |
|------|--------|---------|
| Headless | PASS | `--headless` |
| JSON output | PASS | `--headless --json` |
| Multi-agent commander | PASS | `--multi-agent` |
| Workflow pipeline | PASS | `--workflow plan-code-review` |
| Review subcommand | PASS | `ava review --working` |

## Provider Tests

| Provider | Model | Status |
|----------|-------|--------|
| OpenRouter → Anthropic | `anthropic/claude-haiku-4.5` | PASS |
| OpenRouter → OpenAI | `openai/gpt-5.3-codex` | PASS |
| OpenRouter → Google | `google/gemini-3-flash-preview` | PASS |

## Recommended Test Models

| Use Case | Model ID | Cost (input/output per M) |
|----------|----------|---------------------------|
| Smoke tests | `anthropic/claude-haiku-4.5` | $1 / $5 |
| Quality verification | `anthropic/claude-sonnet-4` | $3 / $15 |
| Budget bulk | `moonshotai/kimi-k2.5` | $0.45 / $0.45 |
