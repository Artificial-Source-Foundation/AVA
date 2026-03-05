# Sprint 35 Performance, Tests, and Docs Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete Sprint 35 by optimizing Epic 4 Rust backend hot paths, expanding test/benchmark depth, and finalizing public docs for extension, validation, and reflection systems.

**Architecture:** Keep optimizations localized to Epic 4 crates (`ava-extensions`, `ava-validator`, `ava-agent`) and their Tauri command adapters. Preserve behavior with test-first changes, then add benchmark harnesses and docs as first-class deliverables.

**Tech Stack:** Rust, Criterion benchmarks, cargo test/clippy/fmt, rustdoc, Tauri command tests.

---

### Task 1: Establish Sprint 35 baseline metrics and benchmark harness

**Files:**
- Modify: `crates/ava-validator/Cargo.toml`
- Modify: `crates/ava-agent/Cargo.toml`
- Create: `crates/ava-validator/benches/validation_hotpaths.rs`
- Create: `crates/ava-agent/benches/reflection_hotpaths.rs`

**Step 1: Add failing benchmark compile gate**

Run: `cargo bench -p ava-validator --no-run`
Expected: FAIL before benchmark target/deps exist.

**Step 2: Add Criterion bench targets and dev-deps**

Add `criterion` benches for:
- syntax validation on small/medium/large source payloads
- retry pipeline execution for fail->fix->pass
- reflection error classification and single fix attempt path

**Step 3: Verify bench compile and capture baseline run**

Run:
- `cargo bench -p ava-validator --no-run`
- `cargo bench -p ava-agent --no-run`

Then run quick sample baseline:
- `cargo bench -p ava-validator --bench validation_hotpaths -- --sample-size 20`
- `cargo bench -p ava-agent --bench reflection_hotpaths -- --sample-size 20`

Expected: PASS with baseline numbers captured in command output.

### Task 2: Optimize `ava-validator` hot paths with behavior-preserving refactor

**Files:**
- Modify: `crates/ava-validator/src/validators.rs`
- Modify: `crates/ava-validator/src/pipeline.rs`
- Modify: `crates/ava-validator/tests/validation_pipeline.rs`

**Step 1: Add failing edge-case tests first**

Add tests for:
- conflict marker detection at line boundaries only
- delimiter mismatch details remain stable
- retry flow preserves final content and attempt counts on all exits

Run: `cargo test -p ava-validator`
Expected: FAIL before refactor.

**Step 2: Implement optimizations**

Implement:
- single-pass line-aware conflict marker checks (avoid per-char prefix scans)
- reduce transient allocations in detail construction
- use `Cow<str>` in retry loop to avoid cloning initial content when no fix needed

**Step 3: Re-run tests + bench**

Run:
- `cargo test -p ava-validator`
- `cargo bench -p ava-validator --bench validation_hotpaths -- --sample-size 20`

Expected: tests pass; benchmarks improve vs baseline.

### Task 3: Optimize `ava-agent` reflection classifier path

**Files:**
- Modify: `crates/ava-agent/src/reflection.rs`
- Modify: `crates/ava-agent/tests/reflection_loop.rs`

**Step 1: Add failing tests first**

Add tests for:
- case-insensitive classification without allocation-dependent behavior
- classifier robustness on long error strings

Run: `cargo test -p ava-agent`
Expected: FAIL before implementation.

**Step 2: Implement optimization**

Replace full-string lowercase allocation with case-insensitive token matching helpers over borrowed data.

**Step 3: Re-run tests + bench**

Run:
- `cargo test -p ava-agent`
- `cargo bench -p ava-agent --bench reflection_hotpaths -- --sample-size 20`

Expected: tests pass; benchmark improves vs baseline.

### Task 4: Expand tests and add property-style robustness checks

**Files:**
- Modify: `crates/ava-validator/tests/validation_pipeline.rs`
- Modify: `crates/ava-agent/tests/reflection_loop.rs`
- Modify: `crates/ava-extensions/tests/extension_manager.rs`

**Step 1: Add failing tests first**

Add broader invariants:
- validators never panic on random-ish mixed delimiter inputs
- reflection analyze function classifies known patterns consistently
- extension hot-reload remains deterministic across repeated registrations

Run targeted tests and confirm new tests fail first where appropriate.

**Step 2: Implement minimal fixes if needed**

Adjust only necessary production code for deterministic behavior.

**Step 3: Re-run crate tests**

Run:
- `cargo test -p ava-extensions -p ava-validator -p ava-agent`

Expected: PASS.

### Task 5: Complete documentation for Sprint 33-35 backend systems

**Files:**
- Modify: `crates/ava-extensions/src/lib.rs`
- Modify: `crates/ava-extensions/src/hook.rs`
- Modify: `crates/ava-extensions/src/manager.rs`
- Modify: `crates/ava-validator/src/lib.rs`
- Modify: `crates/ava-validator/src/pipeline.rs`
- Modify: `crates/ava-validator/src/validators.rs`
- Modify: `crates/ava-agent/src/lib.rs`
- Modify: `crates/ava-agent/src/reflection.rs`
- Create: `docs/development/rust-backend-epic4-architecture.md`

**Step 1: Add public rustdoc comments**

Document all public structs/enums/traits/functions in the three crates.

**Step 2: Add architecture guide**

Write concise design doc covering extension manager, validation pipeline, reflection loop, and Tauri adapter boundaries.

**Step 3: Validate docs build**

Run: `cargo doc --no-deps --workspace`
Expected: PASS.

### Task 6: Sprint 35 verification and final code review

**Files:**
- Optional status update: `docs/planning/sprints/epic-4/sprint-35.md` (if checklist convention is used)

**Step 1: Run full test suite**

Run: `cargo test`
Expected: PASS.

**Step 2: Run formatting and lint gates**

Run:
- `cargo fmt --all -- --check`
- `cargo clippy --workspace --all-targets -- -D warnings`

Expected: PASS (or clearly document pre-existing unrelated blockers).

**Step 3: Capture final benchmark outputs**

Run:
- `cargo bench -p ava-validator --bench validation_hotpaths -- --sample-size 20`
- `cargo bench -p ava-agent --bench reflection_hotpaths -- --sample-size 20`

Expected: measurable improvements vs Task 1 baseline; summarize delta.

**Step 4: Final code review gate**

Call `code-reviewer` for Sprint 35 scope and address any blocking issues.
