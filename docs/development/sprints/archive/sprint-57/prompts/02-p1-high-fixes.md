# Sprint 57b: P1 High Priority Fixes

## Context

AVA is a Rust-first AI coding agent (~21 crates, Ratatui TUI, Tokio async). See `CLAUDE.md` for conventions.

Sprint 56 audit identified 21 P1 (High) findings. This sprint addresses the most impactful ones. Read `docs/development/sprints/sprint-56/results/00-action-plan.md` for full context.

**Prerequisite**: Sprint 57a (P0 fixes) is complete.

## Task 1: Fix modal unwraps

**File**: `crates/ava-tui/src/app/modals.rs`

Read the file. Find all 11 `.unwrap()` calls on `self.state.provider_connect.as_mut().unwrap()` (lines ~457-568).

**Fix**: Each match arm that accesses provider_connect state should use the `state` variable that was already extracted at line 328-334 via `match self.state.provider_connect { Some(ref mut s) => s, None => ... }`. The unwraps happen in sub-screen handlers where the borrow was dropped.

Refactor approach:
1. Extract each `ConnectScreen::*` handler into its own method that takes `&mut ProviderConnectState` directly
2. The caller passes the state reference, eliminating the need for `unwrap()`
3. For cases that need `self` (e.g., `self.set_status`), collect results and apply after the match

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 2: Split model_catalog.rs

**File**: `crates/ava-config/src/model_catalog.rs` (~870 lines)

Read the file. Split into 4 modules:

1. `model_catalog/types.rs` — `CatalogModel`, `ModelCatalog`, `CatalogCache` structs and their impl blocks
2. `model_catalog/fallback.rs` — `fallback_catalog()` function and `CURATED_MODELS` constant
3. `model_catalog/fetch.rs` — `fetch()`, `from_raw()`, `merge_fallback()` functions
4. `model_catalog/mod.rs` — Re-exports everything, keeps the module public API identical

Ensure all existing tests still pass. Move tests to appropriate submodule or keep in `mod.rs`.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 3: Document core traits

Add `///` doc comments to these critical public traits (currently undocumented per Sprint 56 audit):

1. `Tool` trait in `crates/ava-tools/src/registry.rs` — document trait purpose, required methods, example
2. `LLMProvider` trait in `crates/ava-llm/src/provider.rs` — document each method
3. `MCPTransport` trait in `crates/ava-mcp/src/transport.rs` — document Send/Receive contract
4. `CondensationStrategy` trait in `crates/ava-context/src/strategies/` — document strategy pattern
5. `PermissionInspector` trait in `crates/ava-permissions/src/inspector.rs` — document inspection flow

Also document the key error types:
6. `AvaError` in `crates/ava-types/src/error.rs` — document each variant
7. `EditError` in `crates/ava-tools/src/edit/error.rs`
8. `SandboxError` in `crates/ava-sandbox/src/`

Read each file first. Add concise, accurate doc comments. Do not add examples unless the usage is non-obvious.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 4: Fix circuit breaker mutex

**File**: `crates/ava-llm/src/circuit_breaker.rs:40,75`

Read the file. Find `.lock().unwrap()` calls on Mutex.

**Fix**: Use `unwrap_or_else` with poisoning recovery:
```rust
self.state.lock().unwrap_or_else(|e| e.into_inner())
```

This recovers from poisoned mutexes instead of panicking.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Task 5: Add missing test coverage

Add tests for these untested modules:

1. **Gemini provider** (`crates/ava-llm/src/providers/gemini.rs`) — request body construction, thinking config, response parsing
2. **Memory tools** (`crates/ava-tools/src/core/memory.rs`) — remember, recall, search operations
3. **CodebaseSearch tool** (`crates/ava-tools/src/core/codebase_search.rs`) — index lookup, empty results

Follow existing test patterns in each crate. Focus on unit tests that don't need external services.

**Before proceeding to the next task, invoke the Code Reviewer sub-agent to verify all changes from this task are correct, consistent, and follow project conventions. Fix any issues it finds before moving on.**

## Final Verification

```bash
cargo test --workspace
cargo clippy --workspace
```

Both must pass with 0 errors and 0 warnings in modified crates.

## Acceptance Criteria

- [ ] Zero `unwrap()` calls in `modals.rs` provider connect handlers
- [ ] `model_catalog.rs` split into 4 files, all tests pass
- [ ] 8+ core traits/error types documented with `///` comments
- [ ] Circuit breaker mutex recovers from poisoning
- [ ] Gemini, memory tools, codebase search have test coverage
- [ ] `cargo test --workspace` passes
- [ ] `cargo clippy --workspace` clean
