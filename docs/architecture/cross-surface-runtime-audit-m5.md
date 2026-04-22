---
title: "Cross-Surface Runtime Audit (Milestone 5)"
description: "Priority-ordered parity audit of shared runtime behavior across interactive TUI, headless, desktop, and web command/control, event, and lifecycle paths."
order: 8
updated: "2026-04-14"
---

# Cross-Surface Runtime Audit (Milestone 5)

> Superseded as the canonical Milestone 5 artifact by [cross-surface-behavior-audit-m5.md](cross-surface-behavior-audit-m5.md).
> Use this document only as supporting runtime-audit detail. For current status, use Milestone 6/7 docs; this file is a historical Milestone 5 snapshot.

This is the Milestone 5 runtime-audit artifact (parity findings only). It does not implement contract changes.

Findings are classified as:

- **Shared invariant**: behavior that already aligns and should remain common.
- **Intentional adapter-only difference**: explicit surface-level differences that are acceptable by design.
- **Current drift / bug (at audit time)**: mismatches that increased runtime or UX risk and were slated for Milestone 6+ correction.

Priority order is by risk (P0 highest).

## Highest-risk findings (first)

| Priority | Area | Finding | Why high | Evidence |
|---|---|---|---|---|
| P0 | Commands + completion | **`resolve_plan` adapter mapping is missing in the desktop/web frontend command API client** | Browser plan-completion flows can fail even when backend route exists; this blocks interactive planning UX. | `src/lib/api-client.ts` (no `resolve_plan` mapping) vs web route `POST /api/agent/resolve-plan` in `crates/ava-web/src/lib.rs` |
| P0 | Event projection and delivery | **Sub-agent and stream-edit events are not consistently projected across surfaces** (`SubAgentComplete`, `StreamingEditProgress` drop or partial coverage). | Delegation visibility and tool-edit streaming cannot be trusted as equivalent across surfaces. | `src-tauri/src/events.rs::from_backend_event` drops events vs `crates/ava-web/src/api.rs` mapping includes them |
| P1 | Approvals / questions / plans | **No timeout policy parity for web interactive requests** | Desktop auto-resolves stale approval/question paths after 5 minutes; web currently leaves equivalent requests pending indefinitely. | `src-tauri/src/commands/agent_commands.rs` vs `crates/ava-web/src/api_agent.rs` |
| P1 | Queue / cancel semantics | **`clear_message_queue` follow-up/post-complete is a no-op despite user-facing clear endpoint** | Users can expect queued items cleared but currently receive success without an effective cancel/clear for these targets. | `src-tauri/src/commands/agent_commands.rs` + `crates/ava-web/src/api_agent.rs` |
| P2 | Event projection schema parity | **Payload and field-shape divergence for comparable approval/question/plan events** | Shared frontend handling code becomes surface-specific and fragile. | `src-tauri/src/events.rs` vs `crates/ava-web/src/api.rs` |

## 1) Commands

### Shared invariant
- All interactive and headless paths invoke shared runtime through `AgentStack::run(...)` (or equivalent direct stack entrypoint) and therefore remain on the same execution seam.
- Command families (submit, retry, edit-resend, regenerate, queue ops, resolve paths) exist across desktop and web adapters, and headless can execute the same stack via dedicated entrypoints.

### Intentional adapter-only difference
- Desktop is IPC-command based (`invoke` handlers in `src-tauri/src/commands`), web is HTTP + WS (`/api/agent/*` and WebSocket stream).
- Headless is non-interactive by design and therefore does not expose a direct question/approval UI command surface.

### Current drift / bug
- **P0** `resolve_plan` endpoint does not have a frontend adapter mapping, while desktop/web backends expect it.
  - `src/lib/api-client.ts` misses `resolve_plan` route mapping, though `crates/ava-web/src/api_interactive.rs` and `crates/ava-web/src/lib.rs` define the backend path.
- **P1** `submit_goal` completion semantics differ:
  - Web returns immediately after scheduling (`accepted`) and relies on stream/WS for completion, while desktop command currently waits for completion before returning in its invoke handler.
  - Evidence: `src-tauri/src/commands/agent_commands.rs` vs `crates/ava-web/src/api_agent.rs`.
