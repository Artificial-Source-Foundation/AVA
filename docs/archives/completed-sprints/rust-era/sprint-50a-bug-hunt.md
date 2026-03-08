# Sprint 50a: Bug Hunt & Error Audit

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Systematically audit the codebase for edge-case bugs and ensure all error messages are user-friendly. Fix everything found. No new features — only fixes.

## Key Files to Read

```
CLAUDE.md
crates/ava-agent/src/loop.rs             # Agent loop — tool execution, stuck detection
crates/ava-agent/src/stack.rs            # AgentStack — provider resolution, fallback
crates/ava-llm/src/providers/common.rs   # send_with_retry, error handling
crates/ava-llm/src/circuit_breaker.rs    # CircuitBreaker
crates/ava-tools/src/registry.rs         # Tool execution, ToolOutput
crates/ava-tools/src/core/bash.rs        # Bash tool
crates/ava-tools/src/core/read.rs        # Read tool (large file handling)
crates/ava-tools/src/core/custom_tool.rs # Custom TOML tools
crates/ava-permissions/src/classifier.rs # Command classifier
crates/ava-permissions/src/inspector.rs  # DefaultInspector
crates/ava-session/src/lib.rs            # Session persistence
crates/ava-memory/src/lib.rs             # Memory system
crates/ava-mcp/src/config.rs             # MCP config loading
crates/ava-context/src/lib.rs            # Context compaction
crates/ava-codebase/src/indexer.rs       # Codebase indexing
crates/ava-types/src/lib.rs              # AvaError variants
crates/ava-tui/src/config/cli.rs         # NO_PROVIDER_ERROR
```

## Story 1: Known Issues Audit

Write a targeted test for each of the following edge cases. If the test fails, fix the bug. If it passes, the test stays as a regression guard.

**Checklist:**

| # | Edge Case | Test Location | What to Check |
|---|-----------|--------------|---------------|
| 1 | Empty model response | `ava-agent/tests/` | Agent doesn't crash on `""` response, continues or stops gracefully |
| 2 | Invalid JSON tool arguments | `ava-tools/tests/` | `execute()` returns `ToolResult { is_error: true }`, not panic |
| 3 | Provider timeout / circuit breaker | `ava-llm/tests/` | After 5 failures, circuit opens; after cooldown, half-open works |
| 4 | Very large file read (> 1MB) | `ava-tools/tests/` | Truncation kicks in, result < 50KB |
| 5 | Session save/load roundtrip | `ava-session/tests/` | Save session with tool calls + tool results, load back, all fields match |
| 6 | Memory search with special chars | `ava-memory/tests/` | Search for `"foo's bar & <baz>"` doesn't crash, returns empty or matches |
| 7 | Invalid MCP config JSON | `ava-mcp/tests/` | `load_merged_mcp_config()` with malformed JSON returns Ok(empty), not Err |
| 8 | Custom TOML with missing fields | `ava-tools/tests/` | Missing `[execution]` section → skipped with warning, not crash |
| 9 | `--yolo` blocks Critical commands | `ava-permissions/tests/` | `rm -rf /` blocked even when yolo=true |
| 10 | Context compaction preserves system prompt | `ava-context/tests/` | After compaction, first message is still Role::System |
| 11 | Provider fallback on primary failure | `ava-agent/tests/` | Primary returns error → fallback provider used → success |
| 12 | Codebase indexing on empty directory | `ava-codebase/tests/` | `index_project()` on empty dir returns valid (empty) index, not error |
| 13 | Session list with many sessions | `ava-session/tests/` | Create 100 sessions, list completes in < 500ms |
| 14 | Concurrent read-only tools | `ava-agent/tests/` | Multiple read tools in same turn actually run (verify via timing or mock) |
| 15 | Tool call with unknown tool name | `ava-tools/tests/` | Returns error result "unknown tool: foo", not panic |

**Implementation approach:**
- For each item, first check if a test already exists
- If it exists and passes → move on
- If it doesn't exist → write the test
- If the test fails → fix the bug, then verify

**Acceptance criteria:**
- All 15 edge cases have tests
- All tests pass
- Bugs found are fixed at root cause

## Story 2: Error Message Audit

Check that user-facing errors are clear and actionable. For each case, write a test that triggers the error and asserts the message contains helpful text.

**Error cases to verify:**

| Error Condition | Expected Message (contains) |
|----------------|---------------------------|
| No API key for provider | "credentials" or "API key" |
| No provider configured | "config.yaml" and example |
| Tool execution failure | Tool name in the error |
| Network timeout | "timed out" |
| Unknown model | Model name in the error |
| Session not found | Session ID in the error |
| Permission denied (blocked) | "blocked" and risk level |

**Implementation:**
- Add tests in relevant crate test files
- Fix any messages that show raw Rust debug output (e.g., `ToolError("...")` instead of the inner message)
- Ensure `AvaError::display()` produces clean, user-readable text for all variants

**Acceptance criteria:**
- All error messages are user-friendly
- No `Debug` formatting leaks into user-facing output
- Tests verify message quality

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- **NO new features** — only tests and fixes
- Fix bugs at root cause, not workarounds

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
```
