# AVA v3 Plan

> Last updated: 2026-03-13 after Sprint 62V validation closeout and sprint archive normalization.
> Related: `docs/development/roadmap.md`, `docs/development/backlog.md`, `docs/development/epics.md`

## Goal

v3 is the milestone where the current active backlog is intentionally burned down, validated in the CLI/TUI, and reorganized into a clean backend-plus-UX release story.

## Current State

- v2.1.0 is released.
- Sprint 60 is implemented and archived.
- Sprint 61 is implemented and archived.
- Sprint 62 is implemented, validated in Sprint 62V, and archived.
- Sprint 63-66 currently remain planning docs only on `master`.
- The active backlog is now mostly backend/platform work plus one major UI-facing gap: `B26` Praxis in chat composer.
- The local reference corpus under `docs/reference-code/` has been refreshed and now reflects newer upstream states across OpenCode, Codex CLI, Goose, Gemini CLI, Cline, OpenHands, Continue, Zed, and others.

## Planning Principles

1. Keep new core capability work Rust-first.
2. Pair backend wins with visible TUI/Desktop UX so features do not stay hidden.
3. Treat implemented items as archive candidates once their validation notes are preserved.
4. Prefer lean-core delivery: default tools stay capped at 6, with Extended/plugin/MCP for optional capability expansion.
5. Validate CLI/headless paths during implementation and keep manual TUI validation as an explicit closeout step.

## Backend Lane

### Sprint 62 - Cost and Runtime Controls

- `B64` Thinking budget configuration
- `B63` Dynamic API key resolution
- `B47` Cost-aware model routing
- `B40` Budget alerts and cost dashboard

Why now: refreshed competitors consistently expose stronger cost visibility, smarter routing, and better long-session reliability.

### Sprint 63 - Execution and Ecosystem Foundations

- `B65` Pluggable backend operations
- `B39` Background agents on branches
- `B61` Dev tooling setup
- `B71` Skill discovery
- `B45` File watcher mode

Why now: this is the substrate needed for safer automation, remote execution later, and a cleaner extension story.

### Sprint 64 - Knowledge and Context Intelligence

- `B38` Auto-learned project memories
- `B57` Multi-repo context
- `B58` Semantic codebase indexing
- `B48` Change impact analysis

Why now: AVA already has strong lexical/codebase primitives; this sprint turns them into a clearer competitive advantage.

### Sprint 65 - Agent Coordination Backend

- `B49` Spec-driven development
- `B59` Agent artifacts system
- `B50` Agent team peer communication
- `B76` Agent Client Protocol (ACP)

Why now: this finishes the coordination layer after execution and knowledge foundations are in place.

### Sprint 66 - Optional Capability Backends

- `B44` Web search capability
- `B52` AST-aware operations
- `B53` Full LSP exposure
- `B69` Code search tool

Why now: these are valuable but should stay Extended/plugin-first so they do not bloat the default tool surface.

## Frontend and UX Lane

### FE-A - Ambient Awareness (pair with Sprint 62)

- Context window usage indicator
- Modular footer/status bar
- Per-turn duration and cost visibility
- Budget and quota surfacing

### FE-B - Conversation Clarity (pair with Sprint 62)

- Tool-call grouping for read/search noise
- Inline diff presentation after edits
- Streaming render polish for calmer chat output

### FE-C - Session and History UX (pair with Sprint 63)

- Session browser search and sort
- Rewind preview and safer session forking
- Session stats summary

### FE-D - Praxis Chat UX (pair with Sprint 65)

- `B26` Praxis in chat composer
- Worker visibility in the sidebar
- Task status and merge-back visibility
- Agent-switching/task-inspection affordances

This is the highest-priority open UX track because `B26` is the only open `P1` item.

### FE-E - Input and Discoverability (pair with Sprint 64)

- Shortcut/help overlay
- Better command discovery
- Long-input polish and editor-friendly flows

### FE-F - Desktop Parity Follow-Through (pair with Sprint 66)

- Port proven TUI improvements into the desktop shell where they make sense
- Keep desktop work scoped to surfacing approved backend capabilities, not creating a second product roadmap

## Competitive Priorities from the Refreshed Reference Corpus

The fresh reference pass reinforced these gaps as the most valuable:

1. Context condensation and long-session control
2. Loop/stuck detection and recovery UX
3. Parallel read-only tool execution
4. Safer rollback/checkpoint workflows
5. Better edit reliability fallbacks
6. Stronger cost visibility and routing
7. Better conversation readability (tool grouping, inline diffs)
8. Better multi-agent visibility once Praxis is exposed in-chat

## Release Rule for v3

Call the work "v3 ready" only when all of these are true:

- The active backlog has no unclear owner or stale status.
- Sprints 60-62 are archived with validation notes preserved.
- Every planned backend sprint has a matching user-visible UX surface where needed.
- CLI/headless verification exists for each completed sprint.
- Remaining desktop-only work is clearly separated from Rust-first CLI/agent work.
