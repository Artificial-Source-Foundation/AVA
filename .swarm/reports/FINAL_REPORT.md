# AVA Codebase Swarm Audit — Final Report

**Audit date:** 2026-03-20
**Rounds executed:** 4
**Rust crates in scope:** 21
**Baseline commit:** `e2fd426` (chore: baseline for swarm audit)
**Final commit:** `2d0eeb6` (fix: round 4 — regex safety tests, spawn error logging)

---

## Executive Summary

| Metric | Baseline | Final | Delta |
|--------|----------|-------|-------|
| Rust tests passing | 1,756 | 1,769 | +13 |
| Rust tests failing | 0 | 0 | 0 |
| Clippy warnings | 0 | 0 | 0 |
| `cargo check` | PASS | PASS | — |
| Critical issues | 20 | 3 | −17 |
| Warning-level issues | ~70 | ~37 | −33 |
| Files modified | — | 90 | — |
| Lines added / removed | — | +3,250 / −570 | — |

The audit ran 4 sequential rounds: analysis → fixes → verdict → next-round focus. Each round required a clean `cargo check` and zero `cargo test` failures before proceeding. All 4 rounds passed their gate checks. The 3 remaining criticals are architectural/structural changes deferred by design (see Remaining Manual Action Items).

**Build health before vs after:** The build was already clean at baseline (0 clippy warnings, 0 test failures). The audit improved correctness and security posture without breaking anything. Test count rose from 1,756 to 1,769 (+13 net; round reports tracked intermediate counts of 1,739/1,750/1,767/1,769 due to test suite restructuring in unrelated in-flight work).

---

## Security Fixes

### SBPL Injection Prevention (Round 1 — Critical, CWE-94)
**File:** `crates/ava-sandbox/src/macos.rs`

The macOS sandbox-exec profile builder interpolated user/policy-supplied path strings directly into SBPL rule strings without escaping. A path containing `"` or `(` could break out of the SBPL string literal and inject arbitrary sandbox rules, potentially granting full filesystem access to the sandboxed process.

**Fix:** Added `escape_sbpl_path()` that rejects paths containing `"`, `\`, `(`, or `)` with `SandboxError::InvalidPolicy`. Applied to all four path-embedding sites (read-only paths, writable paths, working-dir read, working-dir write). Added test `sbpl_path_injection_is_rejected()`.

### Environment Variable Scrubbing for Bash (Round 1 — Critical, CWE-214)
**File:** `crates/ava-tools/src/core/bash.rs`

Non-sandboxed bash execution passed `scrub_env: false` and `env_vars: Vec::new()`, causing every LLM-spawned shell command to inherit the full parent environment including `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `AWS_SECRET_ACCESS_KEY`, and all other secrets present in the shell.

**Fix:** Changed both `execute_with_options` and `execute_streaming_with_options` call sites to use `env_vars: filtered_env()` and `scrub_env: true`, applying the same allowlist (`PATH`, `HOME`, `USER`, `SHELL`, `LANG`, `LC_ALL`, `LC_CTYPE`, `TERM`, `CARGO_HOME`, `RUSTUP_HOME`) that was already used for sandboxed install-class commands. The fix is symmetric — the same allowlist now applies to both execution paths.

### rm -rf Block-List Hardening with POSIX Tokenizer (Round 2 — Critical, CWE-184)
**File:** `crates/ava-permissions/src/classifier/rules.rs`

The critical-path check used string replacement to strip flags then compared the remainder to a four-path list (`/`, `~`, `/*`, `~/*`). Six bypass classes existed: separated flags (`rm -f -r /`), double spaces, env-var expansion (`rm -rf $HOME`), quoted paths (`rm -rf "/"`), shell metacharacters splitting tokens, and the limited 4-path list missing `/home`, `/etc`, `/usr`, `/root`, etc.

