# Sprint 34 Extensions, Validation, and Reflection Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement production-grade extension loading (native + WASM), edit/tool validation with bounded retry, and reflection-loop error recovery for AVA Rust backend.

**Architecture:** Introduce three focused crates: `ava-extensions`, `ava-validator`, and `ava-agent`. Keep crate logic framework-agnostic and expose thin Tauri command adapters only for boundary integration. Compose validator + reflection via traits to avoid tight coupling.

**Tech Stack:** Rust workspace crates, `libloading`, `wasmtime`, `async-trait`, `serde`, `thiserror`, `tokio`, tree-sitter parsers, Tauri command adapters.

---

### Task 1: Scaffold Sprint 34 crates and workspace wiring

**Files:**
- Modify: `Cargo.toml`
- Modify: `src-tauri/Cargo.toml`
- Create: `crates/ava-extensions/Cargo.toml`
- Create: `crates/ava-extensions/src/lib.rs`
- Create: `crates/ava-validator/Cargo.toml`
- Create: `crates/ava-validator/src/lib.rs`
- Create: `crates/ava-agent/Cargo.toml`
- Create: `crates/ava-agent/src/lib.rs`

**Step 1: Write failing compile gate**

Run: `cargo test -p ava-extensions --no-run`
Expected: FAIL (`package ID specification ... did not match any packages`).

**Step 2: Create minimal crates with healthcheck tests**

Create each crate with:
- `pub fn healthcheck() -> bool { true }`
- one unit test asserting `true`.

**Step 3: Wire crates into workspace and src-tauri deps**

Add all three crates to root workspace members and add path deps in `src-tauri/Cargo.toml`.

**Step 4: Verify compile gate passes**

Run: `cargo test -p ava-extensions -p ava-validator -p ava-agent --no-run`
Expected: PASS.

### Task 2: Implement extension system core in `ava-extensions`

**Files:**
- Modify: `crates/ava-extensions/src/lib.rs`
- Create: `crates/ava-extensions/src/hook.rs`
- Create: `crates/ava-extensions/src/manager.rs`
- Create: `crates/ava-extensions/tests/extension_manager.rs`

**Step 1: Write failing tests first**

Add tests for:
- native registration path (`register_native`) and tool/hook/validator registration collection
- wasm registration path metadata (`register_wasm_module`)
- hook invocation routing by `HookPoint`
- hot reload replacing prior extension entry

Run: `cargo test -p ava-extensions`
Expected: FAIL due missing manager/hook implementation.

**Step 2: Implement minimal production types**

Add:
- `Extension` trait
- `ExtensionManager`
- `HookRegistry`, `HookPoint`, `HookContext`, `Hook`
- `ExtensionDescriptor` for loaded native/wasm metadata

**Step 3: Implement loading APIs**

Implement:
- `register_native(...)`
- `register_wasm_module(...)`
- `register_all(...)`
- `hot_reload(...)`

Use trait-bound/native descriptor design so runtime loading can be validated without requiring real shared objects in tests.

**Step 4: Re-run tests**

Run: `cargo test -p ava-extensions`
Expected: PASS.

### Task 3: Implement validation pipeline in `ava-validator`

**Files:**
- Modify: `crates/ava-validator/src/lib.rs`
- Create: `crates/ava-validator/src/pipeline.rs`
- Create: `crates/ava-validator/src/validators.rs`
- Create: `crates/ava-validator/tests/validation_pipeline.rs`

**Step 1: Write failing tests first**

Add tests for:
- syntax validator pass/fail behavior
- compilation validator command pass/fail behavior
- pipeline stops at first invalid validator
- retry orchestration succeeds after fixer-generated correction
- retry cap reached returns failure context

Run: `cargo test -p ava-validator`
Expected: FAIL due missing pipeline/validator implementation.

**Step 2: Implement validator model and results**

Add:
- `ValidationResult` (`valid`, optional `error`, optional `details`)
- `Validator` async trait
- `ValidationPipeline` with ordered validators.

