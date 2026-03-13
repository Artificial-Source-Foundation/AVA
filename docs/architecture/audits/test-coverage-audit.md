# Test Coverage Audit — AVA Rust Crates

> Historical snapshot from 2026-03-08. This audit predates later crate removals and inventory cleanup; use `CLAUDE.md`, `AGENTS.md`, and `docs/development/roadmap.md` for current crate counts and active module inventory.

**Audit date:** 2026-03-08
**Auditor:** code-reviewer-subagent (claude-opus-4.6)
**Scope:** All 22 crates in `crates/`

---

## Summary

| Metric | Value |
|--------|-------|
| Total crates | 22 |
| Total `.rs` source files | 203 |
| Total inline test functions (`#[test]` + `#[tokio::test]` in `src/`) | 459 |
| Total integration test functions (in `tests/`) | 177 |
| **Total test functions** | **636** |
| Integration test files | 24 |
| Overall test-to-source ratio | **3.13 tests per source file** |
| `#[ignore]` tests | **0** |
| Source files with `#[cfg(test)]` blocks | 96 |
| Source files with NO test coverage at all | ~62 |

### Crates with ZERO integration tests

| Crate | Source Files | Inline Tests | Notes |
|-------|-------------|--------------|-------|
| `ava-auth` | 7 | 19 | Auth flows hard to unit-test; no integ harness |
| `ava-cli-providers` | 9 | 32 | CLI provider bridge; no integ tests |
| `ava-codebase` | 8 | 19 | Indexer/search; no integ tests |
| `ava-config` | 4 | 31 | Good inline coverage, missing integ |
| `ava-db` | 4 | 6 | DB layer; very low total coverage |
| `ava-logger` | 1 | 5 | Single file; no integ tests |
| `ava-lsp` | 4 | 15 | LSP client/transport; no integ |
| `ava-memory` | 1 | 6 | Single file; no integ tests |
| `ava-platform` | 3 | 13 | Shell/FS; no integ tests |
| `ava-sandbox` | 7 | 7 | Sandboxing; 1 test per file avg |
| `ava-types` | 6 | 25 | Good inline; no integ tests |

### Modules with ZERO test coverage (no inline tests AND not covered by integration tests)

**Critical (core agent/tool modules):**
- `crates/ava-tools/src/core/codebase_search.rs` — CodebaseSearch tool, **0 tests**
- `crates/ava-tools/src/core/memory.rs` — Memory tools (store/recall/forget), **0 tests**
- `crates/ava-tools/src/core/session_ops.rs` — Session operation tools, **0 tests**
- `crates/ava-tools/src/core/session_search.rs` — Session search tool, **0 tests**
- `crates/ava-tools/src/permission_middleware.rs` — Permission middleware, **0 tests**
- `crates/ava-tools/src/browser.rs` — Browser driver (covered by integ, but 0 inline)
- `crates/ava-llm/src/providers/gemini.rs` — Gemini provider, **0 mentions in integration tests**
- `crates/ava-llm/src/providers/common/parsing.rs` — 15 pub fns, **0 tests**
- `crates/ava-llm/src/providers/common/message_mapping.rs` — 3 pub fns, **0 tests**
- `crates/ava-llm/src/credential_test.rs` — 2 pub fns, **0 tests** (ironic)

**High (state management / context):**
- `crates/ava-context/src/manager.rs` — 10 pub fns, 0 inline (covered by integ)
- `crates/ava-session/src/lib.rs` — 9 pub fns, 0 inline (covered by integ)
- `crates/ava-session/src/helpers.rs` — 6 pub fns, **0 tests at all**
- `crates/ava-extensions/src/hook.rs` — 11 pub fns, **0 inline tests** (partial integ)
- `crates/ava-extensions/src/wasm_loader.rs` — 2 pub fns, **0 tests**

