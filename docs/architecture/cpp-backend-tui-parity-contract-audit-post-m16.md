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
4. `docs/architecture/backend-contract-exceptions.md` - intentional adapter exceptions, especially `EX-001`, `EX-003`, and scoped C++ `EX-004`.
5. `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md` - post-M16 completion-critical vs deferred inventory buckets.
6. `cpp/MILESTONE14_BOUNDARIES.md`, `cpp/MILESTONE15_BOUNDARIES.md`, and `cpp/MILESTONE16_BOUNDARIES.md` - baseline interactive/run/TUI boundary notes.
7. `cpp/MILESTONE17_BOUNDARIES.md`, `cpp/MILESTONE18_BOUNDARIES.md`, `cpp/MILESTONE19_BOUNDARIES.md`, `cpp/MILESTONE20_BOUNDARIES.md`, `cpp/MILESTONE21_BOUNDARIES.md`, and `cpp/MILESTONE22_BOUNDARIES.md` - post-M16 closure evidence and deferred-boundary records.

Scope rules:

1. Backend/orchestration owns lifecycle, run identity, cancellation, interactive requests, and child-run semantics.
2. TUI remains an adapter that presents state and sends explicit user actions; it must not become the lifecycle owner.
3. Headless remains the authoritative backend proof lane under the current scoped non-interactive exception.
4. Web/desktop parity expansion, MCP/plugin parity, long-tail provider parity, and broad async rewrite are not completion gates unless roadmap scope changes.

## Gap-Audit Bucket Alignment

1. **Completion-critical contracts** in this checklist map to Research Priorities (`RP-1`, `RP-2`, and `RP-3`) from `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md`.
2. **Deferred inventory contracts** in this checklist map to RP-4A and RP-4B and remain evidence-linked inventory, not completion blockers by default.
3. **Intentional non-goal guardrail:** web/desktop expansion remains out of the backend/headless/TUI completion gate unless roadmap/backlog scope is explicitly promoted.

## Contract Checklist

