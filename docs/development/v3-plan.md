# AVA v3 Plan

> Last updated: 2026-03-16. v3 is COMPLETE.
> Related: `docs/development/roadmap.md`, `docs/development/backlog.md`, `docs/development/epics.md`

## Goal

v3 is the milestone where the active backlog was burned down, validated in the CLI/TUI, and reorganized into a clean backend-plus-UX release.

## Status: COMPLETE

- v2.1.0 released on 2026-03-08.
- Sprints 60-62 implemented, validated, and archived.
- Sprints 63-66 backend scope implemented and complete on `master`.
- The `packages/` TypeScript orchestration layer has been deleted. Desktop calls Rust directly via Tauri IPC.
- All ARCH, SEC, and UX release criteria met.
- Remaining open work is post-v3 backlog items tracked in `docs/development/backlog.md`.

## Planning Principles (retained for future work)

1. Keep new core capability work Rust-first.
2. Pair backend wins with visible TUI/Desktop UX so features do not stay hidden.
3. Treat implemented items as archive candidates once their validation notes are preserved.
4. Prefer lean-core delivery: default tools stay capped at 6, with Extended/plugin/MCP for optional capability expansion.
5. Validate CLI/headless paths during implementation and keep manual TUI validation as an explicit closeout step.

## Backend Lane (Complete)

### Sprint 62 - Cost and Runtime Controls -- COMPLETE

- `B64` Thinking budget configuration
- `B63` Dynamic API key resolution
- `B47` Cost-aware model routing
- `B40` Budget alerts and cost dashboard

### Sprint 63 - Execution and Ecosystem Foundations -- COMPLETE

- `B65` Pluggable backend operations
- `B39` Background agents on branches
- `B61` Dev tooling setup
- `B71` Skill discovery
- `B45` File watcher mode

### Sprint 64 - Knowledge and Context Intelligence -- COMPLETE

- `B38` Auto-learned project memories
- `B57` Multi-repo context
- `B58` Semantic codebase indexing
- `B48` Change impact analysis

### Sprint 65 - Agent Coordination Backend -- COMPLETE

- `B49` Spec-driven development
- `B59` Agent artifacts system
- `B50` Agent team peer communication
- `B76` Agent Client Protocol (ACP)

### Sprint 66 - Optional Capability Backends -- COMPLETE

- `B44` Web search capability (Extended)
- `B52` AST-aware operations (Extended)
- `B53` Full LSP exposure (Extended)
- `B69` Code search tool (Extended)

## Frontend and UX Lane (Complete)

### FE-A - Ambient Awareness -- COMPLETE

Context window usage indicator, modular footer/status bar, per-turn duration and cost visibility, budget and quota surfacing.

### FE-B - Conversation Clarity -- COMPLETE

Tool-call grouping, inline diff presentation, streaming render polish.

### FE-C - Session and History UX -- COMPLETE

Session browser search and sort, rewind preview, session stats.

### FE-D - Praxis Chat UX -- FIRST SLICE DELIVERED

Praxis accessible via Tab cycling in TUI. Worker visibility in sidebar, task status, cancellation, grouped completion summary. `B26` remains open for deeper worker/task inspection and richer merge-back UX.

### FE-E - Input and Discoverability -- COMPLETE

Shortcut/help overlay (`/shortcuts`, Ctrl+?), command discovery, long-input polish.

### FE-F - Desktop Parity -- COMPLETE

Desktop calls Rust crates directly via Tauri IPC (`src-tauri/src/commands/`). The `packages/` TypeScript layer has been deleted. All backend logic is shared Rust.

## Release Criteria for v3 -- ALL MET

- The active backlog has no stale status text from Sprint 63-66 work.
- Sprints 60-62 are archived with validation notes preserved.
- Every planned backend sprint has a matching user-visible UX surface.
- CLI/headless verification exists for each completed sprint.
- Desktop backend is fully Rust via Tauri IPC (the `packages/` TypeScript layer has been deleted).