**Medium (TUI — 46/50 source files have no inline tests):**
- `crates/ava-tui/src/state/agent.rs` — 14 pub fns, **0 coverage**
- `crates/ava-tui/src/state/messages.rs` — 10 pub fns, partial integ only
- `crates/ava-tui/src/state/input.rs` — 9 pub fns, **0 coverage**
- `crates/ava-tui/src/state/session.rs` — 7 pub fns, **0 coverage**
- `crates/ava-tui/src/state/keybinds.rs` — 5 pub fns, **0 coverage**
- `crates/ava-tui/src/widgets/model_selector.rs` — 6 pub fns, **0 coverage**
- `crates/ava-tui/src/widgets/provider_connect.rs` — 5 pub fns, **0 coverage**
- `crates/ava-tui/src/widgets/autocomplete.rs` — 5 pub fns, partial integ only
- `crates/ava-tui/src/widgets/composer.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/command_palette.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/dialog.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/diff_preview.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/message.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/message_list.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/session_list.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/streaming_text.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/tool_approval.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/tool_list.rs` — **0 coverage**
- `crates/ava-tui/src/widgets/welcome.rs` — **0 coverage**
- `crates/ava-tui/src/app/commands.rs` — **0 coverage**
- `crates/ava-tui/src/app/event_handler.rs` — **0 coverage**
- `crates/ava-tui/src/app/modals.rs` — **0 coverage**
- `crates/ava-tui/src/config/*` — all 4 files, **0 inline tests**
- `crates/ava-tui/src/rendering/markdown.rs` — 0 inline (covered by integ)
- `crates/ava-tui/src/rendering/syntax.rs` — 0 inline (covered by integ)

**Low (type-only / error-only / glue modules):**
- `crates/ava-codebase/src/error.rs`, `types.rs` — type definitions
- `crates/ava-context/src/error.rs`, `types.rs` — type definitions
- `crates/ava-sandbox/src/error.rs`, `types.rs` — type definitions
- `crates/ava-lsp/src/error.rs` — error types
- `crates/ava-tools/src/edit/error.rs`, `request.rs` — types/request parsing

---

## Per-Crate Breakdown

### ava-agent
- **Source files:** 9
- **Inline tests:** 23
- **Integration tests:** 20 (4 files: `agent_loop.rs`, `e2e_test.rs`, `reflection_loop.rs`, `stack_test.rs`)
- **Coverage ratio:** 4.78 tests/source file
- **Untested modules:**
  - `agent_loop/response.rs` — no inline tests (exercised by integ)
  - `agent_loop/tool_execution.rs` — no inline tests (exercised by integ)
  - `llm_trait.rs` — trait definition only
- **Missing test scenarios:** Error recovery in tool execution; multi-turn agent loop edge cases

### ava-auth
- **Source files:** 7
- **Inline tests:** 19
- **Integration tests:** 0
- **Coverage ratio:** 2.71 tests/source file
- **Untested modules:**
  - `browser.rs` — 1 pub fn, browser launch (hard to test)
  - `device_code.rs` — 2 pub fns, device code flow
- **Missing test scenarios:** Integration test for full OAuth flow with mock server; device code polling; callback server lifecycle

### ava-cli-providers
- **Source files:** 9
- **Inline tests:** 32
- **Integration tests:** 0
- **Coverage ratio:** 3.56 tests/source file
- **Untested modules:**
  - `lib.rs` — glue module
  - `runner/args.rs` — argument parsing, no tests
  - `runner/execution.rs` — execution logic, no tests
- **Missing test scenarios:** Integration test for full CLI provider lifecycle; argument parsing edge cases; process execution error handling

### ava-codebase
- **Source files:** 8
- **Inline tests:** 19
- **Integration tests:** 0
- **Coverage ratio:** 2.38 tests/source file
- **Untested modules:**
  - `error.rs` — error types only
  - `types.rs` — 4 pub fns, data types
- **Missing test scenarios:** Integration test indexing a real project; repomap generation E2E; PageRank on realistic graphs

### ava-praxis
- **Source files:** 4
- **Inline tests:** 19
- **Integration tests:** 17 (2 files: `commander.rs`, `workflow_test.rs`)
- **Coverage ratio:** 9.0 tests/source file ✅
- **Untested modules:**
  - `events.rs` — event types, no inline tests (exercised by integ)
  - `lib.rs` — 17 pub fns, no inline tests (well-covered by integ)
- **Missing test scenarios:** Budget enforcement edge cases; domain routing stress tests

### ava-config
- **Source files:** 4
- **Inline tests:** 31
- **Integration tests:** 0
- **Coverage ratio:** 7.75 tests/source file ✅
- **Untested modules:** None (all files have `#[cfg(test)]`)
- **Missing test scenarios:** Integration test for config file loading from disk; credential rotation; model catalog version migration

### ava-context
- **Source files:** 11
- **Inline tests:** 26
- **Integration tests:** 8 (1 file: `manager.rs`)
- **Coverage ratio:** 3.09 tests/source file
- **Untested modules:**
  - `manager.rs` — 0 inline, fully covered by integ
  - `error.rs`, `types.rs` — type definitions
  - `strategies/mod.rs` — strategy dispatch glue
