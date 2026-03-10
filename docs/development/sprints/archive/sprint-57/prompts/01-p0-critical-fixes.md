# Sprint 57a: P0 Critical Fixes

## Context

AVA is a Rust-first AI coding agent (~21 crates, Ratatui TUI, Tokio async). See `CLAUDE.md` for conventions.

Sprint 56 produced a codebase quality audit. This sprint fixes the 4 Critical (P0) findings. Read `docs/development/sprints/sprint-56/results/00-action-plan.md` for full context.

## Task 1: Fix AgentStack expect panic

**File**: `crates/ava-tui/src/state/agent.rs:107`

Read the file. Find the `.expect("AgentStack not initialised")` call. This is on a user-facing path — if AgentStack init fails, the TUI panics instead of showing an error.

**Fix**: Change to return `Result` or use `Option` with graceful handling:
```rust
// Before:
self.stack.as_ref().expect("AgentStack not initialised")

// After: return Option<&AgentStack> and handle None at call sites
pub fn stack(&self) -> Option<&AgentStack> {
    self.stack.as_ref()
}
```

Update all callers to handle `None` gracefully (show error status instead of panic).

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 2: Fix reqwest::Client expect in pool

**File**: `crates/ava-llm/src/pool.rs:73`

Read the file. Find `.expect("failed to build reqwest client")`.

**Fix**: Change `get_client` to return `Result<Arc<Client>>`:
```rust
// Before:
.expect("failed to build reqwest client")

// After:
.map_err(|e| AvaError::ProviderError(format!("Failed to build HTTP client: {e}")))?
```

Update all callers of `get_client()` (in provider files) to propagate `?`. These are all already in async functions that return `Result`, so this is mechanical.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 3: Add tests for LLM parsing layer

**File**: `crates/ava-llm/src/providers/common/parsing.rs`

Read the file. It has ~15 public functions with 0 test coverage. These parse JSON from LLM API responses — critical for correctness.

Add a `#[cfg(test)] mod tests` block with tests for every pub fn:

1. **Happy path**: Valid JSON input → correct output
2. **Malformed JSON**: Missing fields, wrong types → graceful error (not panic)
3. **Edge cases**: Empty arrays, null values, partial responses, extra fields
4. **Tool call parsing**: Valid tool calls, malformed tool calls, empty tool list
5. **Usage parsing**: Complete usage, partial usage, missing usage
6. **SSE line parsing**: Valid SSE, partial chunks, data: [DONE]

Target: At least 2-3 tests per pub fn. Follow existing test patterns in the crate.

Also read `crates/ava-llm/src/providers/common/message_mapping.rs` and add tests for the message mapping functions if they lack coverage.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 4: Remove TUI blanket allow(dead_code)

**File**: `crates/ava-tui/src/lib.rs:8`

Read the file. Find `#![allow(dead_code)]`. Remove it.

Then run `cargo check -p ava-tui 2>&1` and fix all dead_code warnings:
- If code is genuinely unused → delete it
- If code is used only conditionally (feature-gated) → add targeted `#[allow(dead_code)]` with a comment explaining why
- If code is used by tests only → move to `#[cfg(test)]` module

Run `cargo clippy -p ava-tui` after fixing all warnings. Must be 0 warnings.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Final Verification

```bash
cargo test --workspace
cargo clippy --workspace
```

Both must pass with 0 errors and 0 warnings in modified crates.

## Acceptance Criteria

- [ ] No `.expect()` on user-facing AgentStack path
- [ ] `pool.rs` `get_client` returns `Result`, callers updated
- [ ] `parsing.rs` has 30+ tests covering all pub fns
- [ ] `#![allow(dead_code)]` removed from ava-tui, all dead code resolved
- [ ] `cargo test --workspace` passes
- [ ] `cargo clippy --workspace` clean