**Step 3: Implement concrete validators**

Add:
- `SyntaxValidator` for language-aware parse checks (Rust/TS minimally)
- `CompilationValidator` command-based checks for file language.

**Step 4: Implement retry flow**

Add `validate_with_retry` with max-attempt bound (default 3), using `FixGenerator` trait to produce corrected content.

**Step 5: Re-run tests**

Run: `cargo test -p ava-validator`
Expected: PASS.

### Task 4: Implement reflection loop in `ava-agent`

**Files:**
- Modify: `crates/ava-agent/src/lib.rs`
- Create: `crates/ava-agent/src/reflection.rs`
- Create: `crates/ava-agent/tests/reflection_loop.rs`

**Step 1: Write failing tests first**

Add tests for:
- classifier detects syntax/import/type/command errors
- reflection triggers one fix attempt when result has error
- reflection skips when no error
- reflection returns original result when fix generation fails
- reflection does not retry more than once per call

Run: `cargo test -p ava-agent`
Expected: FAIL due missing reflection implementation.

**Step 2: Implement reflection abstractions**

Add:
- `ToolResult` model for reflection context
- `ReflectionAgent` trait (`generate_fix`)
- `ToolExecutor` trait (`execute_tool`)
- `ErrorKind` classifier enum.

**Step 3: Implement `ReflectionLoop`**

Implement:
- `analyze_error(message) -> ErrorKind`
- `reflect_and_fix(result, agent, executor)` with single-attempt policy.

**Step 4: Re-run tests**

Run: `cargo test -p ava-agent`
Expected: PASS.

### Task 5: Integrate Sprint 34 crates into Tauri command layer

**Files:**
- Modify: `src-tauri/src/commands/mod.rs`
- Modify: `src-tauri/src/lib.rs`
- Create: `src-tauri/src/commands/extensions.rs`
- Create: `src-tauri/src/commands/validation.rs`
- Create: `src-tauri/src/commands/reflection.rs`

**Step 1: Write failing command tests first**

Add tests for:
- extension manager command DTO mapping + serializable output
- validation command output shape (valid/error/details)
- reflection command behavior mapping

Run: `cargo test --manifest-path src-tauri/Cargo.toml commands:: -- --nocapture`
Expected: FAIL before adapters exist.

**Step 2: Implement thin adapters**

Add commands:
- `extensions_register_native`
- `extensions_register_wasm`
- `validation_validate_edit`
- `validation_validate_with_retry`
- `reflection_reflect_and_fix`

Adapters should map JSON DTOs to crate APIs and return stable serializable response DTOs.

**Step 3: Register commands**

Update `commands/mod.rs` exports and `tauri::generate_handler!` in `src-tauri/src/lib.rs`.

**Step 4: Re-run command tests**

Run: `cargo test --manifest-path src-tauri/Cargo.toml commands:: -- --nocapture`
Expected: PASS.

### Task 6: Sprint 34 verification and review handoff

**Files:**
- Optional status update: `docs/planning/sprints/epic-4/sprint-34.md` (only if project convention expects manual checklist updates)

**Step 1: Run sprint crate tests**

Run: `cargo test -p ava-extensions -p ava-validator -p ava-agent`
Expected: PASS.

**Step 2: Run full verification**

Run: `cargo test`
Expected: PASS (or explicitly document unrelated pre-existing blockers).

**Step 3: Run formatting/lints**

Run: `cargo fmt --all -- --check && cargo clippy --workspace --all-targets -- -D warnings`
Expected: PASS (or document pre-existing blockers outside Sprint 34 scope).

**Step 4: Run code-reviewer**

Request final Sprint 34 code-reviewer pass covering:
- extension manager correctness
- validator/retry behavior
- reflection safety and bounded retries
- adapter contract stability

**Step 5: Address review findings and re-verify**

Run relevant test commands after fixes and ensure green.