- **Missing test scenarios:** Context compaction under token pressure; hybrid condenser stress tests; strategy selection edge cases

### ava-db
- **Source files:** 4
- **Inline tests:** 6
- **Integration tests:** 0
- **Coverage ratio:** 1.50 tests/source file ⚠️
- **Untested modules:**
  - `models/mod.rs` — module re-exports
- **Missing test scenarios:** SQLite migration testing; concurrent read/write; session/message CRUD edge cases; disk full error handling

### ava-extensions
- **Source files:** 5
- **Inline tests:** 4
- **Integration tests:** 5 (1 file: `extension_manager.rs`)
- **Coverage ratio:** 1.80 tests/source file ⚠️
- **Untested modules:**
  - `hook.rs` — 11 pub fns, partial integ coverage
  - `wasm_loader.rs` — 2 pub fns, **0 tests**
  - `lib.rs` — re-exports
- **Missing test scenarios:** WASM extension loading; hook priority ordering; extension hot-reload; error propagation in hook chains

### ava-llm
- **Source files:** 17
- **Inline tests:** 32
- **Integration tests:** 17 (1 file: `providers.rs`)
- **Coverage ratio:** 2.88 tests/source file
- **Untested modules (CRITICAL):**
  - `providers/gemini.rs` — **0 integration mentions**, Gemini is a recommended model
  - `providers/common/parsing.rs` — **15 pub fns, 0 tests** (response parsing logic!)
  - `providers/common/message_mapping.rs` — **3 pub fns, 0 tests** (message format conversion)
  - `router.rs` — 9 pub fns, 0 inline (covered by integ)
  - `credential_test.rs` — 2 pub fns, **0 tests** (credential test utility with no tests for itself)
  - `providers/anthropic.rs` — 0 inline (12 integ mentions)
  - `providers/openai.rs` — 0 inline (17 integ mentions)
  - `providers/openrouter.rs` — 0 inline (5 integ mentions)
  - `providers/ollama.rs` — 0 inline (9 integ mentions)
  - `providers/mock.rs` — 0 inline (used as test helper)
- **Missing test scenarios:** Gemini provider unit tests; parsing edge cases (malformed JSON, partial responses); message mapping for all provider formats; stream parsing; error mapping per provider

### ava-logger
- **Source files:** 1
- **Inline tests:** 5
- **Integration tests:** 0
- **Coverage ratio:** 5.0 tests/source file ✅
- **Untested modules:** None
- **Missing test scenarios:** Log rotation; file output; concurrent logging

### ava-lsp
- **Source files:** 4
- **Inline tests:** 15
- **Integration tests:** 0
- **Coverage ratio:** 3.75 tests/source file
- **Untested modules:**
  - `error.rs` — error types
  - `lib.rs` — module glue
- **Missing test scenarios:** LSP server connection lifecycle; diagnostics parsing; transport error recovery

### ava-mcp
- **Source files:** 6
- **Inline tests:** 23
- **Integration tests:** 8 (1 file: `mcp.rs`)
- **Coverage ratio:** 5.17 tests/source file ✅
- **Untested modules:**
  - `server.rs` — 3 pub fns, 0 inline (covered by integ)
  - `lib.rs` — re-exports
- **Missing test scenarios:** MCP server error handling; transport reconnection; concurrent tool calls

### ava-memory
- **Source files:** 1
- **Inline tests:** 6
- **Integration tests:** 0
- **Coverage ratio:** 6.0 tests/source file ✅
- **Untested modules:** None
- **Missing test scenarios:** Memory persistence across restarts; search ranking; concurrent access

### ava-permissions
- **Source files:** 9
- **Inline tests:** 83
- **Integration tests:** 6 (1 file: `permission_system.rs`)
- **Coverage ratio:** 9.89 tests/source file ✅ BEST
- **Untested modules:**
  - `classifier/parser.rs` — 0 inline (exercised indirectly)
  - `classifier/rules.rs` — 1 pub fn, 0 inline
  - `lib.rs` — re-exports
- **Missing test scenarios:** Concurrent permission checks; policy hot-reload; malicious path traversal edge cases

### ava-platform
- **Source files:** 3
- **Inline tests:** 13
- **Integration tests:** 0
- **Coverage ratio:** 4.33 tests/source file
- **Untested modules:** None (all files have `#[cfg(test)]`)
- **Missing test scenarios:** Cross-platform shell detection; FS operations on symlinks; Windows path handling

