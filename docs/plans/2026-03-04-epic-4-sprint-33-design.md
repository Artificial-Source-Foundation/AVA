# Epic 4 Sprint 33 Design (Rust Backend)

**Goal:** Deliver Sprint 33 by implementing Git tools, browser automation, memory, and permission systems as first-class Rust crates, then expose them through thin Tauri command adapters.

## Scope

Sprint 33 includes four stories:

1. Git tools (commit, branch, checkout, status, diff, log, PR)
2. Browser automation tool (navigate/click/type/extract/screenshot)
3. Memory system (remember/recall/search/recent with persistence)
4. Permission system (rule evaluation + dynamic escalation)

## Architecture

Use a crate-first design:

- `crates/ava-tools`: tool engines for Git and Browser
- `crates/ava-memory`: durable memory store and query APIs
- `crates/ava-permissions`: deterministic policy engine with escalation checks
- `src-tauri/src/commands`: minimal adapters that parse command input and call crate APIs

This keeps business logic testable in crates and leaves Tauri as transport/orchestration.

## Components

### 1) ava-tools

- `git/mod.rs`
  - `GitTool` entry type
  - `GitAction` enum with typed variants
  - executor for `git` and `gh` subprocesses
  - structured `ToolResult` output
- `browser.rs`
  - `BrowserTool` entry type
  - command routing by action
  - WebDriver-backed interactions for navigation and DOM actions

### 2) ava-memory

- `MemorySystem` with SQLite connection
- Table schema for key/value memories with timestamp
- FTS5 virtual table and indexed lookup paths
- APIs: `remember`, `recall`, `search`, `get_recent`

### 3) ava-permissions

- `PermissionSystem` with ordered `Rule` list
- Match patterns: any, glob, regex, path
- Actions: allow, deny, ask
- `evaluate` applies static rules; `dynamic_check` escalates high-risk actions

## Data Flow

1. Tauri command receives JSON input.
2. Adapter deserializes into strongly typed request structs.
3. Adapter calls crate API (`ava-tools`, `ava-memory`, or `ava-permissions`).
4. Crate returns typed success/error result.
5. Adapter serializes response to existing frontend contract.

## Error Handling

- Keep crate errors typed (`thiserror`) and map to stable user-facing messages at boundary.
- Preserve stderr/stdout for Git/PR failures in diagnostics.
- Return explicit error variants for unsupported actions, invalid rule syntax, and DB failures.
- Fail closed for permissions when evaluation cannot complete.

## Testing Strategy

- Unit tests in each crate for core logic and edge cases.
- Integration tests for Git action dispatch and permission evaluation ordering.
- Memory tests cover persistence and FTS5 search correctness.
- Browser tests validate action routing and webdriver contract handling (mockable boundary where practical).
- Run `cargo test` after each story and keep sprint-level regression run before handoff.

## Implementation Order

1. Scaffold crates + workspace wiring
2. Implement Git tools + tests
3. Implement Browser tool + tests
4. Implement Memory system + tests
5. Implement Permission system + tests
6. Add/adjust Tauri adapters
7. Full sprint verification (`cargo test`)

## Out of Scope (Sprint 33)

- Native/WASM extension runtime (Sprint 34)
- Validation/reflection loops (Sprint 34)
- Performance optimization and 80%+ suite target (Sprint 35)

## Acceptance Mapping

- Git tools and PR support: covered by `ava-tools::git`
- Browser automation and extraction actions: covered by `ava-tools::browser`
- Persistent searchable memory: covered by `ava-memory`
- Static + dynamic permission enforcement: covered by `ava-permissions`
