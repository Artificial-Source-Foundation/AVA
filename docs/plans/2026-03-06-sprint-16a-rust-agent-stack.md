# Sprint 16a Rust Agent Stack Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete all missing Rust runtime pieces needed for Sprint 16b TUI (core tools, provider-backed commander, concurrency controls, sandbox execution, unified stack, and e2e validation).

**Architecture:** Implement additive modules in each crate while preserving stable APIs where possible. Use constructor-injected provider/tool/context for `AgentLoop` wiring and add orchestration, sandbox, and integration layers around existing primitives. Verify after each feature with full workspace test + clippy gates.

**Tech Stack:** Rust, Tokio, async-trait, serde_json, ava-* workspace crates, glob, grep-regex/searcher/matcher.

---

### Task 1: Core Tool Implementations (`ava-tools`)

**Files:**
- Create: `crates/ava-tools/src/core/mod.rs`
- Create: `crates/ava-tools/src/core/read.rs`
- Create: `crates/ava-tools/src/core/write.rs`
- Create: `crates/ava-tools/src/core/edit.rs`
- Create: `crates/ava-tools/src/core/bash.rs`
- Create: `crates/ava-tools/src/core/glob.rs`
- Create: `crates/ava-tools/src/core/grep.rs`
- Modify: `crates/ava-tools/src/lib.rs`
- Modify: `crates/ava-tools/Cargo.toml`
- Create: `crates/ava-tools/tests/core_tools_test.rs`

**Steps:**
1. Write failing tests for each core tool behavior in `core_tools_test.rs`.
2. Run `cargo test -p ava-tools --test core_tools_test` and confirm failures.
3. Implement tool modules and registration helper.
4. Run `cargo test -p ava-tools`.
5. Run `cargo test --workspace && cargo clippy --workspace -- -D warnings`.

### Task 2: Provider-backed Commander (`ava-commander`)

**Files:**
- Modify: `crates/ava-commander/src/lib.rs`
- Modify: `crates/ava-commander/Cargo.toml`
- Modify: `crates/ava-commander/tests/commander.rs`

**Steps:**
1. Write/update failing tests to use `MockProvider` and verify domain/provider/model wiring.
2. Run `cargo test -p ava-commander` and confirm failures.
3. Replace `NullProvider` with `CommanderConfig` provider injection and overrides.
4. Update worker creation and coordination execution paths to use real providers.
5. Run `cargo test -p ava-commander`.
6. Run `cargo test --workspace && cargo clippy --workspace -- -D warnings`.

### Task 3: Commander Concurrency Controls

**Files:**
- Create: `crates/ava-commander/src/events.rs`
- Modify: `crates/ava-commander/src/lib.rs`
- Modify: `crates/ava-commander/Cargo.toml`
- Modify: `crates/ava-commander/tests/commander.rs`

**Steps:**
1. Add failing tests for cancellation, timeout, failure isolation, and event emission order.
2. Run `cargo test -p ava-commander` and confirm failures.
3. Implement `CommanderEvent` and `coordinate` cancellation/timeout/event flow.
4. Ensure partial failures still produce merged successful session.
5. Run `cargo test -p ava-commander`.
6. Run `cargo test --workspace && cargo clippy --workspace -- -D warnings`.

### Task 4: Sandbox Plan Execution and Bash Integration

**Files:**
- Create: `crates/ava-sandbox/src/executor.rs`
- Modify: `crates/ava-sandbox/src/lib.rs`
- Modify: `crates/ava-sandbox/src/linux.rs`
- Modify: `crates/ava-sandbox/src/macos.rs`
- Modify: `crates/ava-sandbox/src/error.rs`
- Modify: `crates/ava-sandbox/Cargo.toml`
- Modify: `crates/ava-tools/src/core/bash.rs`

**Steps:**
1. Add failing tests for plan builder `working_dir/env` and executor behavior.
2. Run `cargo test -p ava-sandbox` and confirm failures.
3. Implement executor and new sandbox error variants.
4. Route install-class commands to sandbox path in `BashTool`.
5. Run `cargo test -p ava-sandbox && cargo test -p ava-tools`.
6. Run `cargo test --workspace && cargo clippy --workspace -- -D warnings`.

### Task 5: Unified `AgentStack` Entrypoint (`ava-agent`)

**Files:**
- Create: `crates/ava-agent/src/stack.rs`
- Modify: `crates/ava-agent/src/lib.rs`
- Modify: `crates/ava-agent/Cargo.toml`
- Create: `crates/ava-agent/tests/stack_test.rs`

**Steps:**
1. Add failing stack initialization and run-path tests.
2. Run `cargo test -p ava-agent --test stack_test` and confirm failures.
3. Implement `AgentStack` and config/defaults with tool registration and provider routing.
4. Add cancellation-aware run flow.
5. Run `cargo test -p ava-agent`.
6. Run `cargo test --workspace && cargo clippy --workspace -- -D warnings`.

### Task 6: End-to-End Integration Tests

**Files:**
- Create: `crates/ava-agent/tests/e2e_test.rs`

**Steps:**
1. Add tests for full tool-call run, bash path, cancellation, and commander parallel coordination.
2. Run `cargo test -p ava-agent --test e2e_test` and iterate until green.
3. Run `cargo test --workspace`.
4. Run `cargo clippy --workspace -- -D warnings`.

### Final Verification + Commit

**Steps:**
1. Run `cargo test --workspace`.
2. Run `cargo clippy --workspace -- -D warnings`.
3. Run `cargo test -p ava-tools -- core`.
4. Run `cargo test -p ava-commander`.
5. Run `cargo test -p ava-agent -- e2e`.
6. Commit:

```bash
git add crates/ava-tools crates/ava-commander crates/ava-sandbox crates/ava-agent docs/plans
git commit -m "feat(sprint-16a): complete Rust agent stack"
```
