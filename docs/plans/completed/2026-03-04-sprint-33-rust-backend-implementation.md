# Sprint 33 Rust Backend Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement Sprint 33 by adding Git tools, browser automation, memory storage/search, and permission evaluation as new Rust crates integrated through Tauri command adapters.

**Architecture:** Build three new crates (`ava-tools`, `ava-memory`, `ava-permissions`) for core backend logic, then expose thin command wrappers under `src-tauri/src/commands`. Keep request/response types strongly typed in Rust and only deserialize/serialize at Tauri boundaries.

**Tech Stack:** Rust workspace crates, Tokio async process/file I/O, SQLite/FTS5 (rusqlite), serde, thiserror, Tauri command invocations.

---

### Task 1: Scaffold Sprint 33 crates and workspace wiring

**Files:**
- Modify: `Cargo.toml`
- Modify: `src-tauri/Cargo.toml`
- Create: `crates/ava-tools/Cargo.toml`
- Create: `crates/ava-tools/src/lib.rs`
- Create: `crates/ava-memory/Cargo.toml`
- Create: `crates/ava-memory/src/lib.rs`
- Create: `crates/ava-permissions/Cargo.toml`
- Create: `crates/ava-permissions/src/lib.rs`

**Step 1: Write the failing workspace test (compile gate)**

Run: `cargo test -p ava-tools --no-run`
Expected: FAIL with package not found.

**Step 2: Add minimal crate manifests and lib stubs**

Create `ava-tools`, `ava-memory`, and `ava-permissions` with minimal `pub fn healthcheck() -> bool` placeholders and matching unit tests.

**Step 3: Wire crates into workspace/dependencies**

Add new crates to root `Cargo.toml` workspace members. Add path dependencies in `src-tauri/Cargo.toml`.

**Step 4: Verify compile for new crates**

Run: `cargo test -p ava-tools -p ava-memory -p ava-permissions --no-run`
Expected: PASS.

**Step 5: Commit**

```bash
git add Cargo.toml src-tauri/Cargo.toml crates/ava-tools crates/ava-memory crates/ava-permissions
git commit -m "feat(rust): scaffold sprint 33 backend crates"
```

### Task 2: Implement Git tool core engine in `ava-tools`

**Files:**
- Modify: `crates/ava-tools/src/lib.rs`
- Create: `crates/ava-tools/src/git/mod.rs`
- Create: `crates/ava-tools/src/git/tests.rs`

**Step 1: Write failing tests for action routing and process handling**

Cover:
- Action deserialization (`commit`, `branch`, `checkout`, `status`, `diff`, `log`, `pr`)
- Unsupported action error
- Non-zero subprocess exit propagates structured error

Run: `cargo test -p ava-tools git::tests -- --nocapture`
Expected: FAIL due missing module/handlers.

**Step 2: Implement minimal API and types**

Add:
- `GitTool`
- `GitAction` enum
- `GitTool::run(action)`
- internal async executor for `git`/`gh` commands

**Step 3: Implement each Sprint 33 action**

- `commit(message)`
- `branch(name)`
- `checkout(branch)`
- `status()`
- `diff()`
- `log(limit)`
- `pr(title, body)` via `gh pr create`

**Step 4: Re-run tests**

Run: `cargo test -p ava-tools git::tests -- --nocapture`
Expected: PASS.

**Step 5: Commit**

```bash
git add crates/ava-tools/src/lib.rs crates/ava-tools/src/git
git commit -m "feat(rust): add git tool actions for sprint 33"
```

### Task 3: Implement Browser tool core engine in `ava-tools`

**Files:**
- Modify: `crates/ava-tools/src/lib.rs`
- Create: `crates/ava-tools/src/browser.rs`
- Create: `crates/ava-tools/src/browser_tests.rs`

**Step 1: Write failing tests for browser action dispatch**

Cover:
- `navigate`, `click`, `type`, `extract`, `screenshot`
- Invalid action error
- Driver connectivity failure handling

Run: `cargo test -p ava-tools browser -- --nocapture`
Expected: FAIL due missing module.

**Step 2: Implement minimal browser API**

Add:
- `BrowserTool`
- action enum and dispatcher
- WebDriver client wrapper interface (injectable for tests)

**Step 3: Implement actions and response normalization**

- URL navigation
- CSS selector click/type
- body/a11y extraction response
- screenshot capture response path/bytes metadata

**Step 4: Re-run tests**

Run: `cargo test -p ava-tools browser -- --nocapture`
Expected: PASS.

**Step 5: Commit**

```bash
git add crates/ava-tools/src/lib.rs crates/ava-tools/src/browser.rs crates/ava-tools/src/browser_tests.rs
git commit -m "feat(rust): add browser automation tool engine"
```

### Task 4: Implement persistent memory system in `ava-memory`

