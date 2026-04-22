---
title: "Backend Correction Implementation Roadmap (Milestone 7)"
description: "Implementation-ready roadmap that turns Milestone 5 drift audit and Milestone 6 contract into prioritized backend correction workstreams."
order: 10
updated: "2026-04-21"
---

# Backend Correction Implementation Roadmap (Milestone 7)

Milestone 7 began as the implementation-order artifact for contract adoption. The bounded normalization program described here has now been executed for the current desktop/web/TUI scope, so this document now serves as both the roadmap record and the status summary of what landed.

Grounding inputs:

1. [Cross-Surface Behavior Audit (Milestone 5)](cross-surface-behavior-audit-m5.md)
2. [Canonical Shared-Backend Contract (Milestone 6)](shared-backend-contract-m6.md)

## Scope and execution policy

1. Execute **P0 correctness drift** first (`resolve_plan` routing and event-contract gaps).
2. Land shared control-plane contract slices in `crates/ava-control-plane/src/` (with backend-only shims in `crates/ava-agent/src/control_plane/`) before broad adapter rewiring.
3. Keep adapters transport-only: Tauri IPC and web HTTP/WS may differ mechanically, not semantically.
4. Gate each workstream with explicit conformance tests before moving to the next dependency tier.

Current status note (2026-04-15):

1. WS1/WS2 now have a backend-owned combined command+event fixture matrix in `crates/ava-control-plane/src/`, with backend integration coverage in `crates/ava-agent/src/control_plane/mod.rs`.
2. The remaining proof-first cleanup in this area is now mostly adapter simplification and later WS3/WS4/WS5 lifecycle-session-queue coverage, not another broad command/event redesign.

Completion status update (2026-04-16):

1. WS1 command/completion adoption is implemented across the bounded desktop/web/TUI/headless scope, including stale callsite cleanup and command fixture coverage.
2. WS2 event-schema adoption is implemented for the current scope, including required correlation fields, canonical event fixture proof, and narrowed frontend control-plane consumption.
3. WS3 interactive lifecycle adoption is implemented for desktop/web/TUI within the current scope, including request IDs, run ownership, FIFO same-kind handling, front-only resolve/timeout semantics, and cross-kind TUI arbitration.
4. WS4 session continuity adoption is implemented for the current desktop/web/TUI scope, including requested > last > new precedence, replay/history helpers, and session-owned queue behavior.
5. WS5 queue/cancel semantics are implemented for the current desktop/web/TUI scope, including explicit unsupported clear behavior for follow-up/post-complete and session-correct queue mutations.
6. WS6 delegation/event visibility is partially advanced enough for the current scope, but broader adapter-shell simplification and generated-schema work remain follow-up debt rather than blockers.

## Dependency-ordered workstreams

