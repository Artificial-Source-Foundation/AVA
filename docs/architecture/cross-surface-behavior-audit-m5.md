---
title: "Cross-Surface Behavior Audit (Milestone 5)"
description: "Shared-vs-divergent runtime behavior audit across interactive TUI, headless CLI, desktop/Tauri, and web, grounded on Milestone 4 runtime mapping and current code."
order: 8
updated: "2026-04-21"
---

# Cross-Surface Behavior Audit (Milestone 5)

> Historical/superseded note: this is a Milestone 5 audit snapshot, not a live unresolved-drift tracker. Its findings were carried into the canonical Milestone 6 contract (`shared-backend-contract-m6.md`) and Milestone 7 correction roadmap (`backend-correction-roadmap-m7.md`), and the documented M6/M7 scope has since been completed for the current desktop/web/TUI target.

Milestone 5 audits behavior (not wiring) using Milestone 4 (`cross-surface-runtime-map-m4.md`) plus current implementation references.  
This artifact classifies behavior as one of:

1. **Shared invariant** — should remain common across surfaces and belongs in a canonical backend contract.
2. **Intentional adapter-only difference** — surface-specific behavior that should stay different, but be explicitly documented.
3. **Current drift / bug (at audit time)** — mismatch that created correctness, UX, or maintenance risk and was intended for correction before/with the canonical contract.

Headless remains scoped as a **non-interactive execution adapter**, not a parity target for interactive approval/question/plan UX.

## Highest-risk drift items at audit time

| Priority | Area | Drift / bug | Why risk is high | Evidence |
|---|---|---|---|---|
| P0 | Commands + approvals/plans | **Web `resolve_plan` command mapping gap in frontend API adapter** (`resolve_plan` missing from command->endpoint map) | Plan-review flows can fail in browser mode despite backend route existing; this is a correctness break in an interactive control-plane path. | `src/lib/api-client.ts` (has `resolve_approval`/`resolve_question` map but no `resolve_plan`) vs web route `/api/agent/resolve-plan` in `crates/ava-tui/src/web/mod.rs` |
| P0 | Subagent/delegation visibility + event projection | **Cross-adapter event-contract gap for delegation/stream-edit progress**: desktop drops these events entirely, while web projects some of them but the shared TS/frontend contract still does not represent or handle them fully. `PlanStepComplete` is already represented and consumed on the web/frontend path. | Users/operators cannot trust the same runtime visibility by surface, and fixing desktop alone would still leave shared frontend contract drift. | Desktop mapping drops unmapped backend events in `src-tauri/src/events.rs::from_backend_event`; web maps `SubAgentComplete`, `PlanStepComplete`, `StreamingEditProgress` in `crates/ava-tui/src/web/api.rs`; shared frontend contract/handlers remain incomplete for `SubAgentComplete` and `StreamingEditProgress` in `src/types/rust-ipc.ts`, `src/hooks/rust-agent-events.ts`; TUI handles these directly in `crates/ava-tui/src/app/event_handler/agent_events.rs` |
| P1 | Approvals/questions/plans | **Watchdog policy divergence**: desktop auto-timeouts approval/question after 5m; web has no equivalent watchdog in run path | One adapter can unblock/hard-resolve unattended requests while another can hang waiting forever; policy inconsistency is dangerous. | Desktop watchdogs in `src-tauri/src/commands/agent_commands.rs`; web forwarding in `crates/ava-tui/src/web/api_agent.rs` has no timeout watchdog |
| P1 | Event projection contract | **Desktop vs web payload schema mismatch for interactive/tool events** (`tool_call_id` present desktop approval payload; web omits; tool call id present desktop but omitted web) | Frontend contract complexity increases and behavior-specific fallback logic accumulates; this is core contract drift. | `src-tauri/src/events.rs` vs `crates/ava-tui/src/web/api.rs` |
| P2 | Queue/cancel semantics | **`clear_message_queue` naming over-promises behavior** (follow-up/post-complete clear returns OK but does not clear queued items) | Operator expectation mismatch can cause unsafe or surprising queued follow-up execution. | Desktop `clear_message_queue` in `src-tauri/src/commands/agent_commands.rs`; web equivalent in `crates/ava-tui/src/web/api_agent.rs` |

## Area-by-area classification audit

## 1) Commands

### Shared invariants

- Runtime entry for actual agent execution converges to `AgentStack::run(...)` across interactive TUI, headless, desktop, and web.
- Core command intents (submit, cancel, retry, edit-resend, regenerate, queue ops, approvals/questions/plans resolve) exist across desktop/web adapters.

### Intentional adapter-only differences

- Interactive TUI/headless expose slash-command semantics directly (`/help`, `/skills`, `/permissions`, etc.); desktop/web expose command surface via IPC/HTTP endpoints.
- Web transport is HTTP+WS and desktop is Tauri IPC+event emitter; command invocation mechanics are expected to differ.

### Current drift / bug (at audit time)

