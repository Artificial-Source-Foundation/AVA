# Sprint 56: Quality Audit Action Plan

> Generated from 6 parallel audit sub-agents + Code Reviewer synthesis  
> **Date:** 2026-03-08  
> **Scope:** 22 crates, 229 Rust files, ~55,500 lines

---

## Executive Summary

| Category | Findings |
|----------|----------|
| **Total findings** | 150+ across 6 audit dimensions |
| **Critical** | 4 (P0) |
| **High** | 21 (P1) |
| **Medium** | 47 (P2) |
| **Low** | 80+ (P3) |
| **Estimated fix effort** | 3 sprints (P0: 1 sprint, P1: 1.5 sprints, P2: 0.5 sprint) |

### Key Themes
1. **ava-tui is the weakest crate** — 46/50 files untested, 11 unwraps in modals, 30+ `block_in_place` calls
2. **LLM parsing layer is untested** — 15 pub fns with 0 tests, 0 docs
3. **Panic risks exist** — 23 production unwraps/expects, 1 Critical (user-facing)
4. **Performance debt in hot paths** — 18 clone issues, 26 blocking async calls
5. **60.8% of public API undocumented** — 293/482 public items lack docs

---

## P0: Must Fix (Critical)

| # | Finding | File:Line | Category | Fix |
|---|---------|-----------|----------|-----|
| 1 | `AgentStack` expect on user-facing path | `ava-tui/src/state/agent.rs:107` | unwrap | Change to `ok_or_else()` and propagate `?` — callers already return `Result` |
| 2 | LLM parsing layer has 0 tests | `ava-llm/src/providers/common/parsing.rs:1-150` | test | Add unit tests for all 15 pub fns: malformed JSON, partial chunks, tool extraction |
| 3 | `reqwest::Client` expect in LLM pool | `ava-llm/src/pool.rs:73` | unwrap | Change return to `Result<Arc<Client>>`, propagate `?` to callers |
| 4 | TUI has `#![allow(dead_code)]` crate-wide | `ava-tui/src/lib.rs:8` | hygiene | Remove blanket allow, fix resulting warnings, apply `#[allow]` only where needed |

**P0 Rationale:** These are crash risks (1, 3), correctness risks (2), or process violations (4) that undermine AGENTS.md requirements. All are fixable within 1 sprint.

---

## P1: Should Fix (High)

| # | Finding | File:Line | Category | Fix |
|---|---------|-----------|----------|-----|
| 5 | 11 `unwrap()` calls in TUI modals | `ava-tui/src/app/modals.rs:457-568` | unwrap | Extract sub-screen handlers into separate methods, use `if let Some(state)` |
| 6 | `block_in_place` freezes TUI (14×) | `ava-tui/src/app/modals.rs` | perf | Replace with `tokio::spawn` + `AppEvent` channel, add loading indicators |
| 7 | `block_in_place` in commands (12×) | `ava-tui/src/app/commands.rs` | perf | Same as #6 |
| 8 | `model_catalog.rs` 3.6× over limit | `ava-config/src/model_catalog.rs` | modular | Split into 4 files: types.rs, fallback.rs, fetch.rs, mod.rs |
| 9 | `modals.rs` 2.4× over limit | `ava-tui/src/app/modals.rs` | modular | Split into 7 submodules: help.rs, settings.rs, model_selector.rs, etc. |
| 10 | Gemini provider 0 test coverage | `ava-llm/src/providers/gemini.rs` | test | Add provider-level unit tests matching OpenAI/Anthropic pattern |
| 11 | Circuit breaker mutex poisoning risk | `ava-llm/src/circuit_breaker.rs:40,75` | unwrap | Use `unwrap_or_else(|e| e.into_inner())` or switch to `parking_lot::Mutex` |
| 12 | `AgentStack` has 17 fields (god struct) | `ava-agent/src/stack.rs:78` | modular | Extract `AgentServices` and `StackConfig` sub-structs |
| 13 | `Theme` has 28 fields (god struct) | `ava-tui/src/state/theme.rs:4` | modular | Group into `ChromeColors`, `MessageColors`, `SyntaxColors`, `DiffColors`, `InputColors` |
| 14 | `AppState` has 17 fields (god struct) | `ava-tui/src/app/mod.rs:38` | modular | Decompose into `SessionState`, `UiState`, `AgentRunState`, `AppConfig` |
| 15 | Tool registry clones args every call | `ava-tools/src/registry.rs:140` | perf | Change `Tool::execute` to take `&Value` or use `Cow<Value>` |
| 16 | Agent loop clones session on complete | `ava-agent/src/agent_loop/mod.rs:~350` | perf | Emit `Arc<Session>` instead of cloning |
| 17 | Agent loop messages cloned 2-3× per turn | `ava-agent/src/agent_loop/mod.rs:~285-320` | perf | Use `Arc<Message>` for shared ownership |
| 18 | `parsing.rs` functions undocumented | `ava-llm/src/providers/common/parsing.rs` | doc | Add `///` docs to all 15 pub fns, document error conditions |
| 19 | 9 core traits undocumented | Multiple (see 03-doc-coverage.md) | doc | Document `Tool`, `LLMProvider`, `MCPTransport`, `SandboxBackend`, `CondensationStrategy`, `PermissionInspector`, `BrowserDriver`, `EditStrategy`, `SelfCorrector` |
| 20 | 16 error types undocumented | Multiple (see 03-doc-coverage.md) | doc | Document `AvaError`, `CodebaseError`, `ContextError`, `SandboxError`, `LspError`, `EditError`, `BrowserError`, `GitToolError` and 8 others |
| 21 | CodebaseSearch tool 0 tests | `ava-tools/src/core/codebase_search.rs` | test | Add tests for index lookup, empty results, error handling |
| 22 | Memory tools 0 tests | `ava-tools/src/core/memory.rs` | test | Add CRUD tests, search tests, concurrent access tests |