| Priority | Workstream | Depends on | Canonical owner seam | Primary adopters | First code slice (smallest shippable) |
|---|---|---|---|---|---|
| P0 | WS1: Command map + completion contract bootstrap | None | `crates/ava-control-plane/src/commands.rs` (+ backend shim in `crates/ava-agent/src/control_plane/commands.rs`) | `src-tauri/src/commands/agent_commands.rs`, `crates/ava-web/src/api_agent.rs`, `src/lib/api-client.ts`, `crates/ava-tui/src/app/`, `crates/ava-tui/src/headless/single.rs` | Add canonical command inventory + completion-mode enum, close the web `resolve_plan` mapping gap first, and align TUI/headless command-family semantics to the same contract slice. |
| P0 | WS2: Event schema + required coverage | WS1 | `crates/ava-control-plane/src/events.rs` (+ backend projection in `crates/ava-agent/src/control_plane/events.rs`) | `src-tauri/src/events.rs`, `crates/ava-web/src/api.rs`, `crates/ava-tui/src/app/event_handler/agent_events.rs`, `src/types/rust-ipc.ts`, `src/hooks/rust-agent-events.ts` | Define required event set/field requirements; wire desktop projection for missing `SubAgentComplete` and `StreamingEditProgress`, and verify TUI consumes required canonical event types/fields directly. |
| P1 | WS3: Interactive lifecycle unification (approval/question/plan) | WS1, WS2 | `crates/ava-control-plane/src/interactive.rs` + permission seams (`crates/ava-permissions/`, `crates/ava-tools/src/permission_middleware.rs`) | TUI event loop, Tauri commands, web interactive routes, headless scoped path | Introduce canonical lifecycle state transitions (`emitted -> pending -> terminal`) and shared timeout policy config used by desktop and web. |
| P1 | WS4: Session continuity contract adoption | WS1 | `crates/ava-control-plane/src/sessions.rs` + `crates/ava-session/` (+ backend run-context helpers in `crates/ava-agent/src/control_plane/sessions.rs`) | TUI session state, Tauri run start path, web create/submit/session APIs | Add shared precedence helper (`requested > last > new`) and replace one adapter-local precedence branch with contract call. |
| P2 | WS5: Queue/cancel semantics alignment | WS1, WS3 | `crates/ava-control-plane/src/queue.rs` | TUI queue controls, Tauri queue commands, web queue endpoints | Make `clear_message_queue` return explicit unsupported errors for follow-up/post-complete tiers until true clear semantics exist. |
| P2 | WS6: Delegation visibility parity closure | WS2, WS3 | `crates/ava-agent/src/routing.rs`, `crates/ava-agent-orchestration/src/stack/`, `crates/ava-agent/src/control_plane/events.rs` | TUI event handlers, Tauri/web projection layers, shared TS event union | Enforce minimum delegation observability event coverage and close remaining frontend contract omissions. |

## Canonical owner seams and adopter boundaries

### Owner seams

1. `ava-control-plane` owns pure control-plane semantics (commands, lifecycle, events, sessions, queue, orchestration).
2. `ava-agent` owns runtime core + backend-only control-plane helpers that depend on runtime types (`AgentEvent`, `AgentRunContext`).
3. `ava-agent-orchestration` owns stack/subagent composition and delegation runtime wiring.
4. `ava-permissions` + `ava-tools` permission middleware own interactive policy enforcement hooks.
5. Adapters (`ava-tui` web/headless + `src-tauri` + `ava-web`) own translation/transport only.
6. Frontend TS surfaces (`src/types`, `src/hooks`, `src/lib`) mirror backend contract and cannot invent adapter-only fields.

### Adopter rollout order

1. Web adapter first for P0 (`resolve_plan` mapping correctness).
2. Desktop adapter second for projection parity (currently largest event omission).
3. Shared frontend contract lands alongside each affected adapter slice, beginning in P0 for event/command contract changes.
4. Interactive TUI adopts WS1/WS2/WS3 semantics during the same rollout window as desktop/web, not only at end-state conformance.
5. Headless adopts the scoped command/session contract during rollout and is then validated as the non-interactive exception path.

## First code slices (implementation-ready backlog cuts)

1. **Slice A (P0, 1 PR):** canonical command map skeleton + web `resolve_plan` map fix + route-level test + TUI/headless command-family adoption smoke coverage.
2. **Slice B (P0, 1 PR):** canonical event-schema skeleton + desktop projection for required missing events + TS union extension + TUI required-event consumption checks.
3. **Slice C (P1, 1 PR):** shared interactive lifecycle state machine + unified timeout config plumbed to desktop/web.
4. **Slice D (P1, 1 PR):** session precedence helper adoption in one run-start path per adapter family (desktop/web).
5. **Slice E (P2, 1 PR):** queue clear explicit unsupported behavior + adapter error-shape alignment.
6. **Slice F (P2, 1 PR):** delegation visibility conformance closure + required progress-event coverage checks.

## Required conformance and test gates