- **Web API adapter command map gap for `resolve_plan`** (highest risk, P0 above).
- Command DTO shapes still differ by adapter (`args` nesting, camel/snake transforms in web API bridge), increasing contract duplication pressure.
- Command completion semantics differ materially: desktop `submit_goal`/retry flows are completion-bound on the command call, while web returns immediately after background spawn and relies on WS completion/error for lifecycle closure.

## 2) Approvals / Questions / Plans

### Shared invariants

- Same backend request channels originate from `AgentStack::new(...)`: `approval_rx`, `question_rx`, `plan_rx`.
- Interactive adapters use pending oneshot reply slots and explicit resolve commands.
- Cancel paths clear pending approval/question/plan replies in desktop and web.

### Intentional adapter-only differences

- **Headless** auto-approves tool requests (`spawn_auto_approve_requests`) as a non-interactive automation mode.

### Current drift / bug (at audit time)

- Desktop has watchdog auto-resolution for approval/question; web has no equivalent timeout policy.
- Plan resolution command mapping bug in web frontend path (P0).

## 3) Event projection and delivery

### Shared invariants

- Backend emits `ava_agent::AgentEvent`; each adapter projects to frontend payloads.
- Completion/error events are terminal signals for frontend run lifecycle.

### Intentional adapter-only differences

- Desktop delivery: `AppHandle.emit("agent-event", ...)` (window event bus).
- Web delivery: broadcast channel + WebSocket fan-out; slow consumers can drop events by buffer design.

### Current drift / bug (at audit time)

- Desktop projection omits several backend events that web/TUI expose (`SubAgentComplete`, `StreamingEditProgress`; `PlanStepComplete` is already handled on the web/shared-frontend path).
- Web projects some runtime events that the shared TS/frontend contract still does not represent or consume fully, so the event-contract gap is broader than desktop projection alone.
- Payload-shape differences for comparable event types remain adapter-local (e.g., tool-call identity fields), forcing frontend-side compatibility logic.

## 4) Tool introspection

### Shared invariants

- Interactive tool visibility uses shared backend API `effective_tools_for_interactive_run(goal, history, images)` in TUI helper flow, desktop command, and web endpoint.
- Desktop and web both support explicit context payload with fallback to session-derived context.

### Intentional adapter-only differences

- Headless does not expose equivalent user-facing tool-introspection UX path by design.

### Current drift / bug (at audit time)

- Minor DTO alias differences (`sessionId`/`session_id`, `agentVisible`/`agent_visible`) are normalized in adapter code today but are still duplicated instead of contract-centralized.

## 5) Session lifecycle / persistence

### Shared invariants

- All surfaces persist via shared `SessionManager` and rely on backend session IDs.
- Incremental checkpoint behavior exists during runs (with adapter-specific persistence calls).

### Intentional adapter-only differences

- Web supports optional client-provided session IDs on create/submit to align browser-local state.
- Desktop favors `requested_session_id` then `last_session_id` fallback for continuity.

### Current drift / bug (at audit time)

- Session continuity and ID precedence remain adapter-owned (desktop `last_session_id` flow vs web request/session rules vs TUI local session state), not yet governed by one backend contract module.

## 6) Queue / cancel semantics

### Shared invariants

- All interactive surfaces create a backend `MessageQueue` and pass it into `stack.run(...)`.
- Tier semantics are shared in backend (`Steering`, `FollowUp`, `PostComplete`).
- Cancel interrupts active run and clears pending interactive replies.

### Intentional adapter-only differences

- User interaction model for queue management differs by surface UI, but should map to same backend semantics.

### Current drift / bug (at audit time)

- `clear_message_queue` for follow-up/post-complete currently no-ops (returns success without clearing). This may be acceptable short-term but contract text and naming are misleading.

## 7) Subagent / delegation visibility

### Shared invariants

- Delegation/tool visibility policy determination is shared in backend runtime tooling (`runtime_tool_access_profile(...)`, `effective_tools_for_interactive_run(...)`).
- TUI and web consume backend delegation completion signals in user-visible paths.

### Intentional adapter-only differences

- Headless has no dedicated subagent visualization UX.

### Current drift / bug (at audit time)

- Desktop adapter currently loses delegation visibility by dropping `SubAgentComplete` projection.
- Frontend shared TS event union (`src/types/rust-ipc.ts`) does not yet represent all backend/web projected delegation-progress events.

## Contract input for Milestone 6 (do not implement contract here)

This section is intentionally a **contract-prep checklist**, not the contract itself.

## Ownership and adoption map for Milestone 6

