# Unwrap/Expect Audit — AVA Rust Crates

**Audit date:** 2026-03-08
**Auditor:** code-reviewer-subagent (claude-opus-4.6)
**Scope:** All `.rs` files in `crates/` (excluding test code)

---

## Summary
- **Total production unwraps/expects: 23** (out of 631 total across codebase; 608 are in test code)
- **Critical: 1** | **High: 2** | **Medium: 11** | **Low: 9**

---

## Critical Findings

### `crates/ava-tui/src/state/agent.rs:107` — `AgentStack` expect on user-facing path

- **Code**: `self.stack.as_ref().expect("AgentStack not initialised")`
- **Context**: Private `stack()` method called by `switch_model()`, `mcp_server_info()`, `reload_mcp()`, `reload_tools()`, `list_tools_with_source()` — all user-triggered TUI actions.
- **Risk**: If `AgentStack` initialization fails or the user triggers a model switch/MCP reload before the stack is ready, **the entire TUI process panics**. The callers already return `Result<_, String>`, so propagation is straightforward. The `start()` method on line 117 already does a safe `as_ref().map(Arc::clone)` check — inconsistent.
- **Fix**:
  ```rust
  fn stack(&self) -> Result<&Arc<AgentStack>, String> {
      self.stack.as_ref().ok_or_else(|| "AgentStack not initialised".to_string())
  }
  ```
  Then propagate `?` in callers (they already return `Result`).

---

## High Findings

### `crates/ava-llm/src/circuit_breaker.rs:40,75` — Mutex `.lock().unwrap()` in LLM request path

- **Code**:
  - Line 40: `let last = self.last_failure.lock().unwrap();` (in `allow_request`)
  - Line 75: `*self.last_failure.lock().unwrap() = Some(Instant::now());` (in `record_failure`)
- **Context**: `CircuitBreaker` guards every LLM API call. `allow_request()` is checked before each provider call; `record_failure()` is called on every failed response. These are hot-path agent-loop code.
- **Risk**: A poisoned mutex (from a prior panic in another thread holding the lock) will crash the agent loop. While mutex poisoning is rare, this is high-frequency code in a multi-threaded async context.
- **Fix**: Use `.lock().unwrap_or_else(|e| e.into_inner())` to recover from poisoning (the `Instant` data is benign — a stale timestamp is better than a panic). Or switch to `parking_lot::Mutex` which doesn't poison.

### `crates/ava-llm/src/pool.rs:73` — `reqwest::Client::builder().build().expect()`

- **Code**: `.build().expect("failed to build reqwest client")`
- **Context**: `HttpPool::get_client()` is called on every LLM API request to obtain or create an HTTP client. This is core agent-loop infrastructure.
- **Risk**: `reqwest::Client::builder().build()` can fail if the TLS backend fails to initialize (e.g., broken OpenSSL on the system, misconfigured rustls). While unlikely on most systems, this panics the entire process during normal operation rather than returning an error. The function signature `pub async fn get_client(&self, base_url: &str) -> Arc<reqwest::Client>` doesn't return `Result`, so callers can't handle it.
- **Fix**: Change return type to `Result<Arc<reqwest::Client>, reqwest::Error>` and propagate with `?`. Callers in providers should map this to their existing error types.

---

## Medium Findings

### `crates/ava-tui/src/app/modals.rs:457–568` — 11× `provider_connect.as_mut().unwrap()` in TUI key handler

- **Code**: `let state = self.state.provider_connect.as_mut().unwrap();` (11 occurrences at lines 457, 462, 468, 476, 485, 492, 499, 544, 551, 561, 568)
- **Context**: Inside `handle_provider_connect_key()`, the top of the function (line 328) does a safe match on `self.state.provider_connect` and early-returns if `None`. However, the subsequent match arms for sub-screens (`AuthMethodChoice`, `Configure`, `OAuthBrowser`, `DeviceCode`) re-access `.provider_connect` with `.unwrap()` instead of re-using the already-borrowed reference.
- **Risk**: There is a structural invariant — line 328–333 guards the `None` case and returns early. These unwraps *should* be safe because we only reach these match arms if `provider_connect` is `Some`. However, if a future refactor introduces a code path that sets `provider_connect = None` mid-function (line 533 already does this in the `Enter` handler!), the subsequent unwraps in the same match block could panic. This is fragile.
- **Risk Level**: MEDIUM — currently safe due to structural invariant, but line 533 setting `provider_connect = None` within the same function is a code smell that makes future bugs likely.
- **Fix**: Restructure to use a single `let Some(state) = self.state.provider_connect.as_mut() else { return false; }` pattern at the top, then use `state` throughout. Alternatively, use `if let Some(state) = ...` for each sub-arm. The borrow checker issue can be resolved by extracting sub-screen handlers into separate methods that take `&mut ProviderConnectState`.

