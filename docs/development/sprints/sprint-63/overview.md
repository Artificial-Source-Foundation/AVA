# Sprint 63: Execution and Ecosystem Foundations

## Goal

Strengthen AVA's execution substrate and ecosystem interoperability so future multi-agent, automation, and extension work lands on a cleaner foundation.

## Backlog Items

| ID | Priority | Name | Outcome |
|----|----------|------|---------|
| B65 | P2 | Pluggable backend operations | Decouple tool execution from one local backend path |
| B39 | P2 | Background agents on branches | Isolate background runs from the main worktree |
| B61 | P2 | Dev tooling setup | Speed up iteration and standardize verification |
| B71 | P2 | Skill discovery | Load more reusable project/user skill packs automatically |
| B45 | P2 | File watcher mode | Trigger AVA from project changes without bloating tool surface |

## Why This Sprint

- Enables safer background and future remote execution
- Improves day-to-day development velocity
- Expands interoperability without growing the default tool set

## Scope

### 1. Backend abstraction (`B65`)

- Introduce a clear backend boundary for command/tool execution
- Keep the first implementation small and local-first
- Prepare for isolated, remote, or sandboxed execution later

Likely areas:

- `crates/ava-tools/`
- `crates/ava-agent/`
- shared execution/config modules

### 2. Branch-isolated background work (`B39`)

- Move background agent execution onto isolated branches/worktrees
- Keep status reporting and task visibility intact in the TUI
- Avoid surprising the active worktree

Likely areas:

- `crates/ava-praxis/`
- `crates/ava-tui/`
- git integration helpers

### 3. Dev tooling (`B61`)

- Standardize local verification tooling (`nextest`, coverage, hooks, helpers)
- Keep the workflow CI-friendly and non-interactive

Likely areas:

- workspace config files
- CI/dev scripts
- developer docs

### 4. Skill discovery (`B71`)

- Extend current instruction discovery to skill directories with clear precedence rules
- Keep the initial version conservative and compatible with existing project/global instruction loading

Likely areas:

- `crates/ava-agent/src/instructions.rs`
- config and discovery helpers

### 5. File watcher mode (`B45`)

- Add a watch loop that can trigger AVA from file/comment changes
- Keep first version simple and opt-in
- Avoid turning this into a new default tool concept

Likely areas:

- `crates/ava-tui/` CLI/headless entry points
- `crates/ava-agent/` task kickoff plumbing
- config/docs

## Non-Goals

- No browser automation/plugin marketplace work
- No ACP extraction in this sprint
- No new default tools

## Suggested Execution Order

1. `B61` Dev tooling setup
2. `B65` Pluggable backend operations
3. `B39` Background agents on branches
4. `B71` Skill discovery
5. `B45` File watcher mode

## Verification

- Architecture-level tests around backend abstraction boundaries
- Manual safety validation for background branch/worktree isolation
- CI/local DX checks for new tooling
- Discovery tests for skills/instructions precedence
- Manual opt-in watcher verification on a sample project

## Exit Criteria

- Execution backends have a documented, testable boundary
- Background work no longer threatens the active branch/worktree
- Developer setup is faster and more repeatable
- Skill discovery expands ecosystem compatibility without regressions
- File watcher mode is useful and intentionally opt-in