**Files:**
- Modify: `crates/ava-memory/src/lib.rs`
- Create: `crates/ava-memory/src/schema.rs`
- Create: `crates/ava-memory/src/tests.rs`

**Step 1: Write failing tests for remember/recall/search/recent**

Use temp DB path and verify:
- insert + recall by key
- full-text search over values
- recent ordering by created_at descending
- persistence across new `MemorySystem` instance

Run: `cargo test -p ava-memory -- --nocapture`
Expected: FAIL with missing implementation/schema.

**Step 2: Implement schema and initialization**

Create base table and FTS5 virtual table, plus triggers or synchronized writes.

**Step 3: Implement memory APIs**

- `remember(key, value)`
- `recall(key) -> Option<String>`
- `search(query) -> Vec<Memory>`
- `get_recent(limit)`

**Step 4: Re-run tests**

Run: `cargo test -p ava-memory -- --nocapture`
Expected: PASS.

**Step 5: Commit**

```bash
git add crates/ava-memory/src/lib.rs crates/ava-memory/src/schema.rs crates/ava-memory/src/tests.rs
git commit -m "feat(rust): implement persistent memory system with fts"
```

### Task 5: Implement permission rules and dynamic escalation in `ava-permissions`

**Files:**
- Modify: `crates/ava-permissions/src/lib.rs`
- Create: `crates/ava-permissions/src/tests.rs`

**Step 1: Write failing tests for rule precedence and escalation**

Cover:
- allow/deny/ask behavior
- ordered rule matching
- glob/regex/path pattern checks
- escalation for out-of-workspace paths, destructive commands, networked actions
- fail-closed behavior on invalid checks

Run: `cargo test -p ava-permissions -- --nocapture`
Expected: FAIL due missing logic.

**Step 2: Implement rule model and evaluation**

Add:
- `Rule`, `Pattern`, `Action`
- `PermissionSystem::load`
- `PermissionSystem::evaluate`

**Step 3: Implement dynamic checks**

Add `dynamic_check(tool, args)` and merge with static evaluation to produce final decision.

**Step 4: Re-run tests**

Run: `cargo test -p ava-permissions -- --nocapture`
Expected: PASS.

**Step 5: Commit**

```bash
git add crates/ava-permissions/src/lib.rs crates/ava-permissions/src/tests.rs
git commit -m "feat(rust): add permission policy engine with escalation"
```

### Task 6: Add Tauri command adapters for Sprint 33 systems

**Files:**
- Modify: `src-tauri/src/commands/mod.rs`
- Modify: `src-tauri/src/lib.rs`
- Create: `src-tauri/src/commands/tool_git.rs`
- Create: `src-tauri/src/commands/tool_browser.rs`
- Create: `src-tauri/src/commands/memory.rs`
- Create: `src-tauri/src/commands/permissions.rs`

**Step 1: Write failing command-level tests**

Add tests that verify JSON command inputs map to crate APIs and command outputs are serializable and stable.

Run: `cargo test --manifest-path src-tauri/Cargo.toml commands:: -- --nocapture`
Expected: FAIL with missing commands/exports.

**Step 2: Implement thin command wrappers**

- `execute_git_tool` command
- `execute_browser_tool` command
- `memory_remember`, `memory_recall`, `memory_search`, `memory_recent`
- `evaluate_permission` command

**Step 3: Register commands in module exports and invoke handler**

Update `commands/mod.rs` exports and `src-tauri/src/lib.rs` `generate_handler!` list.

**Step 4: Re-run command tests**

Run: `cargo test --manifest-path src-tauri/Cargo.toml commands:: -- --nocapture`
Expected: PASS.

**Step 5: Commit**

```bash
git add src-tauri/src/commands/mod.rs src-tauri/src/lib.rs src-tauri/src/commands/tool_git.rs src-tauri/src/commands/tool_browser.rs src-tauri/src/commands/memory.rs src-tauri/src/commands/permissions.rs
git commit -m "feat(rust): expose sprint 33 backend systems via tauri commands"
```

### Task 7: Sprint 33 verification and review handoff

**Files:**
- Modify: `docs/planning/sprints/epic-4/sprint-33.md` (optional status notes only if project convention expects checklists)

**Step 1: Run full sprint test suite**

Run: `cargo test`
Expected: PASS.

**Step 2: Run lint/type hygiene for Rust workspace**

Run: `cargo fmt --all -- --check && cargo clippy --workspace --all-targets -- -D warnings`
Expected: PASS.

**Step 3: Call code-reviewer subagent**

Request review focused on:
- API correctness for git/browser/memory/permissions
- safety/failure modes
- test quality and sprint acceptance coverage

**Step 4: Address review findings and re-run tests**

Run: `cargo test`
Expected: PASS after fixes.

**Step 5: Commit final sprint fixes**

```bash
git add -A
git commit -m "chore(rust): finalize sprint 33 backend implementation"
```
