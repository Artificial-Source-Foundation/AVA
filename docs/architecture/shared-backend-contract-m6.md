---
title: "Canonical Shared-Backend Contract (Milestone 6)"
description: "Contract-definition artifact for cross-surface backend semantics, ownership seams, and conformance requirements."
order: 9
updated: "2026-04-16"
---

# Canonical Shared-Backend Contract (Milestone 6)

Milestone 6 started as the contract-definition artifact. The core normalization program has now adopted that contract across the current desktop/web/TUI scope, so this document is both the normative contract source and the summary of what now counts as implemented.

This document is grounded on:

1. [Cross-Surface Runtime Map (Milestone 4)](cross-surface-runtime-map-m4.md)
2. [Cross-Surface Behavior Audit (Milestone 5)](cross-surface-behavior-audit-m5.md)

All contract language below is normative for upcoming implementation milestones unless an explicit open decision says otherwise.

The first backend-owned implementation slice now exists under `crates/ava-agent/src/control_plane/{commands,interactive,events,sessions,queue}.rs`; this document remains the normative contract source for further adopter rewiring.

Current proof/adoption status (2026-04-16):

1. Backend-owned command + event fixture matrices now lock the canonical wire contract in `crates/ava-agent/src/control_plane/mod.rs`.
2. Desktop/web/TUI adapter tests already prove required event projection/consumption against that backend contract.
3. Frontend control-plane event consumers now prefer canonical snake_case correlation fields instead of keeping parallel camelCase fallbacks for approval/interactive-clear/delegation/edit-progress paths.
4. Session precedence, replay payloads, queue clear semantics, and interactive request ownership now resolve through backend-owned control-plane modules instead of surface-local rule sets.
5. Same-kind interactive FIFO behavior is now normalized across backend + frontend visibility rules, including front-only resolve/timeout semantics for queued approval/question/plan requests.
6. Remaining gaps are no longer core contract-definition gaps; they are bounded follow-ups around adapter-shell simplification and eventual generated/shared TS schema output.

## 1) Contract scope and philosophy

### Scope

The canonical shared-backend contract covers behavior that must remain semantically consistent across interactive TUI, desktop/Tauri, web, and scoped headless execution where applicable:

1. command capabilities and completion semantics
2. approval/question/plan lifecycle
3. event schema and required coverage
4. session continuity
5. queue/cancel behavior
6. delegation visibility

### Philosophy

1. **Backend owns semantics; adapters own transport only.**
2. **One semantic contract, many transports.** IPC/HTTP/WS/terminal delivery may differ, but meaning may not.
3. **No silent drift.** Adapter-only differences must be explicit and documented.
4. **Headless is scoped.** Headless is a non-interactive automation path, not a parity target for full interactive UX.

## 2) Canonical owner seams

| Contract area | Canonical owner seam | Notes |
|---|---|---|
| Command capabilities + completion semantics | New shared control-plane contract module under `crates/ava-agent/src/control_plane/commands.rs` | Transport adapters consume; do not redefine completion meaning. |
| Approval/question/plan lifecycle | New shared interactive lifecycle contract module under `crates/ava-agent/src/control_plane/interactive.rs`, backed by `crates/ava-permissions/` and `crates/ava-tools/src/permission_middleware.rs` | Interactive request state machine is backend-owned. |
| Event schema + coverage | Backend event contract module under `crates/ava-agent/src/control_plane/events.rs` | Adapter projections must map from the canonical schema. |
| Session continuity | Shared session contract module under `crates/ava-agent/src/control_plane/sessions.rs`, implemented against `crates/ava-session/` | One precedence model for requested/last/new session IDs plus canonical prompt-context/history loading and replay-payload builders for retry/edit/regenerate flows. |
| Queue/cancel semantics | Shared queue/cancel contract module under `crates/ava-agent/src/control_plane/queue.rs` | Tier behavior must be explicit and testable. |
| Delegation visibility | Shared routing/runtime seam (`crates/ava-agent/src/routing.rs`, `crates/ava-agent/src/stack/`) plus projection rules in `crates/ava-agent/src/control_plane/events.rs` | Required delegation events are part of contract coverage. |