---

## Low Findings (Justified)

### `crates/ava-commander/src/review.rs:153` — `Regex::new(...).unwrap()` (static pattern)
- **Code**: `let re = Regex::new(r"^\s*(.+?)\s+\|\s+(\d+)\s*(\+*)(-*)").unwrap();`
- **Context**: Diff stat parser. Hardcoded regex literal.
- **Risk**: LOW — compile-time constant pattern, cannot fail at runtime.
- **Fix**: Use `lazy_static!` or `std::sync::LazyLock` to compile once. Also avoids re-compilation per call.

### `crates/ava-commander/src/review.rs:265` — `Regex::new(...).unwrap()` (static pattern)
- **Code**: `let issue_re = Regex::new(r"###\s+\[(\w+)\]\s+([^:\s]+)?:?(\d+)?\s*-?\s*(.+)").unwrap();`
- **Context**: Issue parser. Hardcoded regex literal.
- **Risk**: LOW — same as above.
- **Fix**: `LazyLock` / `lazy_static!`.

### `crates/ava-codebase/src/indexer.rs:102` — `Regex::new(...).unwrap()` (static pattern)
- **Code**: `let re = Regex::new(r"use\s+(\w+(?:::\w+)*)").unwrap();`
- **Context**: Rust import parser. Hardcoded regex literal.
- **Risk**: LOW.
- **Fix**: `LazyLock`.

### `crates/ava-codebase/src/indexer.rs:116–117` — `Regex::new(...).unwrap()` ×2 (static patterns)
- **Code**: JS import/require regex patterns.
- **Risk**: LOW.
- **Fix**: `LazyLock`.

### `crates/ava-codebase/src/indexer.rs:134–135` — `Regex::new(...).unwrap()` ×2 (static patterns)
- **Code**: Python import regex patterns.
- **Risk**: LOW.
- **Fix**: `LazyLock`.

### `crates/ava-context/src/strategies/relevance.rs:29` — `Regex::new(...).unwrap()` (static pattern)
- **Code**: `let re = Regex::new(r"[\w./\-]+\.\w{1,6}").unwrap();`
- **Context**: Path extraction from message content for relevance scoring.
- **Risk**: LOW — static pattern. However, this is called per-message during context compaction, so repeated compilation is also a performance concern.
- **Fix**: `LazyLock`.

### `crates/ava-tui/src/app/mod.rs:520` — `SessionState::new().expect()` in test helper
- **Code**: `let session = SessionState::new(db_path).expect("SessionState");`
- **Context**: Inside `#[doc(hidden)] pub fn test_new()` — only called from test code.
- **Risk**: LOW — test-only code path, `#[doc(hidden)]`, named `test_new`.
- **Fix**: Acceptable as-is. Could return `Result` for stricter test scaffolding.

---

## AGENTS.md Compliance Notes

The codebase is **in good shape** for a project of this size — only 23 production unwraps out of 631 total, with 608 properly confined to test code. Key violations against AGENTS.md principles:

1. **The Critical finding** (`agent.rs:107`) directly contradicts the agent runtime robustness expected by the architecture. User actions should not panic the TUI.

2. **The High findings** (circuit breaker mutex, HTTP pool) are in the LLM provider hot path described in the data flow section (`model response` → `parse + execute tools`). A panic here kills the agent loop.

3. **The 7 static `Regex::new` unwraps** are technically safe but should use `LazyLock` for idiomatic Rust and performance (avoids re-compilation on each function call).

4. **The 11 modals unwraps** are structurally safe today but fragile. The existing `provider_connect = None` on line 533 within the same function is a latent footgun.

### Recommended Priority
1. Fix `agent.rs:107` — one-line change, high safety impact
2. Fix `pool.rs:73` — change return type to `Result`
3. Fix `circuit_breaker.rs:40,75` — use `unwrap_or_else(|e| e.into_inner())`
4. Refactor `modals.rs` unwraps — extract sub-screen handlers
5. Convert `Regex::new` calls to `LazyLock` — performance + hygiene