**Fix:** Replaced entirely with a POSIX tokenizer:
- `collect_flags()` — accumulates all single-char flags from combined (`-rf`) and separated (`-r -f`) tokens
- `has_recursive_force()` — checks for `r`+`f` flags in any combination
- `extract_rm_paths()` — strips wrapping quotes and rejects tokens containing shell metacharacters (`$`, `;`, `|`, `&`, `` ` ``)
- `path_is_critical()` — normalizes and checks against 17 critical path prefixes (`/`, `~`, `/home`, `/root`, `/etc`, `/usr`, `/var`, `/boot`, `/sys`, `/proc`, `/dev`, `/lib`, `/lib64`, `/bin`, `/sbin`, `/opt`, plus wildcards)
- `has_pipe_to_shell()` — rewrote to detect any known shell binary by name (`sh`, `bash`, `zsh`, `fish`, `dash`, `ksh`, `csh`, `tcsh`, `ash`) after a pipe, including `/bin/sh` absolute paths

Added 14 regression tests covering all 6 bypass classes identified in analysis.

### Remaining Security Posture Assessment

**Fixed:** The three highest-impact security issues (SBPL injection, credential exfiltration, rm-rf bypass) are resolved.

**Unresolved warning-level items (require architectural change or policy decision):**

- **Sandbox model inversion** (`bash.rs`): Sandbox is still opt-in (install-class only). The TODO to sandbox ALL commands by default remains. Until inverted, arbitrary LLM-generated shell commands run unsandboxed on the host.
- **TOCTOU in path_guard** (`path_guard.rs`): `exists()` before `canonicalize()` has a symlink-race window under concurrent access. Best-effort under normal operation.
- **OAuth fixed port** (`callback.rs`): Port 1455 is predictable. Any local process can race the callback. A random ephemeral port would reduce predictability.
- **JWT without signature verification** (`tokens.rs`): Account ID extracted from unverified JWT is used in outbound `x-initiator` headers. Risk is mitigated if the OAuth endpoint is trusted, but not verified in code.
- **MCP SENSITIVE_ENV_VARS blocklist incomplete** (`transport.rs`): Allowlist approach (strip all, re-inject needed) would be more robust.
- **Custom TOML tool interpreter not allowlisted** (`custom_tool.rs`): `interpreter` field is user-controlled with no validation against a known-safe set.
- **SSRF DNS rebinding** (`web_fetch.rs`): DNS resolved at validation time then re-resolved at connection time. A short-TTL rebind can bypass the IP blocklist.

---

## Performance Improvements

### Blocking I/O → Async

**Session logger** (`crates/ava-agent/src/session_logger.rs` — Round 1): `std::fs::OpenOptions::new().open()` was called on every agent turn inside the async loop. Fixed by wrapping the file open + write in `tokio::task::spawn_blocking`. Fire-and-forget (best-effort logging).

**Streaming diff** (`crates/ava-agent/src/streaming_diff.rs` — Round 1): `std::fs::read_to_string()` called synchronously in `snapshot_before_edit()` and `record_edit_complete()` from async tool execution context. Fixed by adding `snapshot_before_edit_async()` and `record_edit_complete_async()` async counterparts that offload I/O to `spawn_blocking`. Sync versions retained for legitimate `spawn_blocking` call sites.

**Instruction loading** (`crates/ava-agent/src/instruction_resolver.rs` — Round 2): `build_system_prompt_suffix()` performed blocking filesystem traversal (`fs::canonicalize`, `fs::read_dir`, `fs::read_to_string` across ~10 call sites) in an async context while potentially holding an `RwLock` guard. Fixed by adding `build_system_prompt_suffix_async()` that wraps the sync implementation in `tokio::task::spawn_blocking`. `stack_run.rs` updated to `await` the async version with no lock held across the await point.

### Trust State Caching

**File:** `crates/ava-config/src/trust.rs` (Round 1)

`is_project_trusted()` read and parsed a JSON file on every invocation. Called on every `read` tool result in the hot agent loop — 100 read calls = 100 disk reads + 100 JSON parses of the same file.

**Fix:** Added a process-scoped `static TRUST_CACHE: RwLock<Option<HashSet<PathBuf>>>`. Fast-path read lock check on cache hit; disk load + cache population only on miss. `trust_project()` invalidates the cache after writing.

### SessionManager Connection Pooling

**File:** `crates/ava-session/src/lib.rs` (Round 1)

`open_conn()` created a new `rusqlite::Connection` on every method call (~17+ calls per save, each with PRAGMA setup costing 0.5–2ms).

**Fix:** Added `conn: std::sync::Mutex<Connection>` field to `SessionManager`. Changed `open_conn()` to return `MutexGuard<Connection>` instead of a fresh connection. All 18 call sites continue to work transparently via `Deref`. Connection opened once at `SessionManager::new()`.

### ToolCall Clone Elimination

**File:** `crates/ava-agent/src/agent_loop/tool_execution.rs` (Round 2)

`tool_call.clone()` was called unconditionally before every tool execution to support the name-repair path, even when no repair was needed. This allocated a new `String` for name/id and deep-copied the `serde_json::Value` arguments on every tool call.

**Fix:** Changed to call `repair_tool_name()` first, then allocate an owned `ToolCall` (via clone) only when the name actually changed. The repair path is rare; the common path now avoids the allocation entirely.

---

## Code Quality

### Error Handling Improvements

**`From<io::Error>` ErrorKind preservation** (`crates/ava-types/src/error.rs` — Round 3): Previously discarded `ErrorKind`, mapping all `io::Error` to opaque `AvaError::IoError(String)`. Fixed with structured match: `NotFound → AvaError::NotFound`, `PermissionDenied → AvaError::PermissionDenied`, `TimedOut | WouldBlock → AvaError::TimeoutError` (retryable), all others → `IoError` legacy fallback.

**`ProjectState::save()` error type** (`crates/ava-config/src/lib.rs` — Round 3): Return type changed from `Result<(), String>` to `ava_types::Result<()>`. Three `map_err(|e| e.to_string())` chains replaced with structured `AvaError::from` conversions (preserving `ErrorKind` for I/O operations) and an explicit `AvaError::SerializationError` for the `serde_json` call.

**`run_validation_tool` chain** (`crates/ava-agent/src/agent_loop/tool_execution.rs` — Round 3): `run_validation_tool()` return type changed from `Result<ToolResult, String>` to `ava_types::Result<ToolResult>`. The `map_err(|e| e.to_string())` erasure removed; underlying `tools.execute()` error propagates naturally via `?`.

**AvaError legacy variant deprecation** (`crates/ava-types/src/error.rs` — Round 3): All 10 legacy string-payload variants (`ToolError`, `IoError`, `PlatformError`, `ConfigError`, `NotFound`, `PermissionDenied`, `SerializationError`, `ValidationError`, `DatabaseError`, `TimeoutError`) received deprecation doc comments with migration guidance pointing to structured variants.

### Debug Output Cleanup

**Streaming parser** (`crates/ava-llm/src/providers/common/parsing.rs` — Round 1): 5-line `eprintln!` block printing `[REASONING] delta.reasoning_content = ...` on every OpenAI streaming chunk in production removed.

**Praxis review** (`crates/ava-praxis/src/review.rs` — Round 1): Three debug `eprint!`/`eprintln!` calls streaming raw review agent output to stderr removed (`eprint!("{t}")` per token, `eprintln!("[tool: {}]")` per tool call, trailing newline).

**Web server startup** (`crates/ava-tui/src/web/mod.rs` — Round 2): Five consecutive `eprintln!` calls for server URL and endpoint announcements replaced with `tracing::info!` for consistency with structured logging throughout the rest of the application.

**Production eprintln audit** (Round 4): All 32 flagged files in `crates/ava-tui/` audited. All remaining `eprintln!`/`println!` instances confirmed as intentional (pre-logging-init CLI messages, interactive auth output, headless progress output, plugin subcommand output). No changes needed.

### WasmLoader Stub Gating

**File:** `crates/ava-extensions/src/wasm_loader.rs` (Round 1)

`WasmLoader::load()` was a public API that always returned `Err(Unsupported)` while being exported from the crate's public API surface. Callers could not distinguish "not yet implemented" from a runtime error.

**Fix:** Changed `WasmLoader` from `pub` to `pub(crate)` and removed the re-export from `lib.rs`. Improved error message to explicitly say "not yet implemented" and guide callers to native extensions or MCP servers. Added two unit tests (`load_returns_unsupported`, `load_error_message_is_actionable`).

### MCP HttpTransport Fix

**File:** `crates/ava-mcp/src/transport.rs` (Round 1)

`HttpTransport::send()` silently discarded all messages (`let _ = message; Ok(())`), causing callers to believe messages were delivered when they were not.

**Fix:** Changed `send()` to return `Err(AvaError::ToolError(...))` with a clear message guiding callers to use stdio transport. Added two tests (`http_transport_send_fails_loudly`, `http_transport_receive_fails_loudly`).

### Named Constants for Magic Numbers

(Round 2)

- `DEFAULT_MAX_RETRIES` in `ava-llm/src/providers/common/mod.rs` changed from `pub(crate)` to `pub const`. All five providers (`openai.rs`, `anthropic.rs`, `gemini.rs`, `copilot.rs`, `openrouter.rs`) updated to reference the constant instead of the literal `3`.
- `DEFAULT_FAILURE_THRESHOLD: u32 = 5` and `DEFAULT_COOLDOWN_SECS: u64 = 30` added to `ava-llm/src/circuit_breaker.rs` with doc comments.
- `MEMORY_BLOCK_MAX_CHARS: usize = 2000` added to `ava-types/src/lib.rs`. Two independent sites (`ava-agent/src/memory_enrichment.rs` and `ava-praxis/src/synthesis.rs`) updated to use the shared constant.
- `INSTRUCTION_BUDGET_DIVISOR: usize = 3` added to `ava-agent/src/instruction_resolver.rs` with rationale doc comment ("Reserve 1/3 of context for instructions, leaving 2/3 for conversation history and tool results").

### LLMProvider Trait Documentation

**File:** `crates/ava-llm/src/provider.rs` (Round 1)

Replaced a misleading inline TODO comment about plugin request-header injection with a proper doc-comment section that accurately states the hook is not wired through the trait and explains where injection must be done instead (HTTP client level in each provider implementation).

### Spawn Error Surfacing

**File:** `crates/ava-tui/src/state/agent.rs` (Round 4)

Three fire-and-forget `tokio::spawn` calls for `set_thinking_level`, `cycle_thinking`, and `set_mode` previously swallowed task panics silently. Fixed by capturing each `JoinHandle` and spawning a lightweight watcher task that awaits the handle and calls `tracing::warn!` if the task panicked. Full `AppEvent::Error` routing deferred pending `AgentStack` method signature changes.

---

## Test Coverage

### Regex Compile Safety Tests

Added `regexes_compile()` tests across 5 crates to ensure `LazyLock<Regex>.unwrap()` panics surface at `cargo test` time rather than at runtime on first use:

| Crate | File | Statics covered |
|-------|------|----------------|
| `ava-praxis` | `src/review.rs` | `RE_DIFF_STAT`, `RE_ISSUE` (Round 3) |
| `ava-tools` | `src/core/web_fetch.rs` | `RE_SCRIPT`, `RE_STYLE`, `RE_BLOCK`, `RE_TAGS`, `RE_BLANK_LINES` (Round 3) |
| `ava-context` | `src/strategies/relevance.rs` | `RE_FILE_PATH` (Round 3) |
| `ava-codebase` | `src/indexer.rs` | `RUST_USE_RE`, `JS_IMPORT_RE`, `JS_REQUIRE_RE`, `PY_IMPORT_RE`, `PY_FROM_RE` (Round 4) |
| `ava-tools` | `src/core/web_search.rs` | `RESULT_RE`, `TAG_RE` (Round 4) |

### Permission Middleware Tests

**File:** `crates/ava-tools/src/permission_middleware.rs` (Round 3)

Added 5 tests closing the gap in critical path coverage:

1. `deny_propagates_as_permission_denied` — `DenyInspector` → `AvaError::PermissionDenied`
2. `ask_without_bridge_propagates_as_permission_denied` — `Ask` result with no approval bridge → error
3. `rejection_propagates_as_permission_denied` — user rejects via bridge → error with reason string
4. `auto_approve_context_bypasses_bridge_for_allowed_tools` — `AllowInspector` + `auto_approve=true` + no bridge → `Ok`
5. `after_passthrough` — `Middleware::after` returns result unchanged

### Budget Tracking Unit Tests

**File:** `crates/ava-agent/src/budget.rs` (Round 3)

Added 9 unit tests for `BudgetTelemetry`:

1. `observe_accumulates_cost`
2. `observe_emits_threshold_warnings` — verifies 50% threshold fires
3. `threshold_warnings_are_emitted_only_once` — dedup guard
4. `no_warnings_when_no_budget_set`
5. `budget_exhausted_when_over_limit`
6. `remaining_budget_none_when_no_limit`
7. `budget_status_label_with_limit`
8. `budget_status_label_without_limit`
9. `non_usage_events_do_not_accumulate_cost`

### rm -rf Security Regression Tests

**File:** `crates/ava-permissions/src/classifier/rules.rs` (Round 2)

Added 14 regression tests covering all 6 bypass classes: separated flags, double spaces, env-var expansion, quoted paths, shell metacharacters in token stream, and path variants beyond the original 4-path list.

### Test Count

| Round | Tests passing |
|-------|--------------|
| Baseline | 1,756 |
| Round 1 | 1,739* |
| Round 2 | 1,750 |
| Round 3 | 1,767 |
| Round 4 | 1,769 |

*Round 1 intermediate count reflects concurrent in-flight test restructuring unrelated to the audit; no failures at any round.

**Net new tests from audit: +13 confirmed.** Actual new test functions added: ~34 (rm -rf regression suite, regex compile tests, permission middleware tests, budget telemetry tests, WasmLoader/HttpTransport tests). The delta from 1,756 → 1,769 reflects net change after test restructuring.

---

## Round-by-Round Progress

### Round 1 — Foundation Pass
**Verdict:** PASS | **Critical remaining after:** 8 | **Fixes applied:** 13

Key changes: Removed debug `eprintln!` blocks from production streaming paths (`parsing.rs`, `review.rs`). Converted `.expect()` panic in background task to graceful error channel send (`background.rs`). Closed SBPL injection with escape function + test (`macos.rs`). Applied env scrubbing to non-sandboxed bash execution (`bash.rs`). Moved session logger file I/O to `spawn_blocking`. Added async counterparts for streaming diff I/O. Cached trust state in-process. Pooled SQLite connection in `SessionManager`. Gated `WasmLoader` behind `pub(crate)`. Fixed `HttpTransport` silent data loss. Cleaned up misleading LLM provider trait TODO.

**Critical count trend: 20 → 8**

### Round 2 — Security & Performance Pass
**Verdict:** PASS | **Critical remaining after:** 5 | **Fixes applied:** 11

Key changes: Replaced rm -rf string-replacement check with full POSIX tokenizer covering all 6 bypass classes + 14 regression tests. Eliminated unconditional `ToolCall` clone on tool execution. Wrapped blocking instruction loading in `spawn_blocking` and updated async call site. Published `DEFAULT_MAX_RETRIES` constant to all 5 providers. Extracted circuit breaker magic literals to named constants. Converted web server `eprintln!` to `tracing::info!`. Added `MEMORY_BLOCK_MAX_CHARS` shared constant across 2 crates. Named instruction budget divisor constant.

**Critical count trend: 8 → 5**

### Round 3 — Error Handling & Test Coverage Pass
**Verdict:** PASS | **Critical remaining after:** 3 | **Fixes applied:** 10

Key changes: `From<io::Error>` now preserves `ErrorKind` with structured dispatch. `ProjectState::save()` changed to `ava_types::Result<()>`. `run_validation_tool` chain changed to `ava_types::Result<ToolResult>` throughout. All 10 legacy `AvaError` string variants received deprecation doc comments. Added `regexes_compile()` tests to `ava-praxis`, `ava-tools/web_fetch`, `ava-context/relevance`. Added 5 permission middleware tests. Added 9 `BudgetTelemetry` tests. (+17 net new tests this round)

**Critical count trend: 5 → 3**

### Round 4 — Final Safety & Logging Pass
**Verdict:** PASS | **Critical remaining after:** 3 | **Fixes applied:** 3 (1 deferred)

Key changes: Added `regexes_compile()` tests to `ava-codebase` indexer (5 statics) and `ava-tools/web_search` (2 statics). Improved fire-and-forget spawn error surfacing in `ava-tui/state/agent.rs` — all three sites now capture `JoinHandle` and log panics via `tracing::warn!`. Production `eprintln!`/`println!` audit confirmed all 32 files use intentional CLI output. Codebase `JoinHandle` storage deferred as structural `AgentStack` change.

**Critical count trend: 3 → 3** (remaining 3 are all architectural changes requiring structural refactors)

---

## Remaining Manual Action Items

### Architectural Changes (Deferred by Design)

**1. Codebase index JoinHandle storage in `AgentStack`**
`crates/ava-agent/src/stack/mod.rs`

The codebase indexing `tokio::spawn` drops its `JoinHandle`. If the task panics, the index silently never builds and subsequent `codebase_search` calls return empty results with no user-facing error. Fix requires adding a `RwLock<Option<JoinHandle<()>>>` field to `AgentStack`, plumbing through `Send` bound verification, and adding poll-before-first-use logic in the `codebase_search` tool path. The existing `warn!` on `Err` already handles the most common failure path (non-panic task errors).

**2. Fire-and-forget spawn full error routing in `ava-tui`**
`crates/ava-tui/src/state/agent.rs`

`set_thinking_level`, `cycle_thinking`, and `set_mode` spawn tasks that call `AgentStack` methods returning `()` (not `Result`). Full error surfacing to the TUI via `AppEvent::Error` requires changing those method signatures to return `Result` across `ava-agent` and `ava-tui`. Round 4 added panic logging via `tracing::warn!`; silent non-panic errors require the signature change.

**3. Agent loop integration tests for `run_unified()`**
`crates/ava-agent/src/agent_loop/mod.rs`

`run_unified()` is the 325-line central execution path (LLM calls, tool dispatch, stuck detection, steering queue, cost tracking) with zero integration tests. Adding tests requires a `MockProvider` that returns scripted responses, injected into `AgentLoop` via an `AgentStack` built from `AgentStackConfig`. The test infrastructure exists in `ava-llm` but the wiring to `AgentStack` requires care to avoid requiring a real filesystem and credentials.

Recommended test cases (per analysis):
- Normal single-turn completion → `AgentEvent::Complete`
- Steering queue interruption after tool execution
- Stuck-loop detection triggering
- Tool-error self-correction path
- `max_turns` limit enforcement

### Other Deferred Items

**`AgentStack` RwLock consolidation** (`crates/ava-agent/src/stack/mod.rs`): 9 separate `RwLock` fields with no documented locking order. Latent deadlock risk if acquisition order reverses. Consolidate into a single `Arc<RwLock<AgentRuntimeState>>` or actor-pattern `mpsc` channel for state mutations.

**SIGTERM handling** (`crates/ava-tui/src/headless/single.rs`): Only `SIGINT` (Ctrl+C) is handled. `SIGTERM` — the standard termination signal in Docker/Kubernetes/systemd — is ignored, causing non-graceful shutdown in containerized deployments. Use `tokio::select!` with `signal(SignalKind::terminate())`.

**`AvaError` legacy variant migration**: 10 deprecated string-payload variants still have active call sites throughout the codebase. A codebase-wide migration to structured variants (guided by the deprecation doc comments added in Round 3) would eliminate the dual-personality design and improve retryability detection for transient errors.

**`CodeSearchTool` index rebuild** (`crates/ava-tools/src/core/code_search.rs`): Rebuilds the entire `CodebaseIndex` on every invocation. Should accept a shared `Arc<RwLock<Option<Arc<CodebaseIndex>>>>` from `AgentStack` at construction time (same pattern used elsewhere in the stack).

**`ava-db` message ordering** (`crates/ava-db/src/models/message.rs`): `list_by_session` orders by `created_at` which can produce non-deterministic ordering on timestamp ties. Add a `created_seq INTEGER DEFAULT rowid` column and sort by it.

**Property-based testing**: No `proptest`/`quickcheck`/`bolero` anywhere in the workspace. Priority targets: message queue tier-ordering invariants, token counting invariants, tool parameter validation with arbitrary JSON inputs.

**Snapshot tests**: `insta` is already a dev-dependency in `ava-tui` but has zero snapshot tests. Good targets: `build_system_prompt()` output, key TUI widget rendering via `TestBackend`, `StreamChunk` serialization format.

**Provider integration tests**: `ava-llm/tests/providers.rs` contains only provider creation tests. No fixture-based streaming parser tests, tool call assembly tests, or SSE chunk parsing regression tests. Add `wiremock`/`httpmock` and recorded API response fixtures.

**`rust-version` pin**: Workspace pins `rust-version = "1.75"` (Nov 2023). AFIT (async fn in traits) is stable since 1.75 but `async-trait` is still used everywhere. Bump to `1.80+` and begin removing `async-trait` from crates where AFIT is sufficient.

---

## Metrics

| Metric | Value |
|--------|-------|
| Rust files scanned (R1 analysis) | 361 |
| TypeScript files in scope | 523 |
| Rust crate count | 21 |
| Files modified by audit | 90 |
| Lines added | +3,250 |
| Lines removed | −570 |
| Clippy warnings: before → after | 0 → 0 |
| Test count: before → after | 1,756 → 1,769 |
| Critical issues found (R1) | 20 |
| Critical issues resolved | 17 |
| Critical issues remaining | 3 |
| Warning-level issues found | ~70 |
| Warning-level issues resolved | ~33 |
| Warning-level issues remaining | ~37 |
| Fixes applied total | 37 (R1:13, R2:11, R3:10, R4:3) |
| Fixes deferred | 4 (R1:2, R2:2, R4:1 — architectural changes) |
| Security fixes | 3 critical + 1 warning |
| Performance fixes | 5 (blocking I/O ×3, caching ×2) |
| Error handling fixes | 5 |
| Test additions | ~34 new test functions |
| Named constant extractions | 5 |
| Debug output removals | 3 (eprintln blocks, web server) |
| API correctness fixes | 2 (WasmLoader, HttpTransport) |