### ava-sandbox
- **Source files:** 7
- **Inline tests:** 7
- **Integration tests:** 0
- **Coverage ratio:** 1.0 tests/source file ⚠️
- **Untested modules:**
  - `error.rs` — error types
  - `types.rs` — type definitions
- **Missing test scenarios:** Sandbox escape prevention; Linux namespace isolation; macOS sandbox-exec profiles; policy enforcement under load

### ava-session
- **Source files:** 2
- **Inline tests:** 0
- **Integration tests:** 7 (1 file: `session_manager.rs`)
- **Coverage ratio:** 3.50 tests/source file
- **Untested modules:**
  - `helpers.rs` — 6 pub fns, **0 tests anywhere**
- **Missing test scenarios:** Session fork/search edge cases; concurrent session access; `helpers.rs` utility functions

### ava-tools
- **Source files:** 33
- **Inline tests:** 53
- **Integration tests:** 40 (3 files: `core_tools_test.rs`, `tool_registry.rs`, `browser_dispatch.rs`)
- **Coverage ratio:** 2.82 tests/source file
- **Untested modules (CRITICAL):**
  - `core/codebase_search.rs` — CodebaseSearch tool, **0 tests**
  - `core/memory.rs` — Memory tools (3 pub fns), **0 tests**
  - `core/session_ops.rs` — Session tools (2 pub fns), **0 tests**
  - `core/session_search.rs` — SessionSearch tool, **0 tests**
  - `permission_middleware.rs` — PermissionMiddleware, **0 tests**
  - `registry.rs` — 10 pub fns, 0 inline (covered by integ)
  - `core/edit.rs` — 0 inline, partial integ
  - `core/glob.rs` — 0 inline, partial integ
  - `core/grep.rs` — 0 inline, partial integ
  - `core/read.rs` — 0 inline, partial integ
  - `core/write.rs` — 0 inline, partial integ
  - `edit/request.rs` — 5 pub fns, **0 tests**
  - `edit/error.rs` — error types
- **Missing test scenarios:** Codebase search with real project; memory CRUD operations; session tool operations; glob/grep edge cases (symlinks, large files); permission middleware blocking; edit request validation

### ava-tui
- **Source files:** 50
- **Inline tests:** 18
- **Integration tests:** 37 (7 files)
- **Coverage ratio:** 1.10 tests/source file ⚠️
- **Untested modules (extensive — 46/50 files have no inline tests):**
  - `state/agent.rs` — 14 pub fns, **0 coverage**
  - `state/input.rs` — 9 pub fns, partial integ
  - `state/session.rs` — 7 pub fns, **0 coverage**
  - `state/keybinds.rs` — 5 pub fns, **0 coverage**
  - `state/theme.rs` — 4 pub fns, partial integ
  - `state/mod.rs` — 0 coverage
  - All 13 untested widget modules (composer, command_palette, dialog, diff_preview, message, message_list, model_selector, provider_connect, session_list, streaming_text, tool_approval, tool_list, welcome)
  - `app/commands.rs`, `app/event_handler.rs`, `app/modals.rs` — **0 coverage**
  - `config/cli.rs`, `config/keybindings.rs`, `config/themes.rs`, `config/mod.rs` — **0 coverage**
  - `headless.rs`, `event.rs`, `transcribe.rs`, `review.rs`, `auth.rs` — mostly **0 coverage**
- **Missing test scenarios:** Widget rendering tests with TestBackend; state mutation tests; keybind dispatch; theme switching; session list CRUD; model selector flow; command palette navigation; event handler edge cases; config parsing

### ava-types
- **Source files:** 6
- **Inline tests:** 25
- **Integration tests:** 0
- **Coverage ratio:** 4.17 tests/source file ✅
- **Untested modules:** None (all files have `#[cfg(test)]`)
- **Missing test scenarios:** Serialization roundtrips; error conversion chains

### ava-validator
- **Source files:** 3
- **Inline tests:** 0
- **Integration tests:** 12 (2 files: `validation_pipeline.rs`, `validation_pipeline_robustness.rs`)
- **Coverage ratio:** 4.0 tests/source file
- **Untested modules:** All 3 src files have 0 inline tests (fully reliant on integ)
- **Missing test scenarios:** Validator error propagation; pipeline ordering; concurrent validation

---

## Priority Recommendations

### 🔴 P0 — Critical (blocks correctness of core features)

