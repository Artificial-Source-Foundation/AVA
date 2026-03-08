# Sprint 50f: DX Hardening & Code Quality

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Fix developer experience issues across the codebase. Split oversized files, eliminate unwraps, standardize error handling, add doc comments, and clean up workspace configuration. No new features — only refactoring and documentation.

## Key Files to Read

```
CLAUDE.md
Cargo.toml                                    # Workspace config

# Files to refactor (over 300 lines):
crates/ava-tui/src/app.rs                     # 1196 lines — CRITICAL
crates/ava-agent/src/loop.rs                  # 804 lines
crates/ava-llm/src/providers/common.rs        # 767 lines
crates/ava-permissions/src/classifier.rs      # 753 lines
crates/ava-commander/src/workflow.rs          # 651 lines
crates/ava-commander/src/review.rs            # 636 lines
crates/ava-agent/src/stack.rs                 # 598 lines
crates/ava-permissions/src/inspector.rs       # 559 lines

# Error handling:
crates/ava-types/src/lib.rs                   # AvaError enum
crates/ava-tools/src/registry.rs              # ToolError
```

## Story 1: Eliminate Unwraps in ava-tools (CRITICAL)

ava-tools has **37 `unwrap()` calls** — each is a potential panic in production.

**Implementation:**
- Audit every `unwrap()` in `crates/ava-tools/src/`
- Replace with:
  - `?` operator where the function returns Result
  - `.unwrap_or_default()` for safe defaults
  - `.ok_or_else(|| AvaError::...)` for meaningful errors
  - `if let Some(x) = ...` for optional handling
- Special attention to:
  - JSON parsing: `serde_json::from_value(...).unwrap()` → use `?`
  - Path operations: `.parent().unwrap()` → `.parent().unwrap_or(Path::new("."))`
  - String operations: `.to_str().unwrap()` → `.to_str().unwrap_or("")`

**Acceptance criteria:**
- Zero `unwrap()` calls in ava-tools/src/ (except in test code)
- All existing tests still pass
- No behavior changes

## Story 2: Split app.rs (1196 lines → 4 modules)

**Current**: Everything in one file — state, event handling, rendering, modals.

**Target split:**

| New File | Contents | Lines (est) |
|----------|----------|-------------|
| `app.rs` | `App` struct, `new()`, `run()`, public API | ~150 |
| `app/event_handler.rs` | Key event handling, input processing | ~300 |
| `app/commands.rs` | Slash command handling, command palette dispatch | ~200 |
| `app/modals.rs` | Modal state management (model selector, tool list, etc.) | ~200 |

**Implementation:**
- Create `crates/ava-tui/src/app/` directory
- Move `app.rs` → `app/mod.rs`
- Extract event handler methods into `event_handler.rs`
- Extract command dispatch into `commands.rs`
- Extract modal logic into `modals.rs`
- Keep `App` struct definition and public API in `mod.rs`

**Acceptance criteria:**
- No file over 300 lines
- All existing tests pass
- No public API changes (same `App` type, same methods)

## Story 3: Split loop.rs (804 lines → 3 modules)

**Target split:**

| New File | Contents | Lines (est) |
|----------|----------|-------------|
| `loop.rs` | `AgentLoop` struct, `run()`, `run_streaming()` | ~250 |
| `tool_execution.rs` | `execute_tool_calls_tracked()`, parallel/sequential logic, `READ_ONLY_TOOLS` | ~200 |
| `response.rs` | `generate_response()`, dedup guard, `parse_tool_calls()` | ~150 |

**Acceptance criteria:**
- No file over 300 lines
- All existing tests pass
- `AgentLoop` public API unchanged

## Story 4: Split common.rs (767 lines)

**Target split:**