| Area | Completion contract | Current C++ evidence | Missing contract evidence | Closure acceptance trace |
|---|---|---|---|---|
| Control-plane commands | Frozen command inventory, completion modes, terminal signals, and correlation IDs remain aligned with canonical shared-backend contract. | `control_plane_contracts.test.cpp` covers canonical command specs and queue mapping. | No known completion-critical gap. Keep freeze guard active when command contracts change. | `just freeze-m1-check`; `./build/cpp/debug/tests/ava_cpp_tests "full command spec table matches frozen contract"` |
| Event schema and headless NDJSON | C++ covers `complete`/`error` NDJSON tags, run-id-bearing runtime event serialization, and focused tool call/result `call_id` correlation; M19 adds deterministic child terminal summary metadata but does not claim canonical headless `subagent_complete` parity. | `ava_app_tests` covers individual NDJSON event fields including `run_id`, streaming delta payloads, and tool call/result `call_id` correlation; `ava_orchestration_tests` covers child terminal summaries. | Full scripted NDJSON-stream parity remains deferred; canonical `subagent_complete` emission is explicitly tracked as C++ scoped-completion exception `EX-004`. | Focused headless event tests assert stable `type`, `run_id`, tool call/result `call_id` correlation, and terminal events; M19 child terminal summaries are the scoped C++ child-run terminal evidence while canonical `subagent_complete` remains deferred under `EX-004`. |
| Interactive lifecycle | Approval/question/plan requests can enter `pending` and settle to `resolved`, `cancelled`, or `timeout` with stable `run_id` + `request_id`, stale-request rejection, and fail-closed terminal outcomes. | `ava_cpp_tests` and `ava_orchestration_tests` cover request store lifecycle, stale/non-existent request rejection, bridge lifecycle basics, idempotency, and run correlation; M18 adds request-id-bearing TUI adapter action evidence for approve/reject/answer/accept-plan. | Full modal/widget parity remains deferred. | Register one request of each kind, resolve/cancel/timeout through orchestration-owned APIs, assert idempotent terminal state and stable correlation IDs. |
| Session continuity | Requested session wins over last active, replay/resume metadata precedence is deterministic, and persisted message fields needed for tool-heavy replay survive round trip. | `session_foundation.test.cpp` covers SQLite persistence, message metadata, tree/branch behavior, and legacy migration; M20 adds explicit resume-by-ID tool-heavy metadata survival coverage. | Advanced replay parity remains deferred unless C++ headless/TUI adopts those commands. | Create session, add tool-heavy transcript, resume by explicit ID, assert metadata and tool call/result JSON survive. |
| Queue and cancel semantics | Queue targets and cancellation behavior fail explicitly for unsupported paths and do not silently drop steering/follow-up work. | `control_plane_contracts.test.cpp` covers queue target parsing and command mapping; M20 adds cancellation transcript-integrity coverage for session order and parent links. | CLI/TUI queue population remains deferred and full queue parity must not be claimed. | Unsupported queue targets return explicit errors; cancellation clears in-flight run state without corrupting session transcript. |
| Permission and approval policy | Permission middleware is fail-closed without a resolver, bridges approval decisions through orchestration, and headless auto-approval is bounded by `EX-001`. | `tools_registry.test.cpp`, `headless_runtime_m10.test.cpp`, and orchestration bridge tests cover fail-closed behavior, approval rejection paths, safe read-only auto-approve coverage, and high-risk mutating auto-approve rejection. | Full Rust policy persistence (`AllowAlways`, broader session cache semantics) remains outside the narrow completion claim unless promoted. | Mutating tool without resolver fails closed; safe read-only tool executes; dangerous mutating tool behavior under `--auto-approve` is rejected per `EX-001`. |
| Tool path and edit contract | Local tools preserve workspace containment and M22 now adds a bounded non-`replace_all` cascade beyond exact-only matching (quote-normalized exact, explicit occurrence/line/anchor targeting, line-trimmed/auto-anchor/ellipsis/flexible-whitespace fallbacks). | `ava_tools_tests` covers read/write/edit basics, symlink escape rejection, deterministic cascade strategy hits, and no-match edit immutability. | Advanced Rust edit parity (hashline, weighted fuzzy/recovery, merge-style fallbacks) remains deferred inventory by default. | Read/write/edit reject symlink escape; bounded cascade strategies apply deterministically; failed edit still returns deterministic no-match error without modifying file. |
| Runtime run identity and streaming | Run leases are unique, foreground cancellation is cooperative, streaming deltas include `run_id`, partial assistant text is preserved on cancellation, and M19 deadline expiry participates in cooperative cancellation for child runs. | `ava_agent_tests`, `ava_orchestration_tests`, and `ava_app_tests` cover streaming deltas, cancellation boundaries, run leases, and deadline-expired cancellation state. | Broad async/background scheduler parity and hard-kill provider interruption remain deferred. | Start streaming run, cancel mid-stream, assert terminal reason, persisted partial text, and consistent run ID across deltas/events; child-run watchdog evidence asserts deterministic timeout summary. |
| Orchestration/subagent lifecycle | Native blocking child sessions enforce depth/spawn budget, thread interactive resolvers, persist child lineage, expose active child-run listing/cancellation, and surface deterministic terminal metadata. | `ava_orchestration_tests` covers native blocking task spawner, depth/spawn controls, child sessions, resolver propagation, active child-run cancellation, watchdog timeout summaries, and persisted child terminal metadata. | Full async/background subagent parity and provider hard-kill semantics remain deferred. | Parent starts child, observes active child handle, requests cancellation through orchestration-owned API, and receives deterministic child terminal summary. |
| TUI adapter workflow | TUI provides core operator flows for slash/help/clear/model, input history, navigation/status, pending request visibility, narrow interactive dock actions, and child-run observer projection without owning runtime lifecycle. | `ava_tui_tests` covers slash/history/navigation/status/pending visibility, M18 request-id-bearing approve/reject/answer/accept-plan/cancel-question/reject-plan adapter actions through `InteractiveActionAdapter`, M26 approval/question/plan dock state/action/result behavior plus question cancellation/plan rejection and minimal detail projection, and M19 child-run terminal metadata projection. | Full modal/widget parity and polished request-payload rendering remain deferred. | In TUI state tests, display pending request, open the bounded dock, send approve/reject/answer/accept-plan/reject-plan/cancel-question adapter actions, assert bridge-owned terminal state changes, assert `AppState` clears only from backend pending snapshots, and project child terminal metadata without mutating foreground run state. |
| Deferred inventory | MCP/plugin/tool-extension and provider/auth/config breadth stay explicitly bucketed and evidence-linked. | Gap audit has RP-4A/RP-4B buckets, M20 records deferred guardrails, M25 adds a narrow MCP protocol/config/client foundation, and current C++ tests cover core tools/OpenAI/Anthropic/config foundations. | No completion-critical gap while buckets remain deferred. | Deferred bucket changes must update this audit, the gap audit, and backlog/changelog before being promoted to completion-critical scope; M25 does not claim MCP runtime tool-registration, plugin, custom-tool, or browser-tool parity. |
| Intentional non-goal guardrail | Web/desktop parity expansion remains out of the backend/headless/TUI completion gate unless scope is explicitly promoted. | Gap audit intentional non-goal bucket and active backlog scope notes keep this boundary explicit. | No completion-critical gap while scope remains unchanged. | If scope broadens, update this audit, the gap audit, backlog, and changelog before changing completion-claim language. |

## Acceptance Trace Scenarios

