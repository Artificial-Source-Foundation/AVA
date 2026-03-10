# Code Hygiene Audit — AVA Rust Crates

**Scope**: All 22 crates under `crates/` (40,816 total lines of Rust)  
**Date**: 2026-03-08

---

## Summary

| Category | Count | Details |
|---|---|---|
| TODO/FIXME markers | **1** | 1 still relevant |
| Dead code allows | **3** | 2 field-level (ava-mcp), 1 crate-level (ava-tui) |
| Unsafe blocks | **9** | 9 justified, 0 questionable |
| Deprecated patterns | **0** | No `extern crate`, `#[macro_use]`, `try!()`, or `Box<dyn Error>` |
| Consistency issues | **7 categories** | See detailed breakdown below |

---

## 1. TODO/FIXME Markers

### `crates/ava-extensions/src/wasm_loader.rs:13` — `TODO: Integrate with wasmtime to support loading WASM extensions.`

- **Status**: Still relevant. The entire `WasmLoader` is a stub that returns `Err(ExtensionError::Unsupported(...))`.
- **Action**: **Keep** — this is a genuine roadmap item. Consider adding a tracking issue reference (e.g., `// TODO(#123): ...`) so it doesn't go stale.

---

## 2. Dead Code

### `crates/ava-mcp/src/transport.rs:240–242` — `#[allow(dead_code)]` on `HttpTransport.base_url` and `HttpTransport.session_id`

- **Assessment**: **Keep (for now), but flag for cleanup.**  
  `HttpTransport` is a partially-implemented stub — its `send()` is a no-op and `receive()` returns an error string `"HTTP transport not yet fully implemented"`. The fields are needed once the transport is complete. This is acceptable as long as the transport is on the roadmap. If it's abandoned, the entire struct should be removed.

### `crates/ava-tui/src/lib.rs:8` — `#![allow(dead_code)]` (crate-level blanket allow)

- **Assessment**: ⚠️ **VIOLATION — Remove or scope down.**  
  A crate-level `#![allow(dead_code)]` suppresses ALL dead-code warnings across the entire `ava-tui` crate. This hides legitimate dead code that should be cleaned up. Per AGENTS.md, `cargo clippy --workspace` is a pre-commit check — this blanket allow defeats that. At minimum, apply `#[allow(dead_code)]` only on specific items that are genuinely needed-but-unused-for-now.

---

## 3. Unsafe Code

### `crates/ava-extensions/src/native_loader.rs:7,18,23,27,34,45` — Native extension FFI loading

- **Justification**: **Fully justified** — this is FFI code loading shared libraries via `libloading`. There is no safe alternative for dynamic library loading.
- **Safe alternative**: No. `libloading::Library::new()`, symbol lookup, and `Box::from_raw()` are inherently unsafe operations.
- **Invariants documented**: **Yes** — excellent `/// # Safety` doc block (lines 10–17) documents ABI compatibility, valid pointer, and lifetime requirements.
- **Note**: `std::mem::forget(library)` on line 37 is intentional and commented — it keeps the shared library resident so the vtable stays valid. This is a well-known pattern.

### `crates/ava-agent/tests/reflection_loop.rs:15–23` — Custom global allocator for allocation counting

- **Justification**: **Justified** — implementing `GlobalAlloc` requires `unsafe impl`. This is a test-only counting allocator that delegates to `System`.
- **Safe alternative**: No. The `GlobalAlloc` trait requires `unsafe` by definition.
- **Invariants documented**: **Yes** — `// SAFETY: Delegates to the system allocator and only increments a counter.` on line 14.

---

## 4. Stale Imports

**None found.** No `#[allow(unused_imports)]` attributes exist anywhere in the codebase. ✅

---

## 5. Deprecated Patterns

| Pattern | Count | Details |
|---|---|---|
| `extern crate` | 0 | ✅ Clean — Rust 2021 edition used throughout |
| `#[macro_use]` | 0 | ✅ Clean — explicit imports used |
| `try!()` macro | 0 | ✅ Clean — `?` operator used throughout |
| `Box<dyn Error>` | 0 | ✅ Clean — `AvaError` and crate-specific errors used |