| Contract area | Canonical owner seam | Required adopters | Must-have conformance checks |
|---|---|---|---|
| Commands + completion semantics | Shared control-plane contract adjacent to `crates/ava-agent/` | TUI slash handlers, headless command path, `src-tauri/src/commands/agent_commands.rs`, `crates/ava-tui/src/web/api_agent.rs`, `src/lib/api-client.ts` | `resolve_plan` routing, per-command completion mode, retry/edit/regenerate lifecycle parity |
| Approval/question/plan lifecycle | Shared runtime/control seam in `crates/ava-agent/` plus permission layers in `crates/ava-permissions/` and `crates/ava-tools/src/permission_middleware.rs` | TUI app/event loop, desktop bridge/commands, web state/api handlers, headless non-interactive policy path | pending -> resolved/timeout/cancelled state transitions, timeout policy parity, headless exception policy |
| Event projection schema | Backend-owned event contract adjacent to `crates/ava-agent/` with adapter projections consuming it | `src-tauri/src/events.rs`, `crates/ava-tui/src/web/api.rs`, `src/types/rust-ipc.ts`, `src/hooks/rust-agent-events.ts` | required event coverage, required/optional fields, correlation IDs, delegation/stream-edit event availability |
| Session continuity contract | Shared session/control module adjacent to `crates/ava-agent/` and `crates/ava-session/` | TUI session state, desktop `agent_commands.rs`, web `api_agent.rs`/`api_sessions.rs` | requested vs last session precedence, checkpoint behavior, retry/edit/regenerate continuity |
| Queue/cancel contract | Shared runtime/control seam in `crates/ava-agent/` | TUI queue controls, desktop queue commands, web queue endpoints | tier clear semantics, cancel semantics, steer/follow-up/post-complete consistency |
| Delegation visibility contract | Shared runtime/delegation seam in `crates/ava-agent/src/routing.rs` and `crates/ava-agent/src/stack/` | TUI agent events, desktop event projection, web event projection, shared frontend event types | minimum required delegation events per interactive surface, tool visibility parity, frontend event coverage |

1. **Canonical command map**
   - Required: one backend-owned command capability matrix (surface-agnostic names, required args, response envelope, sync/async completion mode).
   - Risk to eliminate first: `resolve_plan` path mismatch.
   - Required ownership rule: adapters may translate transport only, not redefine completion semantics.
   - Canonical owner seam: shared control-plane contract adjacent to `crates/ava-agent/`.

2. **Canonical interactive request lifecycle**
   - Required: one approval/question/plan request state machine (emitted -> pending -> resolved/timeout/cancelled).
   - Required policy point: explicit timeout semantics (or explicit “no-timeout”) shared by desktop/web/TUI interactive adapters.
   - Canonical owner seam: `crates/ava-agent/` runtime seam plus permission ownership in `crates/ava-permissions/`.

3. **Canonical event projection schema**
    - Required: one normalized event schema for agent runtime projection, including delegation and plan-step events.
    - Required: mandatory/optional fields per event (e.g., tool call correlation IDs).
    - Required: shared TS/frontend contract must be derived from or explicitly aligned to the backend schema instead of inventing adapter-local omissions.
    - Canonical owner seam: backend event contract adjacent to `crates/ava-agent/`, consumed by adapter projection layers.

4. **Canonical session continuity rules**
    - Required: explicit precedence order for requested session ID, last session ID, and new session generation.
    - Required: checkpoint/persist guarantees and cancel-time behavior.
    - Canonical owner seam: shared session/control logic adjacent to `crates/ava-agent/` and `crates/ava-session/`.

5. **Canonical queue/cancel contract**
    - Required: explicit behavior for `clear_message_queue` targets by tier (true clear vs cancel-only), including error semantics for unsupported operations.
    - Canonical owner seam: shared runtime/control seam in `crates/ava-agent/`.

6. **Canonical delegation visibility contract**
    - Required: minimum delegation observability events every interactive surface must expose.
    - Required: alignment between backend-emitted events and frontend shared event types.
    - Canonical owner seam: shared delegation/runtime visibility logic in `crates/ava-agent/src/routing.rs` and `crates/ava-agent/src/stack/`.

7. **Canonical lifecycle completion contract**
   - Required: explicit per-command completion mode (completion-bound, accepted-and-streaming, fire-and-forget) and the terminal event(s) that close the lifecycle.
   - Required: adapters may vary by transport, but not by semantic completion contract.
   - Canonical owner seam: shared control-plane contract adjacent to `crates/ava-agent/`.

8. **Canonical conformance/test matrix**
   - Required: backend-owned fixtures or schema examples plus cross-surface conformance tests for command routing, required payload fields, correlation IDs, completion semantics, timeout behavior, and delegation visibility.
   - Required: no adapter may silently invent/drop fields outside documented adapter-only exceptions.
   - Canonical owner seam: shared contract module plus surface-specific adapter tests.

## Milestone 5 conclusion (historical)

At the time of this audit, the runtime seam was shared but control-plane behavior was still partially adapter-owned.  
This record fed the canonical shared-backend contract and correction roadmap; for current status, rely on [Canonical shared-backend contract (Milestone 6)](shared-backend-contract-m6.md) and [Backend correction implementation roadmap (Milestone 7)](backend-correction-roadmap-m7.md).
