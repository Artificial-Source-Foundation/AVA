---
title: "C++ Backend/Headless/TUI Migration Completion Roadmap (Post-M16, M17+)"
description: "Planning roadmap for scoped migration completion across M17-M20, including acceptance criteria, decision points, validation lanes, and deferred-scope guardrails."
order: 15
updated: "2026-04-24"
---

# C++ Backend/Headless/TUI Migration Completion Roadmap (Post-M16, M17+)

This planning-only artifact sequences the post-M16 completion push into narrow, durable milestones.

It is explicitly scoped to backend/headless/TUI completion claims under the current non-interactive exception baseline and does **not** claim full Rust parity, full TUI modal parity, web/desktop parity, MCP/plugin parity, or broad provider/auth parity.

## Inputs and authority

1. `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md`
2. `docs/architecture/cpp-backend-tui-parity-contract-audit-post-m16.md`
3. `cpp/MILESTONE14_BOUNDARIES.md`, `cpp/MILESTONE15_BOUNDARIES.md`, `cpp/MILESTONE16_BOUNDARIES.md`
4. `docs/architecture/backend-contract-exceptions.md`
5. `cpp/MILESTONE17_BOUNDARIES.md`, `cpp/MILESTONE18_BOUNDARIES.md`, `cpp/MILESTONE19_BOUNDARIES.md`, and `cpp/MILESTONE20_BOUNDARIES.md`

## Scope guardrails for completion claims

1. Backend/orchestration remains lifecycle owner (interactive lifecycle, run identity, cancellation, child-run controls).
2. TUI remains adapter-first: it renders state and emits explicit actions, but does not own lifecycle settlement.
3. Headless remains the authoritative runtime-evidence lane; TUI validates adapter behavior, not backend ownership.
4. Deferred inventory and intentional non-goal buckets stay explicit and non-blocking unless promoted by backlog/roadmap updates.

## Milestone sequence (planning outputs)

Prerequisite for direct test-binary filters: run `just cpp-configure cpp-debug && just cpp-build cpp-debug` first. Commands below are Catch2 test filters run from the repo root; if a shell does not match due to quoting, retry with single quotes.

| Milestone | Focus | Acceptance criteria | Validation lane (target commands) |
|---|---|---|---|
| **M17** | Interactive lifecycle terminal-state closure | 1) approval/question/plan requests enter `pending` and settle to `resolved`/`cancelled`/`timeout` through orchestration-owned APIs; 2) `run_id` + `request_id` correlation is stable across state transitions; 3) stale-request rejection and idempotent terminal handling are enforced. | `./build/cpp/debug/tests/ava_cpp_tests "interactive request store tracks pending and terminal lifecycle"`; `./build/cpp/debug/tests/ava_cpp_tests "interactive request store rejects stale and non-existent requests"`; `./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge tracks approval/question/plan request lifecycle"`; `./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge terminal cancelled/timedout outcomes are fail-closed"`; `./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge settle is idempotent for same terminal state and rejects mismatched resettle"`; `./build/cpp/debug/tests/ava_orchestration_tests "runtime composition interactive bridge correlates approvals to active run id"` |
| **M18** | TUI adapter action harness | 1) Add focused adapter-action harness for in-TUI `approve`/`reject`/`answer`/`cancel-question`/`accept-plan`/`reject-plan`; 2) every adapter action carries a `request_id`, stale/missing IDs fail through the bridge, and TUI state clears only from bridge/backend pending snapshots; 3) harness proves actions dispatch to orchestration-owned stores and TUI does not become lifecycle owner; 4) existing slash/history/navigation/pending-visibility tests remain green. | `./build/cpp/debug/tests/ava_tui_tests "tui state tracks adapter-facing interactive request visibility and clearing"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action approve resolves pending approval via bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action reject cancels pending approval via bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action answer carries request_id to bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action cancel-question cancels pending question via bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action accept-plan delegates to orchestration bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action reject-plan cancels pending plan via bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects stale or missing request id through bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects unknown action kind"`; `./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects unavailable bridge"`; `./build/cpp/debug/tests/ava_tui_tests "tui state clears interactive request only on backend clear event"`. |
| **M19** | Child-run cancellation + bounded watchdog/arbitration closure | 1) Orchestration exposes active child-run visibility/listing and explicit child cancellation requests; 2) deterministic child terminal summary metadata is surfaced to parent flow and projected to the TUI as observer state; 3) one bounded RunController-owned watchdog rule is specified with deadline trigger, non-goals, and targeted fail-closed harness coverage. | `./build/cpp/debug/tests/ava_agent_tests "agent runtime emits streaming assistant deltas with run_id"`; `./build/cpp/debug/tests/ava_agent_tests "agent runtime exits cooperatively when cancelled during streaming"`; `./build/cpp/debug/tests/ava_agent_tests "agent runtime cancels before tool execution after streamed assistant text"`; `./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled before tool execution"`; `./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled during streaming"`; `./build/cpp/debug/tests/ava_orchestration_tests "run controller issues unique run leases and cooperative cancellation state"`; `./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner runs child sessions"`; `./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner enforces spawn budget"`; `./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads interactive resolvers into child composition"`; `./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner exposes active child runs for cancellation"`; `./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner watchdog timeout surfaces deterministic terminal summary"`; `./build/cpp/debug/tests/ava_tui_tests "tui state projects child-run terminal metadata without owning lifecycle"`; `just cpp-test cpp-debug -R "ava_app_integration" --output-on-failure`. |
| **M20** | Contract-conformance closure + deferred-inventory guardrails | 1) Completion-evidence lane runs all M17-M19 focused filters plus focused M20 closure tests for remaining completion-critical contract rows; 2) missing broader evidence is explicitly deferred or exception-tracked before any completion claim; 3) deferred RP-4A/RP-4B buckets have documented guardrails; 4) completion-claim wording cannot expand without bucket reclassification + docs updates. | `./build/cpp/debug/tests/ava_app_tests "ndjson tool call and result correlate call_id"`; `./build/cpp/debug/tests/ava_app_tests "resume by id preserves tool heavy message metadata"`; `./build/cpp/debug/tests/ava_agent_tests "agent runtime cancellation preserves session transcript integrity"`; `./build/cpp/debug/tests/ava_app_integration_tests "headless auto approve rejects dangerous mutating tool"`; `./build/cpp/debug/tests/ava_app_integration_tests "headless scripted tool loop executes tool and persists transcript"`; `./build/cpp/debug/tests/ava_tools_tests "edit no match returns error without mutating file"`; `just cpp-test cpp-debug --output-on-failure`; `git diff --check`. |