## 3) Required adopters

Current status:

1. Interactive TUI, desktop/Tauri, and web have adopted the current contract at the command/event/session/interactive/queue seams covered by this milestone program.
2. Headless remains intentionally scoped by documented exception `EX-001` rather than full interactive parity.
3. Desktop run-start/replay completion timing still carries documented exception `EX-002`.

The following paths are required contract adopters in the implementation milestone:

1. **Interactive TUI**
   - `crates/ava-tui/src/app/`
   - `crates/ava-tui/src/state/agent.rs`
2. **Headless CLI (scoped non-interactive)**
   - `crates/ava-tui/src/headless/mod.rs`
   - `crates/ava-tui/src/headless/single.rs`
3. **Desktop/Tauri adapter**
    - `src-tauri/src/commands/agent_commands.rs`
    - `src-tauri/src/events.rs`
    - `src-tauri/src/bridge.rs`
    - `src/services/rust-bridge.ts`
    - `src/hooks/rust-agent-ipc.ts`
4. **Web adapter**
    - `crates/ava-tui/src/web/api_agent.rs`
    - `crates/ava-tui/src/web/api_interactive.rs`
    - `crates/ava-tui/src/web/api.rs`
    - `crates/ava-tui/src/web/state.rs`
    - `crates/ava-tui/src/web/ws.rs`
5. **Shared frontend contract consumers**
    - `src/lib/api-client.ts`
    - `src/types/rust-ipc.ts`
    - `src/hooks/rust-agent-events.ts`

## 4) Command/completion semantics

### Canonical command contract

### Command inventory matrix

| Command family | Canonical owner | Included adopters | Response envelope | Completion mode | Terminal closure signal(s) |
|---|---|---|---|---|---|
| `submit_goal` | `crates/ava-agent/src/control_plane/commands.rs` | TUI, desktop, web, headless | accepted response containing session metadata; `success`/`turns` are not canonical terminal state on accepted-and-streaming adapters | accepted-and-streaming (desktop currently uses documented exception EX-002) | `complete` or `error` event closes the lifecycle |
| `cancel_agent` | `crates/ava-agent/src/control_plane/commands.rs` | TUI, desktop, web, headless | ack | fire-and-forget | no command-level terminal event beyond ack (run interruption/error may arrive on run stream if active) |
| `retry_last_message` / `edit_and_resend` / `regenerate_response` | `crates/ava-agent/src/control_plane/commands.rs` | TUI, desktop, web | accepted response containing session metadata; `success`/`turns` are not canonical terminal state on accepted-and-streaming adapters | accepted-and-streaming (desktop currently uses documented exception EX-002) | `complete` or `error` event closes the lifecycle |
| `resolve_approval` / `resolve_question` / `resolve_plan` | `crates/ava-agent/src/control_plane/commands.rs` | TUI, desktop, web | ack or state transition result | completion-bound | direct command result (`ok`/error) plus request-correlated interactive-clear lifecycle signal on adapters that project canonical interactive events |
| `steer_agent` / `follow_up_agent` / `post_complete_agent` | `crates/ava-agent/src/control_plane/queue.rs` | TUI, desktop, web | ack | accepted-and-streaming | queue acceptance plus later run lifecycle events |
| `clear_message_queue` | `crates/ava-agent/src/control_plane/queue.rs` | TUI, desktop, web | ack or explicit unsupported error | fire-and-forget | none beyond ack/error |
| Tool introspection (`list_agent_tools` / `/tools`) | `crates/ava-agent/src/control_plane/commands.rs` | TUI, desktop, web | tool list payload | completion-bound | direct result |
| Session continuity helpers | `crates/ava-agent/src/control_plane/sessions.rs` | TUI, desktop, web, headless where applicable | session payload | completion-bound | direct result |