---

## 6. Consistency Issues

### 6.1 Mixed Error Handling — `Result<T, String>` vs `Result<T>` (AvaError)

**Severity: Medium** — Several crates mix `Result<T, String>` with the project's `AvaError`-based `Result<T>` in the same crate or even the same module.

| Crate | `Result<_, String>` locations | Also uses `AvaError`? |
|---|---|---|
| `ava-config` | `model_catalog.rs` (4 functions: `fetch`, `load_cached`, `save_cache`, `default_cache_path`) | Yes (`credentials.rs`, `lib.rs`, `credential_commands.rs`) |
| `ava-commander` | `review.rs` (3 functions: `collect_diff`, `run_git_command`, `run_review_agent`) | Yes (`lib.rs`) |
| `ava-agent` | `reflection.rs` (`ReflectionAgent` trait) | Yes (everywhere else) |
| `ava-llm` | `provider.rs` trait uses `Result<String>` (AvaError) but individual providers also return `Result<String>` in parsing | Mixed |

**Action**: Migrate `Result<T, String>` to `AvaError` variants (e.g., `AvaError::Config(...)`, `AvaError::Git(...)`) for consistency. The `model_catalog.rs` is the worst offender — 4 public functions use `String` errors while the rest of `ava-config` uses `AvaError`.

### 6.2 Zero `pub(crate)` Usage — Overly Broad Visibility

**Severity: Medium** — The entire codebase has **zero** `pub(crate)` declarations. Every public item is `pub`, meaning internal implementation details are part of the public API of each crate.

Examples of items that should likely be `pub(crate)`:
- `ava-commander/src/review.rs`: `parse_diff_stats`, `run_git_command` — internal helpers
- `ava-llm/src/providers/common/parsing.rs`: 15 `pub fn` helpers — these are implementation details for providers, not for external consumers
- `ava-config/src/model_catalog.rs`: `fallback_catalog()` — internal fallback data

**Action**: Audit all `pub` items and narrow to `pub(crate)` where the item is only used within the crate.

### 6.3 Production `.unwrap()` Calls — Panic Risks

**Severity: High (modals.rs), Low (Regex)** — Production code contains `unwrap()` calls that could panic at runtime.

**Critical — `ava-tui/src/app/modals.rs`** (11 instances):
```rust
let state = self.state.provider_connect.as_mut().unwrap();
```
Lines 457, 462, 468, 476, 485, 492, 499, 544, 551, 561, 568 — all unwrapping `Option<ProviderConnectState>`. If the modal state is `None` due to a logic bug, the TUI crashes.

**Action**: Replace with `if let Some(state) = ...` or extract a helper that returns early.

**Acceptable — Regex::new() in production** (9 instances across 3 crates):
- `ava-commander/src/review.rs:153,265`
- `ava-context/src/strategies/relevance.rs:29`
- `ava-codebase/src/indexer.rs:102,116,117,134,135`