M19 watchdog scope is deliberately narrow: orchestration/`RunController` owns the rule, the first closure target is a single explicit trigger and fail-closed terminal behavior, and broad async scheduler/background runtime parity remains deferred. The TUI may observe child-run terminal metadata and offer actions, but it must not settle child lifecycle state locally.

## Decision points

Decision points are answered in the milestone closure PR. If any answer changes scope or creates/changes an exception, the PR description must record the decision and update the relevant audit/exception/backlog docs before merge.

1. **DP-1 (after M17):** Resolved in `cpp/MILESTONE17_BOUNDARIES.md`; no new adapter exception was required for terminal-state closure.
2. **DP-2 (after M18):** Resolved in `cpp/MILESTONE18_BOUNDARIES.md`; the adapter-action harness is sufficient for scoped RP-2 evidence, while full modal/widget parity remains deferred.
3. **DP-3 (after M19):** Resolved in `cpp/MILESTONE19_BOUNDARIES.md`; scoped child-run cancellation/watchdog evidence is sufficient for the current roadmap, while broad async scheduler parity, hard provider interruption, and richer child-run UI remain deferred.
4. **DP-4 (after M20):** Resolved in `cpp/MILESTONE20_BOUNDARIES.md`; scoped completion-critical backend/headless/TUI gates are closed without promoting deferred buckets, with canonical `subagent_complete` NDJSON parity explicitly tracked as `EX-004`.

## Deferred and non-goal buckets

Deferred inventory (not completion-blocking by default):

1. MCP/plugin/tool-extension breadth beyond the current core tool/runtime seams.
2. Provider/auth/config long-tail breadth (keychain/OAuth/device/browser-login expansion beyond current baseline).
3. Full Rust edit-strategy parity and broad async/background runtime parity.

Deferred-inventory guardrail command:

```bash
rg -n "MCP|plugin|OAuth|keychain|web|desktop|full Rust edit|async|hard-kill" cpp/MILESTONE*.md docs/architecture/cpp-backend-*.md
```

Matches for those terms must remain in deferred, out-of-scope, or intentional non-goal contexts unless backlog scope is explicitly promoted first.

Intentional non-goals (unless scope is explicitly promoted):

1. Web (`ava-web`) and desktop/Tauri parity expansion as a completion gate.
2. Full Rust TUI visual/modal parity.
3. Broad MCP/provider feature parity claims.

## Recommended first implementation slice

Start with **M17 Interactive lifecycle terminal-state closure**:

1. Lock orchestration-owned terminal-state transition table (`resolved`/`cancelled`/`timeout`) for approval/question/plan.
2. Add/confirm idempotent terminal-state handling + stale request rejection in one narrow seam.
3. Keep TUI changes out of this slice except wiring needed to observe existing bridge outcomes.
4. Close with focused M17 tests before opening M18 adapter-action harness work.

Rationale: this removes the highest-risk ownership ambiguity first and creates a stable base for the M18 adapter-action harness.

## Documentation update rules

1. Update this roadmap whenever M17-M20 scope, acceptance criteria, or decision outcomes change.
2. Update `docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md` when completion-critical/deferred/non-goal bucket status changes.
3. Update `docs/architecture/cpp-backend-tui-parity-contract-audit-post-m16.md` when contract evidence or closure traces change.
4. Update `docs/architecture/backend-contract-exceptions.md` for any new/changed exception entry.
5. Update `docs/project/backlog.md` and `CHANGELOG.md` whenever a roadmap milestone is opened, re-scoped, or closed.
6. If a deferred or non-goal item is promoted, update the backlog scope first, then update both post-M16 audit artifacts before implementation proceeds.

## Lightweight docs validation commands

```bash
git diff --check
rg -n "M17|M18|M19|M20|Decision points|Deferred and non-goal buckets|Recommended first implementation slice" docs/architecture/cpp-backend-headless-tui-migration-completion-roadmap-post-m16-m17-plus.md
rg -n "completion roadmap|M17|M18|M19|M20" docs/architecture/README.md docs/project/backlog.md CHANGELOG.md
```