Each command in the canonical map MUST define:

1. canonical command name and intent
2. required/optional input fields
3. response envelope
4. completion mode
5. terminal closure signal(s)

### Completion modes

Allowed modes:

1. **Completion-bound**: command call returns final success/failure.
2. **Accepted-and-streaming**: command returns acceptance; terminal status arrives via events.
3. **Fire-and-forget**: command acknowledges dispatch only, with explicit no-terminal guarantee.

Adapters MUST preserve canonical completion mode semantics even when invocation transport differs, except for explicit entries in `docs/architecture/backend-contract-exceptions.md`.

Normative decision:

- `submit_goal` family uses **accepted-and-streaming** semantics. Adapters may return immediately with run/session metadata, but terminal completion MUST arrive through canonical completion/error events rather than adapter-local completion meaning. Desktop currently retains a bounded WS1 exception (EX-002).

## 5) Approval/question/plan lifecycle

Canonical interactive baseline after WS2 desktop/web adoption:

1. Request events are emitted (`approval_request` / `question_request` / `plan_created`) with request correlation IDs.
2. Resolve commands are completion-bound and return direct success/error from the command call.
3. Desktop/web resolve APIs require `request_id` and MUST reject missing/stale IDs without consuming newer pending state.
4. Interactive adapters that project canonical interactive events emit `interactive_request_cleared` for terminal request cleanup on success, timeout, and cancel paths.
5. Accepted-and-streaming interactive adapters MUST ensure every live run has a `run_id`; if a caller omits it, the adapter mints one and attaches it to correlated terminal plus interactive lifecycle events for that run.

Required rules:

1. `resolve_plan` MUST be available end-to-end on all interactive adapters.
2. Resolve command handlers MUST require `request_id` on desktop/web adapters and return explicit errors for missing/invalid pending request state.
3. Successful resolve paths on event-projecting adapters MUST emit the same request-correlated clear event used by timeout/cancel cleanup.
4. Cancel MUST clear any pending approval/question/plan reply handles before the next run begins.

## 6) Event schema and coverage rules

### Canonical schema rules

1. Backend event schema is the source of truth.
2. For each event type, fields are marked **required** or **optional**.
3. Correlation identifiers (run/session/request/tool-call where applicable) MUST be present where required.

### Coverage rules

1. Interactive adapters MUST not silently drop required event types.
2. Any adapter-only omission/addition must be explicitly documented as an intentional exception.
3. Delegation and stream-edit progress events are required coverage areas for interactive surfaces.

## 7) Session continuity rules

Canonical precedence order for run start:

1. explicitly requested session ID
2. adapter-provided last/active session ID fallback
3. backend-generated new session ID

Additional continuity requirements:

1. Retry/edit/regenerate flows MUST preserve continuity semantics.
2. Checkpoints MUST persist enough session state to resume the active conversation without adapter-local reconstruction.
3. Terminal `complete`, `error`, and `cancelled` states MUST persist the session state reached at the last successful checkpoint plus any terminal metadata guaranteed by the backend contract.
4. Cancel-time session behavior is canonical: cancel preserves the current session, marks the run cancelled, and does not create a replacement session.

## 8) Queue/cancel semantics

Queue tier semantics (`Steering`, `FollowUp`, `PostComplete`) are backend-owned and must be uniformly interpreted.

Required contract statements:

1. `clear_message_queue` behavior by tier MUST be explicit (true clear, cancel-only behavior, or unsupported).
2. Canonical rule: `FollowUp` and `PostComplete` clear targets are **unsupported until true clear semantics exist** and MUST return explicit error semantics, not silent success.
3. Cancel semantics are canonical: cancel interrupts the active run, resolves pending interactive requests to `cancelled`, and preserves queued follow-up/post-complete items unless a future contract revision defines explicit drain semantics.