| Gate | Must pass before advancing | Minimum enforcement surface |
|---|---|---|
| G0a: Command fixture gate | Backend-owned fixtures/examples exist for command map and completion semantics before WS1 advances. | `crates/ava-control-plane` contract tests + `crates/ava-agent` integration tests |
| G0b: Event fixture gate | Backend-owned fixtures/examples exist for required event fields/coverage before WS2 advances. | `crates/ava-control-plane` event contract tests + `crates/ava-agent` projection tests |
| G0c: Lifecycle/session/queue fixture gate | Backend-owned fixtures/examples exist for interactive lifecycle transitions, session precedence, and queue semantics before WS3-WS5 advance. | `crates/ava-control-plane` lifecycle/session/queue tests + backend adapter integration tests |
| G1: P0 command correctness gate | `resolve_plan` route/command path is proven end-to-end on web adapter; completion mode assertions present for command families; TUI/headless command-family smoke coverage is green. | web API route tests + adapter command map tests + TUI/headless command tests |
| G2: Event parity gate | Desktop/web adapters project all required canonical event types and required correlation fields; TUI consumes required canonical event types/fields directly; no silent drops. | `src-tauri` event projection tests + web projection tests + TUI event-consumption tests + TS contract tests |
| G3: Interactive lifecycle gate | Approval/question/plan requests always reach terminal state (`resolved`, `timed_out`, or `cancelled`) with shared timeout behavior. | `ava-agent` lifecycle tests + desktop/web integration tests |
| G4: Session and queue gate | Session precedence (`requested > last > new`) and queue/cancel contract (including explicit unsupported clear semantics) are consistent across adapters. | backend contract tests + desktop/web adapter tests |
| G5: Headless scoped conformance gate | Headless remains non-interactive, retains allowed auto-approval exception, and still satisfies canonical command/event/session semantics. | headless tests in `crates/ava-tui/src/headless/` + cross-surface conformance checks |

Promotion rule: no workstream is considered complete until its gate is green and no new adapter-only exception is introduced without versioned documentation.

## Versioned exception registry

Milestone 6 closed the decision to keep exceptions explicit and versioned. Registry location for implementation milestone:

- `docs/architecture/backend-contract-exceptions.md`.

Seed this registry in Milestone 7 before implementation starts with the current headless non-interactive exception set.

Milestone 7 deliverable requirements:

1. Create `docs/architecture/backend-contract-exceptions.md`.
2. Initial owner: backend contract owner for `crates/ava-control-plane/src/` plus backend-only shims in `crates/ava-agent/src/control_plane/`.
3. Initial seeded exception entries must include the current headless non-interactive approval/question/plan behavior.

Required exception fields:

1. contract area
2. impacted adapters
3. rationale and risk
4. owner and expiry/removal trigger
5. test coverage proving bounded behavior

## Explicit scope note

This roadmap covers cross-surface backend contract adoption for runtime commands, events, lifecycle, sessions, queue/cancel behavior, and delegation visibility.

It does **not** close the broader built-in-vs-custom command registry unification problem identified earlier in Milestones 2 and 3. That remains a follow-up control-plane milestone after this contract-adoption roadmap unless separately pulled forward.

## Definition of done for Milestone 7 planning

1. Priority and dependency order is explicit and implementation-ready.
2. Owner seams and adopter boundaries are unambiguous.
3. First shippable slices are small enough for incremental PRs.
4. Conformance gates are concrete and enforceable in CI.
5. No refactor code is landed as part of this milestone.

## Current closure note

The roadmap itself is now effectively executed for the bounded normalization scope that was pulled forward:

1. Backend-owned control-plane seams now own the main correctness rules that previously drifted across surfaces.
2. Desktop/web/TUI now align on command/event/session/interactive/queue behavior closely enough to treat this program as complete at the current scope.
3. Remaining work is follow-up hardening, not a failure of roadmap execution:
   - extract more shared adapter-runtime glue from desktop/web shells
   - replace hand-maintained TS wire mirrors with generated/shared schema output
   - add deeper full end-to-end transport proof where current coverage is still seam-level