**P1 Rationale:** These are user-visible bugs (5-7), maintainability issues (8-14), performance problems in hot paths (15-17), or documentation gaps on critical types (18-20). Address in Sprint 57b.

---

## P2: Nice to Have (Medium)

| # | Finding | File:Line | Category | Fix |
|---|---------|-----------|----------|-----|
| 23 | `Result<T, String>` mixed with `AvaError` | `ava-config/src/model_catalog.rs`, `ava-commander/src/review.rs` | hygiene | Migrate 7 functions to `AvaError` variants |
| 24 | Zero `pub(crate)` usage codebase-wide | All crates | hygiene | Audit and narrow 50+ `pub` items to `pub(crate)` |
| 25 | 7 static Regex::new calls | `ava-codebase/src/indexer.rs`, `ava-commander/src/review.rs`, etc. | perf/hygiene | Use `std::sync::LazyLock` to compile once |
| 26 | `ava-tui` 46/50 files have 0 inline tests | `ava-tui/src/` | test | Add TestBackend rendering tests for widgets |
| 27 | 11 crates have 0 integration tests | Multiple | test | Add at least smoke tests for ava-auth, ava-config, ava-platform |
| 28 | `agent_loop/mod.rs` duplicates run() vs run_streaming() | `ava-agent/src/agent_loop/mod.rs` | modular | DRY up ~200 lines of shared logic |
| 29 | Permission middleware clones result every call | `ava-tools/src/permission_middleware.rs:45` | perf | Change `after()` signature to return `Option<ToolResult>` |
| 30 | TUI status bar clones separator 8× per frame | `ava-tui/src/ui/status_bar.rs` | perf | Use const or fn sep() -> Span |
| 31 | Agent loop allocates without capacity | `ava-agent/src/agent_loop/mod.rs:~271,~299-302` | perf | Add `with_capacity(4096)` for String, `with_capacity(4)` for Vecs |
| 32 | `workflow.rs` 2.2× over limit | `ava-commander/src/workflow.rs` | modular | Split into presets.rs, prompts.rs, executor.rs, types.rs |
| 33 | `review.rs` 2.1× over limit | `ava-commander/src/review.rs` | modular | Split into types.rs, diff.rs, prompt.rs, runner.rs |
| 34 | `HttpTransport` is incomplete stub | `ava-mcp/src/transport.rs` | hygiene | Feature-gate with `#[cfg(feature = "http-transport")]` |
| 35 | `WasmLoader` is incomplete stub | `ava-extensions/src/wasm_loader.rs` | hygiene | Document or remove, add tracking issue to TODO |
| 36 | `ava-validator` missing crate-level docs | `ava-validator/src/lib.rs` | doc | Add `//!` documentation |
| 37 | `ava-tools/src/core/` flat directory | 18 files | modular | Group into subdirs: file/, search/, vcs/, memory/, session/ |
| 38 | `ava-db` only 1.5 tests per file | `ava-db/src/` | test | Add SQLite integration tests, migration tests |
| 39 | `ava-sandbox` only 1.0 tests per file | `ava-sandbox/src/` | test | Add escape prevention, policy enforcement tests |

**P2 Rationale:** These improve maintainability and performance but aren't blocking. Address in Sprint 57c or as follow-up tech debt.

---

## P3: Backlog (Low)

| # | Finding | File:Line | Category | Fix |
|---|---------|-----------|----------|-----|
| 40-50 | 169 undocumented structs/enums | Multiple | doc | Add doc comments as files are touched |
| 51-60 | 28 undocumented fns returning Result | Multiple | doc | Document error conditions |
| 61-70 | 40+ files 300-500 lines | Multiple | modular | Split as part of feature work |
| 71-80 | Minor clone optimizations | Multiple | perf | Address only if profiles show impact |
| 81+ | Test coverage for type-only modules | Multiple | test | Add smoke tests for error types, structs |

**P3 Rationale:** Large volume of small issues. Address opportunistically during feature work.

---

## Suggested Fix Sprint Structure

