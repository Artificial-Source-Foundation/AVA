---
title: "C++ Backend/TUI Parity Contract Audit (Post-M16)"
description: "Planning-only checklist of scoped parity contracts required before claiming C++ backend/TUI migration completion."
order: 14
updated: "2026-04-24"
---

# C++ Backend/TUI Parity Contract Audit (Post-M16)

This planning-only artifact turns the post-M16 gap audit into a contract checklist.

It defines the backend/headless/TUI contracts that must either be matched, explicitly deferred, or recorded as intentional non-goals before AVA can make a scoped C++ backend/TUI migration-complete claim. It does not expand the migration into web, desktop, MCP/plugin, broad provider, or full Rust TUI parity.

## Authority And Scope

Primary sources:

1. `docs/architecture/cpp-contract-freeze-m1.md` - C++ Milestone 1 freeze scope, fixture anchors, drift risks, and signoff gates.
2. `docs/architecture/cpp-m1-event-stream-parity-checklist.md` - headless JSON event-stream parity checks and accepted emitter differences.
3. `docs/architecture/shared-backend-contract-m6.md` - canonical shared backend command/event/session/queue/delegation semantics.
4. `docs/architecture/backend-contract-exceptions.md` - intentional adapter exceptions, especially `EX-001` and `EX-003`.
5. `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md` - post-M16 completion-critical vs deferred inventory buckets.
6. `cpp/MILESTONE14_BOUNDARIES.md`, `cpp/MILESTONE15_BOUNDARIES.md`, and `cpp/MILESTONE16_BOUNDARIES.md` - current interactive/run/TUI boundary notes.

Scope rules:

1. Backend/orchestration owns lifecycle, run identity, cancellation, interactive requests, and child-run semantics.
2. TUI remains an adapter that presents state and sends explicit user actions; it must not become the lifecycle owner.
3. Headless remains the authoritative backend proof lane under the current scoped non-interactive exception.
4. Web/desktop parity expansion, MCP/plugin parity, long-tail provider parity, and broad async rewrite are not completion gates unless roadmap scope changes.

## Gap-Audit Bucket Alignment

1. **Completion-critical contracts** in this checklist map to RP-1, RP-2, and RP-3 from `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md`.
2. **Deferred inventory contracts** in this checklist map to RP-4A and RP-4B and remain evidence-linked inventory, not completion blockers by default.
3. **Intentional non-goal guardrail:** web/desktop expansion remains out of the backend/headless/TUI completion gate unless roadmap/backlog scope is explicitly promoted.

## Contract Checklist