1. **`ava-llm/src/providers/common/parsing.rs`** — 15 pub fns with ZERO tests. This is the LLM response parsing layer. A parsing bug here corrupts every agent interaction. Add unit tests for: malformed JSON, partial streaming chunks, tool call extraction, content block parsing.

2. **`ava-tools/src/core/codebase_search.rs`** — CodebaseSearch is a user-facing tool with 0 tests. Test: index lookup, empty results, error handling.

3. **`ava-tools/src/core/memory.rs`** — Memory tools (store/recall/forget) are agent-facing with 0 tests. Memory corruption = agent context loss. Test: CRUD operations, search, concurrent access.

4. **`ava-llm/src/providers/gemini.rs`** — Gemini is listed as a recommended model in AGENTS.md but has ZERO integration test mentions. Add provider-level unit tests matching the pattern used for OpenAI/Anthropic.

5. **`ava-llm/src/providers/common/message_mapping.rs`** — Message format conversion with 0 tests. Wrong mapping = wrong prompts to LLMs.

### 🟠 P1 — High (significant coverage gaps in important subsystems)

6. **`ava-tui` state modules** — `state/agent.rs` (14 pub fns), `state/input.rs` (9), `state/session.rs` (7), `state/keybinds.rs` (5) all have **0 coverage**. These are the state management backbone of the TUI. Add unit tests using mock state.

7. **`ava-tools/src/core/session_ops.rs` + `session_search.rs`** — Session tools with 0 tests. These are user-facing tools.

8. **`ava-tools/src/permission_middleware.rs`** — Permission enforcement for tools, 0 tests. A bug here could allow unsafe operations. Unit test: allow/deny decisions, edge cases.

9. **`ava-session/src/helpers.rs`** — 6 pub utility fns, 0 tests anywhere. Utility functions often have subtle edge cases.

10. **`ava-sandbox`** — Only 7 tests for 7 source files (1.0 ratio). Sandbox correctness is security-critical. Add: escape prevention tests, policy enforcement tests, resource limit tests.

### 🟡 P2 — Medium (moderate risk, should be addressed)

11. **`ava-db`** — 6 tests for 4 files (1.5 ratio), no integration tests. Add: SQLite CRUD integration test, concurrent access, migration testing.

12. **`ava-extensions/src/wasm_loader.rs`** — 0 tests for WASM extension loading. Add: load/unload lifecycle, malformed WASM handling.

13. **`ava-extensions/src/hook.rs`** — 11 pub fns, 0 inline tests. Hook chain ordering and error propagation need testing.

14. **`ava-cli-providers/src/runner/args.rs` + `execution.rs`** — CLI runner with 0 tests. Argument parsing and process execution edge cases.

15. **`ava-tui` widgets** — 13 widget modules with 0 coverage. Start with the most complex: `composer.rs`, `command_palette.rs`, `model_selector.rs`. Use `ratatui::backend::TestBackend` for rendering tests.

### 🟢 P3 — Low (nice to have)

16. **Integration test suites** for `ava-auth`, `ava-config`, `ava-platform`, `ava-types` — these have good inline coverage but no integration tests to verify cross-module interactions.

17. **`ava-lsp`** — 15 inline tests but no integration test. An integration test with a mock LSP server would catch transport issues.

18. **`ava-codebase`** — Good inline tests but no integration test. An E2E test indexing a fixture project would catch real-world issues.

19. **`ava-llm/src/credential_test.rs`** — A file named "credential_test" that has 0 tests for itself. Either add tests or merge into the credentials module.

20. **`ava-tui` config modules** — `cli.rs`, `keybindings.rs`, `themes.rs` all lack tests. Add: config parsing, default handling, validation.

---

## AGENTS.md Compliance Notes

Per AGENTS.md: *"Before Committing"* requires `cargo test --workspace` and `cargo clippy --workspace`. The test suite itself runs, but this audit reveals that **~62 source files (31% of the codebase) have ZERO test coverage** — neither inline tests nor integration test imports. The most concerning gaps are in the LLM response parsing layer, core tools, and TUI state management.

The AGENTS.md instruction *"Add tests, run `cargo test -p ava-tools`"* for new tools is being followed for some tools (bash, apply_patch, custom_tool, diagnostics, git_read, lint, test_runner) but not for others (codebase_search, memory, session_ops, session_search, glob, grep, read, write, edit, multiedit). **6 of 19 core tools have zero inline tests.**