1. **Headless tool approval trace:** run a headless scripted mutating tool request without auto-approve, assert approval request creation, fail-closed result, terminal error metadata, and persisted session state; run a dangerous mutating request with `--auto-approve` and assert it still fails closed per `EX-001`.
2. **Interactive terminal-state trace:** register approval/question/plan requests with run IDs, settle each through resolve/cancel/timeout, assert stale request rejection and idempotent terminal behavior.
3. **TUI adapter action trace:** surface a pending approval/question/plan in `AppState`, open the bounded interactive dock with minimal details, send adapter actions, assert only the orchestration bridge mutates lifecycle state, and keep local UI state synchronized from backend pending snapshots or accepted adapter results.
4. **Streaming cancellation trace:** stream assistant deltas, cancel mid-stream, assert run ID stability, cancellation reason, partial assistant preservation, and no post-cancel tool execution.
5. **Child-run lifecycle trace:** spawn a native child run, enforce depth/spawn limits, thread interactive resolvers, persist child session lineage, list active child runs, cancel a child through orchestration-owned APIs, and record deterministic child terminal metadata.
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

Prerequisite: ensure the C++ debug tree is configured and built before running direct test-binary filters. Test names are current as of this audit date and must be updated if Catch2 case names change.

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug

# Freeze and canonical contract tables
just freeze-m1-check
just cpp-test cpp-debug -R "ava_cpp_unit" --output-on-failure
./build/cpp/debug/tests/ava_cpp_tests "full command spec table matches frozen contract"

# Headless NDJSON event serialization
./build/cpp/debug/tests/ava_app_tests "ndjson event preserves canonical complete and error tags"
./build/cpp/debug/tests/ava_app_tests "ndjson event carries run_id and streaming delta payload"
./build/cpp/debug/tests/ava_app_tests "ndjson tool call and result correlate call_id"
./build/cpp/debug/tests/ava_app_tests "resume by id preserves tool heavy message metadata"

# Interactive lifecycle and bridge ownership
./build/cpp/debug/tests/ava_cpp_tests "interactive request store tracks pending and terminal lifecycle"
./build/cpp/debug/tests/ava_cpp_tests "interactive request store rejects stale and non-existent requests"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge tracks approval/question/plan request lifecycle"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge terminal cancelled/timedout outcomes are fail-closed"

# TUI adapter state and action harness
./build/cpp/debug/tests/ava_tui_tests "tui state slash commands provide help/clear/model and unsupported compact handling"
./build/cpp/debug/tests/ava_tui_tests "tui state tracks adapter-facing interactive request visibility and clearing"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action approve resolves pending approval via bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action reject cancels pending approval via bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action answer carries request_id to bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action accept-plan delegates to orchestration bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects stale or missing request id through bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects unknown action kind"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects unavailable bridge"
./build/cpp/debug/tests/ava_tui_tests "tui state tracks adapter-facing interactive request visibility and clearing"
./build/cpp/debug/tests/ava_tui_tests "tui state can dismiss interactive dock without resolving backend request"

# Runtime and orchestration lifecycle
./build/cpp/debug/tests/ava_agent_tests "agent runtime emits streaming assistant deltas with run_id"
./build/cpp/debug/tests/ava_agent_tests "agent runtime exits cooperatively when cancelled during streaming"
./build/cpp/debug/tests/ava_agent_tests "agent runtime cancels before tool execution after streamed assistant text"
./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled before tool execution"
./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled during streaming"
./build/cpp/debug/tests/ava_agent_tests "agent runtime cancellation preserves session transcript integrity"
./build/cpp/debug/tests/ava_orchestration_tests "run controller issues unique run leases and cooperative cancellation state"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads interactive resolvers into child composition"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner exposes active child runs for cancellation"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner watchdog timeout surfaces deterministic terminal summary"
./build/cpp/debug/tests/ava_tui_tests "tui state projects child-run terminal metadata without owning lifecycle"

# M20 closure evidence
./build/cpp/debug/tests/ava_app_integration_tests "headless auto approve rejects dangerous mutating tool"
./build/cpp/debug/tests/ava_app_integration_tests "headless scripted tool loop executes tool and persists transcript"
./build/cpp/debug/tests/ava_tools_tests "edit no match returns error without mutating file"

# Tool containment and deferred breadth guardrails
./build/cpp/debug/tests/ava_tools_tests "read/write/edit reject symlink escapes outside workspace"
./build/cpp/debug/tests/ava_tools_tests "default tools registration includes milestone 6 core set"
./build/cpp/debug/tests/ava_llm_tests "factory selects openai + anthropic and stubs deferred providers"
```

## Documentation Update Rules

1. Update this audit when a parity contract changes, closes, or is deliberately reclassified.
2. Update `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md` when RP status or completion-critical/deferred/intentional-non-goal buckets change.
3. Update `docs/architecture/backend-contract-exceptions.md` for any new adapter exception, including rationale, risk, owner, expiry condition, and test coverage.
4. Update `cpp/MILESTONE{N}_BOUNDARIES.md` for every subsequent C++ migration slice.
5. Update `docs/project/backlog.md` and `CHANGELOG.md` when a contract audit milestone is added or closed.