| Area | Completion contract | Current C++ evidence | Missing contract evidence | Required acceptance trace |
|---|---|---|---|---|
| Control-plane commands | Frozen command inventory, completion modes, terminal signals, and correlation IDs remain aligned with canonical shared-backend contract. | `control_plane_contracts.test.cpp` covers canonical command specs and queue mapping. | No known completion-critical gap. Keep freeze guard active when command contracts change. | `just freeze-m1-check`; `./build/cpp/debug/tests/ava_cpp_tests "canonical command specs stay aligned with frozen contract"` |
| Event schema and headless NDJSON | C++ emits canonical overlapping lifecycle tags (`complete`, `error`, `subagent_complete`) and preserves explicit accepted headless differences. | `ava_app_tests` covers NDJSON event fields including `run_id` and streaming delta payloads. | Need future parity harness if completion claim expands beyond overlapping headless tags. | Headless scripted run emits valid NDJSON lines with stable `type`, `run_id`, tool call/result correlation, and terminal event. |
| Interactive lifecycle | Approval/question/plan requests can enter `pending` and settle to `resolved`, `cancelled`, or `timeout` with stable `run_id` + `request_id`, stale-request rejection, and fail-closed terminal outcomes. | `ava_cpp_tests` and `ava_orchestration_tests` cover request store and bridge lifecycle basics. | TUI adapter action harness for approve/reject/answer/accept-plan is still required before RP-2 closure. | Register one request of each kind, resolve/cancel/timeout through orchestration-owned APIs, assert idempotent terminal state and stable correlation IDs. |
| Session continuity | Requested session wins over last active, replay/resume metadata precedence is deterministic, and persisted message fields needed for tool-heavy replay survive round trip. | `session_foundation.test.cpp` covers SQLite persistence, message metadata, tree/branch behavior, and legacy migration. | Advanced replay parity is deferred unless C++ headless/TUI adopts those commands. | Create session, add tool-heavy transcript, resume by explicit ID, assert metadata and tool call/result JSON survive. |
| Queue and cancel semantics | Queue targets and cancellation behavior fail explicitly for unsupported paths and do not silently drop steering/follow-up work. | `control_plane_contracts.test.cpp` covers queue target parsing and command mapping. | CLI/TUI queue population remains deferred; do not claim full queue parity. | Unsupported queue targets return explicit errors; cancellation clears in-flight run state without corrupting session transcript. |
| Permission and approval policy | Permission middleware is fail-closed, bridges approval decisions through orchestration, and headless auto-approval remains bounded to the scoped non-interactive exception. | `tools_registry.test.cpp`, `headless_runtime_m10.test.cpp`, and orchestration bridge tests cover fail-closed behavior and approval rejection paths. | Full Rust policy persistence (`AllowAlways`, broader session cache semantics) remains outside the narrow completion claim unless promoted. | Mutating tool without resolver fails closed; safe read-only tool executes; approval resolver decision is reflected in tool result and interactive request terminal state. |
| Tool path and edit contract | Local tools preserve workspace containment and edits remain honest about the narrowed exact/replace-all strategy. | `ava_tools_tests` covers read/write/edit basics and symlink escape rejection. | Full Rust edit strategy parity (hashline/fuzzy/recovery) is deferred inventory, not a completion blocker by default. | Read/write/edit reject symlink escape; exact edit succeeds; failed edit returns deterministic no-match error without modifying file. |
| Runtime run identity and streaming | Run leases are unique, foreground cancellation is cooperative, streaming deltas include `run_id`, and partial assistant text is preserved on cancellation. | `ava_agent_tests`, `ava_orchestration_tests`, and `ava_app_tests` cover streaming deltas, cancellation boundaries, and run leases. | Child-run cancellation and watchdog/arbitration harness are future-required before RP-3 closure. | Start streaming run, cancel mid-stream, assert terminal reason, persisted partial text, and consistent run ID across deltas/events. |
| Orchestration/subagent lifecycle | Native blocking child sessions enforce depth/spawn budget, thread interactive resolvers, persist child lineage, and surface deterministic terminal metadata. | `ava_orchestration_tests` covers native blocking task spawner, depth/spawn controls, child sessions, and resolver propagation. | Externally addressable child-run listing/cancellation remains missing before completion-critical RP-3 closure. | Parent starts child, observes active child handle, requests cancellation through orchestration-owned API, and receives deterministic child terminal summary. |
| TUI adapter workflow | TUI provides core operator flows for slash/help/clear/model, input history, navigation/status, pending request visibility, and narrow interactive actions without owning runtime lifecycle. | `ava_tui_tests` covers current slash/history/navigation/status/pending visibility. | Adapter action harness for approval/question/plan resolution is future-required. | In TUI state tests, display pending request, send approve/reject/answer/accept-plan adapter action, and assert bridge-owned terminal state changes. |
| Deferred inventory | MCP/plugin/tool-extension and provider/auth/config breadth stay explicitly bucketed and evidence-linked. | Gap audit has RP-4A/RP-4B buckets and current C++ tests for core tools/OpenAI/config foundations. | Inventory consistency harnesses are future work. | Deferred bucket changes must update this audit, the gap audit, and backlog/changelog before being promoted to completion-critical scope. |
| Intentional non-goal guardrail | Web/desktop parity expansion remains out of the backend/headless/TUI completion gate unless scope is explicitly promoted. | Gap audit intentional non-goal bucket and active backlog scope notes keep this boundary explicit. | No completion-critical gap while scope remains unchanged. | If scope broadens, update this audit, the gap audit, backlog, and changelog before changing completion-claim language. |

## Acceptance Trace Scenarios

