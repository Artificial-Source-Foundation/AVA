# Sprint 100 — v2.1 Release Polish & Documentation

> Organize docs, document test matrix, tag v2.1

## Context

All feature sprints (11–50) and stabilization (50a–50f, 99) are complete. The codebase is feature-complete with 19 built-in tools, 6 LLM providers, multi-agent workflows, code review, voice input, and MCP plugins. This sprint organizes everything for a v2.1 release.

## Stories

### Story 1 — E2E Test Matrix Documentation

Create `docs/development/test-matrix.md` documenting every tested capability:

```markdown
# AVA v2.1 — E2E Test Matrix

> Verified 2026-03-08 on `anthropic/claude-haiku-4.5` via OpenRouter

## Tool Tests (19/19)

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
| 12 | remember | PASS | Store key-value memory |
| 13 | recall | PASS | Retrieve stored memory |
| 14 | memory_search | PASS | Query memory by keyword |
| 15 | session_list | PASS | List sessions (empty = correct) |
| 16 | session_search | PASS | Search sessions by query |
| 17 | session_load | N/A | Requires existing session ID |
| 18 | codebase_search | PASS | BM25+PageRank symbol search |
| 19 | git_read | PASS | Review-only (via `ava review`) |

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
```

### Story 2 — Version Bump & CHANGELOG

1. Create or update `CHANGELOG.md` with a v2.1 section:
   - List all sprint ranges and their focus areas (from `docs/development/roadmap.md`)
   - Highlight key capabilities: 19 tools, 6 providers, multi-agent, workflows, code review, voice, MCP, TUI
   - Stats: 21 crates, ~47K lines of Rust, 595 tests, 291 source files
2. Update version in `crates/ava-tui/Cargo.toml` to `2.1.0`
3. Update version in any other `Cargo.toml` files that specify a version
4. Ensure `cargo test --workspace` still passes after version bump

### Story 3 — README Refresh

Update `README.md` to reflect current state:

1. Add version badge or mention "v2.1" near the top
2. Verify the architecture diagram is accurate (21 crates, 19 tools)
3. Add a "Test model" note in the Quick Start section:
   ```
   # Smoke test (cheapest SOTA)
   cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3
   ```
4. Add "Tested on" line: `Verified: All 19 tools, 5 modes, 3 providers pass E2E (2026-03-08)`
5. Keep it concise — README should be a quick overview, not a manual

### Story 4 — Docs Index Cleanup

1. Update `docs/README.md` — ensure it links to all current docs:
   - `development/roadmap.md`
   - `development/test-matrix.md` (new)
   - `development/research/`
   - `development/benchmarks/`
   - `architecture/` (all 7 files)
   - `troubleshooting/`
   - `reference-code/`
2. Remove any dead links or references to deleted files
3. Verify `docs/architecture/` files are still accurate — if any reference outdated sprint numbers or missing features, update them

### Story 5 — Clean Up Stale Files

1. Check for any leftover sprint prompt files in the repo root or `docs/development/sprints/`
2. Remove temp/test files if any exist
3. Check for any TODO/FIXME comments that reference completed sprints — remove or update them
4. Verify `.gitignore` covers common temp patterns (`/tmp/`, `*.swp`, `.DS_Store`)
5. Run `cargo clippy --workspace` and fix any new warnings

### Story 6 — CLAUDE.md & AGENTS.md Final Review

1. Read both files end-to-end
2. Verify all counts are accurate (21 crates, 19 tools, 595 tests, etc.)
3. Verify all CLI flags listed match actual `clap` definitions in `crates/ava-tui/src/config/cli.rs`
4. Verify all code paths referenced still exist
5. Fix any contradictions or outdated information
6. Add version reference: "Current version: v2.1"

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Smoke test
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openrouter --model anthropic/claude-haiku-4.5 --max-turns 3
```

## Rules

- Do NOT add new features — this is documentation and organization only
- Do NOT refactor code — only version bumps and doc changes
- Keep all docs concise and scannable
- Use tables over prose where possible
- Conventional commit: `docs: v2.1 release documentation and test matrix`