## 9) Delegation visibility requirements

For interactive surfaces (TUI, desktop, web), minimum delegation observability includes:

1. delegation/subagent completion visibility
2. plan-step progress/completion visibility
3. streaming edit progress visibility where emitted by backend

Contract requirements:

1. Shared frontend event types MUST represent required delegation events.
2. Adapter projections MUST not remove required delegation visibility.
3. Headless remains exempt from interactive visualization requirements, but not from backend correctness.

## 10) Headless-specific non-interactive rules

Headless is explicitly non-interactive and follows these contract rules:

1. Auto-approval behavior is allowed as a documented non-interactive exception path.
2. Headless is not required to provide interactive approval/question/plan UX parity.
3. Headless still MUST obey canonical command meaning, event correctness, and session continuity semantics.
4. No headless behavior may require interactive prompts/TTY input in normal operation.

## 11) Explicit open decisions

These remaining decisions must be tracked during implementation, but the blocking semantic choices below are now closed by this contract:

| Decision | Proposed default | Decision owner | Blocking? | Target milestone |
|---|---|---|---|---|
| Timeout policy unification | Closed: shared backend-configurable timeout policy, never adapter-local watchdog semantics | shared interactive lifecycle owner | no | Milestone 6 |
| `submit_goal` completion mode | Closed: accepted-and-streaming | command contract owner | no | Milestone 6 |
| `clear_message_queue` contract for follow-up/post-complete | Closed: explicit unsupported error until true clear semantics exist | queue/cancel owner | no | Milestone 6 |
| Canonical required correlation IDs | Closed: require run ID, session ID, request/interactive ID where applicable, and tool call correlation IDs for tool/approval/result flows | event contract owner | no | Milestone 6 |
| Rust -> TypeScript schema strategy | Closed: mirrored types are allowed only with backend-owned fixture/schema checks in CI until generation is introduced; backend schema remains canonical source | event contract owner + frontend contract consumers | no | Milestone 6 |
| Adapter exception registry location | Store versioned intentional exceptions alongside the contract artifact | contract owner | no | Milestone 7 |

## 12) Conformance testing requirements

Implementation milestone MUST ship a contract conformance matrix with backend-owned fixtures/examples and adapter test coverage.

Minimum required conformance suites:

1. **Command routing and completion semantics**
   - Includes `resolve_plan` end-to-end coverage.
   - Verifies command-specific completion mode and terminal closure behavior.
2. **Approval/question/plan lifecycle**
   - Verifies `emitted -> pending -> terminal` transitions.
   - Verifies timeout and cancel behavior against contract.
3. **Event schema conformance**
   - Verifies required fields and correlation IDs.
   - Verifies required event-type coverage (including delegation/progress events).
4. **Session continuity**
   - Verifies requested/last/new precedence and retry/edit/regenerate continuity.
5. **Queue/cancel semantics**
   - Verifies tier-specific clear and cancel behavior and explicit errors.
6. **Headless non-interactive conformance**
   - Verifies non-interactive operation and scoped exceptions without violating backend semantics.

Conformance gate rule: adapters may not silently invent, drop, or redefine canonical contract fields/semantics outside explicit, versioned exceptions.

Current implementation note:

1. G0a/G0b are implemented with backend-owned fixture coverage for the current command/event inventory.
2. Lifecycle/session/queue conformance is now implemented for the current desktop/web/TUI normalization scope with focused Rust/frontend/TUI regressions.
3. Remaining proof work is mostly deeper end-to-end transport coverage, headless-specific exception coverage, and eventual schema generation to replace mirrored TypeScript definitions.

---

Milestone 6 output: canonical shared-backend contract, now adopted across the current normalization scope.

Implementation planning is defined in [Backend Correction Implementation Roadmap (Milestone 7)](backend-correction-roadmap-m7.md).