These unwrap compile-time constant regex patterns. Compile-time-constant `Regex::new()` panics are acceptable (the regex is either valid or not — it won't change at runtime). However, best practice would be to use `lazy_static!` or `std::sync::LazyLock` to compile them once. The tools crate already handles user-supplied regex correctly with `.map_err()` (`ava-tools/src/edit/strategies/advanced.rs:62`).

**Production `.expect()` calls** (2 instances):
- `ava-tui/src/state/agent.rs:107`: `self.stack.as_ref().expect("AgentStack not initialised")` — will panic if called before init
- `ava-tui/src/app/mod.rs:520`: `SessionState::new(db_path).expect("SessionState")` — but this is in `test_new()`, acceptable

### 6.4 Files Exceeding 300-Line Limit

**Severity: Medium** — AGENTS.md specifies "Max 300 lines per file". **40 Rust files** exceed this limit. The worst offenders:

| File | Lines | Over by |
|---|---|---|
| `ava-config/src/model_catalog.rs` | 1,075 | 3.6× |
| `ava-tui/src/app/modals.rs` | 713 | 2.4× |
| `ava-agent/src/agent_loop/mod.rs` | 659 | 2.2× |
| `ava-commander/src/workflow.rs` | 652 | 2.2× |
| `ava-commander/src/review.rs` | 637 | 2.1× |
| `ava-agent/src/stack.rs` | 625 | 2.1× |
| `ava-tui/src/app/mod.rs` | 563 | 1.9× |
| `ava-permissions/src/inspector.rs` | 563 | 1.9× |

**Note**: Many of these include `#[cfg(test)]` modules. The 300-line rule may be intended for non-test code, but even excluding tests, `model_catalog.rs` and `modals.rs` are well over the limit.

### 6.5 Incomplete Transport Stubs Left in Production

**Severity: Low** — `HttpTransport` in `ava-mcp/src/transport.rs` and `WasmLoader` in `ava-extensions/src/wasm_loader.rs` are stubs that return errors. They're properly guarded (return `Err`), but they add unused code to the binary.

**Action**: Consider feature-gating these behind cargo features (e.g., `#[cfg(feature = "http-transport")]`) so they don't ship as dead code.

### 6.6 Crate-Specific Error Types vs AvaError

**Severity: Low** — The project uses 5 distinct `Result` types across crates:

| Crate | Error Type |
|---|---|
| `ava-types` | `AvaError` (canonical) |
| `ava-codebase` | `CodebaseError` |
| `ava-context` | `ContextError` |
| `ava-lsp` | `LspError` |
| `ava-sandbox` | `SandboxError` |

This is **generally fine** — crate-local error types are idiomatic Rust. However, the **mix of `Result<T, String>` alongside these typed errors** (see 6.1) is the real problem. The typed errors should all impl `From<...>` for `AvaError` where cross-crate boundaries are crossed.

### 6.7 `std::mem::forget` Without Feature Gate

**Severity: Low** — `ava-extensions/src/native_loader.rs:37` uses `std::mem::forget(library)` to keep a loaded shared library alive. While documented and necessary, this is a controlled memory leak. Consider wrapping the library handle in a struct that implements `Drop` to handle unloading, or at minimum adding a `// SAFETY:` comment on the `forget` line itself (currently the safety is documented on the function, not the specific `forget` call).

---

## AGENTS.md Compliance Summary

| Rule | Status |
|---|---|
| Rust-first for new CLI/agent features | ✅ All crate code is Rust |
| No `any` in TypeScript (N/A for crates) | N/A |
| Max 300 lines per file | ❌ 40 files exceed limit |
| kebab-case filenames | ✅ All `.rs` files use snake_case (correct for Rust) |
| camelCase functions | ✅ snake_case used (correct for Rust) |
| PascalCase types | ✅ |
| No secrets/credentials | ✅ None found |
| Pre-commit checks should pass | ⚠️ `#![allow(dead_code)]` on ava-tui defeats `cargo clippy` dead-code checks |

---

## Priority Actions

1. **🔴 High**: Remove 11 `unwrap()` calls in `ava-tui/src/app/modals.rs` — these will crash the TUI on state bugs
2. **🟡 Medium**: Remove `#![allow(dead_code)]` from `ava-tui/src/lib.rs` and fix any resulting warnings
3. **🟡 Medium**: Migrate `Result<T, String>` to `AvaError` in `ava-config/src/model_catalog.rs` (4 functions) and `ava-commander/src/review.rs` (3 functions)
4. **🟡 Medium**: Introduce `pub(crate)` for internal helpers (start with `ava-llm/src/providers/common/parsing.rs` and `ava-commander/src/review.rs`)
5. **🔵 Low**: Split files over 600 lines (especially `model_catalog.rs` at 1,075 lines)
6. **🔵 Low**: Use `LazyLock`/`once_cell` for compile-time constant regex patterns instead of `Regex::new().unwrap()` on every call
7. **🔵 Low**: Feature-gate `HttpTransport` and `WasmLoader` stubs