- Lower risk: command DTO naming/casing normalization is implemented in both adapters but duplicated (`session_id` vs `sessionId`, args shape variants).

## 2) Approvals / questions / plans

### Shared invariant
- Interactive request channels are sourced from `approval_rx`, `question_rx`, and `plan_rx` in stack startup and exposed as pending interactive decisions.
- Both desktop and web provide resolve commands for all three request types.

### Intentional adapter-only difference
- Headless uses explicit auto-approval behavior for non-interactive operation, which is expected and currently documented as a scoped exception.

### Current drift / bug
- **P1** Desktop includes watchdog auto-resolution (5 min) for pending approvals/questions; web has no corresponding timeout in request-forwarding code.
- **P1** `resolve_plan` is blocked by client mapping drift (same issue as command section).

## 3) Event projection & delivery

### Shared invariant
- All adapters subscribe to backend agent events and convert them into frontend-facing events.
- Completion/error outcomes are terminal signals used by each surface to close execution lifecycle.

### Intentional adapter-only difference
- Delivery transport differs by contract surface:
  - Desktop emits `agent-event` via Tauri event channels.
  - Web emits event payloads via broadcast + WS (`/ws`) fan-out.

### Current drift / bug
- **P0/P1** Desktop projection drops backend events that web currently includes, specifically delegation/completion visibility signals (`SubAgentComplete`) and stream-edit progress (`StreamingEditProgress`).
- **P1** Tool-call correlation payload differences remain surface-local (`tool_call_id`/approval IDs shapes differ), creating downstream ambiguity for shared handlers.
- Lower: queue lifecycle events are not always represented with identical semantics in adapters.

## 4) Tool introspection

### Shared invariant
- Desktop and web tool introspection requests use the same backend helper path for effective visibility (`effective_tools_for_interactive_run(...)`) and pass goal/history/image context where available.

### Intentional adapter-only difference
- Headless does not expose explicit user-facing introspection commands (non-interactive automation mode).

### Current drift / bug
- Serialization shape differences in request/response DTOs (`session_id` vs `sessionId`, visibility fields) are present but normalized in adapters, adding conversion debt rather than reducing it.

## 5) Session lifecycle / persistence

### Shared invariant
- All surfaces rely on shared session persistence primitives and checkpointing behavior that ultimately feed the same backend stack lifecycle.

### Intentional adapter-only difference
- Desktop supports `requested_session_id` fallback to a local `last_session_id`; web relies on explicit request session continuation and generates a fresh session id when absent.

### Current drift / bug
- Retry/edit/regenerate/queue operations around continuation semantics are not defined by one contract, so behavior varies by adapter and risks user-facing inconsistency.

## 6) Queue / cancel semantics

### Shared invariant
- Cancel and queue-clear endpoints exist for all interactive adapters and target message-queue steering/tier controls.
- Message queue clear and cancel logic is routed through backend queue structures in both desktop and web.

### Intentional adapter-only difference
- Operator gesture model differs (desktop UI controls vs web API calls), while tiers remain intended as shared (`Steering`, `FollowUp`, `PostComplete`).

### Current drift / bug
- **P1/P2** `clear_message_queue` handles follow-up/post-complete as success responses without actually clearing queued items on both surfaces.

## 7) Subagent / delegation visibility

### Shared invariant
- Delegation behavior and tool visibility policy are determined by shared backend logic (`runtime_tool_access_profile` + interactive helper path), so semantics should be transport-agnostic.
- TUI/web event consumers are expected to receive delegation completion/progress signals for operator awareness.

### Intentional adapter-only difference
- Headless is currently not built around visible delegation telemetry.

### Current drift / bug
- **P0/P1** Desktop drops at least `SubAgentComplete` event projection, and shared TS/frontend event contracts do not uniformly model all delegation event payloads used by web projections.

## Audit status

- This Milestone 5 output is intentionally read-only and preparatory.
- Contract content remains for Milestone 6; no runtime behavior contracts were implemented in this file.