1. **Headless tool approval trace:** run a headless scripted mutating tool request without auto-approve, assert approval request creation, fail-closed result, terminal error metadata, and persisted session state.
2. **Interactive terminal-state trace:** register approval/question/plan requests with run IDs, settle each through resolve/cancel/timeout, assert stale request rejection and idempotent terminal behavior.
3. **TUI adapter action trace:** surface a pending approval/question/plan in `AppState`, send adapter actions, and assert only the orchestration bridge mutates lifecycle state.
4. **Streaming cancellation trace:** stream assistant deltas, cancel mid-stream, assert run ID stability, cancellation reason, partial assistant preservation, and no post-cancel tool execution.
5. **Child-run lifecycle trace:** spawn a native child run, enforce depth/spawn limits, thread interactive resolvers, persist child session lineage, and later add active-child cancellation evidence.
6. **Tool containment trace:** attempt read/write/edit through a symlink escape, assert rejection; perform valid exact edit, assert deterministic content and message.
7. **Session continuity trace:** create a session with tool calls/results, resume explicitly, and assert message metadata survives without legacy namespace drift.
8. **Deferred inventory trace:** when MCP/plugin/provider/auth/config breadth changes, assert the corresponding RP-4A/RP-4B bucket and validation command is updated before claim language changes.
9. **Intentional non-goal scope trace:** if web/desktop parity expansion is proposed, update gap-audit bucketing plus backlog/changelog scope language before changing completion-claim wording.

## Validation Commands

Docs-only validation for this audit:

```bash
rg -n "Gap-Audit Bucket Alignment|Control-plane commands|Interactive lifecycle|TUI adapter workflow|Deferred inventory|Intentional non-goal guardrail|Acceptance Trace" docs/architecture/cpp-backend-tui-parity-contract-audit-post-m16.md
rg -n "completion-critical|deferred inventory|intentional non-goal|RP-1|RP-2|RP-3|RP-4A|RP-4B" docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md
```

Focused follow-up validation slices:

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug

# Freeze and canonical contract tables
just freeze-m1-check
ctest --preset cpp-debug -R "ava_cpp_unit" --output-on-failure

# Interactive lifecycle and bridge ownership
./build/cpp/debug/tests/ava_cpp_tests "interactive request store tracks pending and terminal lifecycle"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge tracks approval/question/plan request lifecycle"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge terminal cancelled/timedout outcomes are fail-closed"

# TUI adapter state and future action harness
./build/cpp/debug/tests/ava_tui_tests "tui state slash commands provide help/clear/model and unsupported compact handling"
./build/cpp/debug/tests/ava_tui_tests "tui state tracks adapter-facing interactive request visibility and clearing"
# TODO(RP-2): add focused approve/reject/answer/accept-plan adapter-action tests before RP-2 closure.

# Runtime and orchestration lifecycle
./build/cpp/debug/tests/ava_agent_tests "agent runtime emits streaming assistant deltas with run_id"
./build/cpp/debug/tests/ava_agent_tests "agent runtime exits cooperatively when cancelled during streaming"
./build/cpp/debug/tests/ava_orchestration_tests "run controller issues unique run leases and cooperative cancellation state"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads interactive resolvers into child composition"
# TODO(RP-3): add active-child listing/cancellation and watchdog/arbitration harnesses before RP-3 closure.

# Tool containment and deferred breadth guardrails
./build/cpp/debug/tests/ava_tools_tests "read/write/edit reject symlink escapes outside workspace"
./build/cpp/debug/tests/ava_tools_tests "default tools registration includes milestone 6 core set"
./build/cpp/debug/tests/ava_llm_tests "factory selects openai and stubs deferred providers"
```

## Documentation Update Rules

1. Update this audit when a parity contract changes, closes, or is deliberately reclassified.
2. Update `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md` when RP status or completion-critical/deferred/intentional-non-goal buckets change.
3. Update `docs/architecture/backend-contract-exceptions.md` for any new adapter exception, including rationale, risk, owner, expiry condition, and test coverage.
4. Update `cpp/MILESTONE{N}_BOUNDARIES.md` for every subsequent C++ migration slice.
5. Update `docs/project/backlog.md` and `CHANGELOG.md` when a contract audit milestone is added or closed.