### Sprint 57a: P0 Fixes (Week 1)
**Goal:** Eliminate crash risks and process violations
- Fix `agent.rs:107` expect (1 line change)
- Fix `pool.rs:73` expect (change signature, update ~5 callers)
- Remove `ava-tui` blanket `allow(dead_code)` (fix resulting warnings)
- Add tests for `parsing.rs` (15 fns, ~500 lines of tests)

**Estimated:** 3-4 files, 2-3 days

### Sprint 57b: P1 Fixes (Weeks 2-3)
**Goal:** Address user-visible bugs, hot-path performance, documentation gaps
- TUI unwraps in modals (split handlers, safe unwrapping)
- Replace 26 `block_in_place` calls with async tasks
- Split `model_catalog.rs` and `modals.rs` (mechanical extraction)
- Fix god structs: `AgentStack`, `Theme`, `AppState`
- Document 9 core traits and 16 error types
- Add Gemini and core tool tests
- Fix clone issues in agent loop and tool registry

**Estimated:** 25-30 files, 8-10 days

### Sprint 57c: P2 Improvements (Week 4)
**Goal:** Consistency, coverage, minor refactors
- Migrate `Result<T, String>` to `AvaError` (7 functions)
- Add `pub(crate)` visibility (50+ items)
- Regex LazyLock conversion (9 instances)
- TUI widget tests with TestBackend
- Split workflow.rs and review.rs
- Add integration tests for 11 crates

**Estimated:** 15-20 files, 4-5 days

---

## Cross-Reference Notes

### Convergent Findings (Multiple Reports Agree)

1. **ava-tui is the quality hotspot**
   - 01: 11 unwraps in modals.rs
   - 02: 46/50 files have 0 inline tests
   - 04: 3 god structs (AppState, AgentState, Theme)
   - 05: 30+ `block_in_place` calls
   - 06: `#![allow(dead_code)]` defeats clippy

2. **LLM parsing layer is critical**
   - 02: 15 pub fns, 0 tests (P0 Critical)
   - 03: All 15 fns undocumented
   - 06: Should be `pub(crate)` not `pub`

3. **File size violations cluster in specific modules**
   - 04: 8 files >500 lines, split plans for each
   - 06: 40 files >300 lines
   - Common files: model_catalog.rs, modals.rs, agent_loop/mod.rs, workflow.rs, review.rs

### Severity Discrepancies Resolved

| Finding | Discrepancy | Resolution |
|---------|-------------|------------|
| modals.rs 11 unwraps | 01: Medium, 06: High | **Medium** — Report 01's structural invariant analysis is more rigorous |
| `#![allow(dead_code)]` | 06: Medium | **High** — Process violation undermining AGENTS.md pre-commit checks |
| `pub(crate)` absence | 06: Medium | **Low** — Deeply entrenched style, high-effort fix for marginal benefit |

### Gaps Identified (Not Covered by 6 Audits)

Per Code Reviewer synthesis, these areas were **not audited**:

1. **Security: Credential handling**
   - No audit of API key storage/transmission
   - No check for credential logging
   - No memory zeroing verification

2. **Error message quality**
   - AGENTS.md requires "actionable and deterministic" errors
   - No review of actual error message content

3. **Async cancellation safety**
   - What happens on Ctrl+C mid-agent-loop?
   - Are MCP connections properly shut down?
   - Are `tokio::select!` branches cancellation-safe?

4. **TypeScript layer health**
   - Desktop-only per AGENTS.md, but pre-commit checks include npm
   - Could be silently failing

**Recommendation:** Schedule Sprint 58 for security audit + async cancellation safety review.

---

## AGENTS.md Compliance Status

| Requirement | Status | Notes |
|-------------|--------|-------|
| Rust-first for CLI/agent | ✅ Compliant | All audited code is Rust |
| Max 300 lines per file | ⚠️ 13% violation | 30 files exceed limit, 8 critically (>500 lines) |
| Pre-commit: `cargo test --workspace` | ⚠️ Risk | Tests pass but 31% of files have 0 coverage |
| Pre-commit: `cargo clippy --workspace` | ⚠️ Risk | `#![allow(dead_code)]` in ava-tui defeats clippy |
| No secrets in repo | ✅ Compliant | None found across 6 audits |
| Tool trait requires docs | ❌ Violation | `pub trait Tool` is undocumented (critical trait) |

---

## Action Plan for Fix Sprint

1. **Create tracking issues** for each P0/P1 finding in the sprint board
2. **Assign owners:**
   - P0 fixes: Any senior Rust dev (mechanical changes)
   - P1 performance: Dev familiar with async Rust (clone/blocking issues)
   - P1 docs: Technical writer or dev-on-call (document traits/errors)
   - P1 modular: Architecture owner (split plans are in Report 04)
3. **Review checkpoints:**
   - After P0: Verify no new panics via stress testing
   - After P1 modals fix: Manual TUI testing for model switching, auth
   - After P1 performance: Profile agent loop with `cargo flamegraph`
4. **Documentation:** Update AGENTS.md with lessons learned (e.g., visibility guidelines)

---

*This action plan is the key deliverable of Sprint 56. All findings have file:line references and concrete fix suggestions. Use this to create Sprint 57 issues.*