| New File | Contents | Lines (est) |
|----------|----------|-------------|
| `common.rs` | `send_with_retry()`, shared HTTP logic | ~200 |
| `message_mapping.rs` | `map_messages_openai()`, `map_messages_anthropic()`, role conversion | ~250 |
| `pricing.rs` | `model_pricing_usd_per_million()`, `estimate_cost_usd()` | ~100 |

**Acceptance criteria:**
- No file over 300 lines
- Provider implementations still compile and work
- All tests pass

## Story 5: Split classifier.rs (753 lines)

**Target split:**

| New File | Contents | Lines (est) |
|----------|----------|-------------|
| `classifier.rs` | `classify_command()`, `CommandClassification` struct | ~150 |
| `classifier/rules.rs` | Pattern matching rules, command categories | ~300 |
| `classifier/parser.rs` | Tree-sitter bash parsing, pipe/chain splitting | ~200 |

**Acceptance criteria:**
- No file over 300 lines
- All classifier tests pass
- Classification behavior unchanged

## Story 6: Workspace Config Standardization

Fix 6 crates not using `edition.workspace = true`.

**Crates to fix:**
- `ava-cli-providers/Cargo.toml`
- `ava-commander/Cargo.toml`
- `ava-llm/Cargo.toml`
- `ava-mcp/Cargo.toml`
- `ava-session/Cargo.toml`
- `ava-tui/Cargo.toml`

**Change in each:**
```toml
# Before
edition = "2021"

# After
edition.workspace = true
```

**Also check:** `version.workspace = true` consistency across crates.

**Acceptance criteria:**
- All crates use `edition.workspace = true`
- `cargo build --workspace` succeeds
- No behavior changes

## Story 7: Doc Comments on Public API

Add `//!` module docs and `///` doc comments to key public types.

**Priority targets (lowest coverage):**

| Crate | File | What to Document |
|-------|------|-----------------|
| ava-commander | `lib.rs` | Commander, Lead, Worker, Domain, Task, TaskType, Budget |
| ava-tools | `lib.rs` | Module overview, add `pub use` for public API |
| ava-tools | `registry.rs` | Tool trait, ToolRegistry, ToolSource, ToolOutput |
| ava-agent | `loop.rs` | AgentLoop, AgentConfig, AgentEvent |
| ava-agent | `stack.rs` | AgentStack, AgentStackConfig, AgentRunResult |
| ava-tui | `app.rs` | App, AppState, ModalType |
| ava-permissions | `classifier.rs` | CommandClassification, classify_command() |
| ava-llm | `provider.rs` | LLMProvider trait, SharedProvider |

**Rules:**
- `//!` at top of each lib.rs (1-2 sentence crate purpose)
- `///` on all public structs, enums, traits
- `///` on public functions with non-obvious behavior
- Skip trivial getters and obvious functions
- Don't over-document — concise is better than verbose

**Acceptance criteria:**
- All crate lib.rs files have `//!` module docs
- All public structs/enums/traits in listed files have `///` docs
- `cargo doc --workspace --no-deps` builds without warnings

## Implementation Order

1. Story 6 (workspace config) — 10 minutes, zero risk
2. Story 1 (unwraps) — safety critical, do early
3. Story 2 (split app.rs) — biggest file, highest impact
4. Story 3 (split loop.rs) — second biggest
5. Story 4 (split common.rs) — third
6. Story 5 (split classifier.rs) — fourth
7. Story 7 (doc comments) — polish, do last

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- `cargo doc --workspace --no-deps` — no warnings
- **NO new features** — only refactoring and documentation
- **NO behavior changes** — all public APIs stay the same
- **NO new dependencies** — refactoring only
- Keep `pub use` exports stable (don't break downstream imports)
- When splitting files, keep types and functions in the same crate (don't move across crate boundaries)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo doc --workspace --no-deps 2>&1 | grep warning

# Verify no file over 300 lines (excluding tests)
find crates -name "*.rs" -not -path "*/tests/*" -exec wc -l {} + | sort -rn | head -20
```
