---
title: "C++ Backend/TUI Migration Completion Gap Audit (Post-M16)"
description: "Planning-only audit of remaining backend/TUI migration completion gaps after C++ Milestone 16."
order: 13
updated: "2026-04-23"
---

# C++ Backend/TUI Migration Completion Gap Audit (Post-M16)

This planning-only artifact captures durable backend/TUI migration-completion gaps after M16.

It includes both immediate M14-M16 findings and carry-forward deferred scope from earlier milestones/planning so plugin/MCP/provider breadth and web/desktop scope are not implicitly treated as completion-critical.

## Planning Findings Sources and Provenance

Primary post-M16 boundary sources:

1. `cpp/MILESTONE14_BOUNDARIES.md`
2. `cpp/MILESTONE15_BOUNDARIES.md`
3. `cpp/MILESTONE16_BOUNDARIES.md`

Carry-forward sources referenced by this audit where applicable:

4. `cpp/MILESTONE4_BOUNDARIES.md`
5. `cpp/MILESTONE5_BOUNDARIES.md`
6. `cpp/MILESTONE6_BOUNDARIES.md`
7. `cpp/MILESTONE13_BOUNDARIES.md`
8. `docs/architecture/cpp-backend-tui-migration-plan-m1.md`

## Scope and Non-Goals

Scope:

1. Capture migration-completion gaps after M16 for backend/headless/TUI runtime behavior.
2. Keep ownership boundaries explicit (runtime/orchestration owns lifecycle; TUI remains adapter-first).
3. Define first research slices and concrete evidence commands for follow-up milestone planning.
4. Keep completion claims narrow: minimum backend/headless/TUI semantics needed for completion, not full Rust parity.

Non-goals:

1. No code changes in this milestone.
2. No web (`ava-web`) or desktop/Tauri migration expansion.
3. No broad async/background runtime rewrite in one pass.
4. No replacement of accepted milestone boundary docs; this audit is a completion-gap overlay.

## Gap Classification Buckets

1. **Completion-critical**: required before calling backend/TUI migration complete under the current scoped claim.
2. **Deferred inventory**: explicitly tracked deferred breadth; not completion-blocking by default.
3. **Intentional non-goal**: out of scope for current backend/TUI migration completion unless roadmap/backlog scope changes.

## Current M16 State

1. Foundations through M13 are in place, including shared runtime composition ownership and native blocking subagent execution baseline.
2. M14 added the control-plane interactive request lifecycle seam plus orchestration bridge wiring for approval/question/plan.
3. M15 added per-run identity and narrow foreground streaming/cancellation ownership seams.
4. M16 added pragmatic TUI workflow parity basics (`/help`, `/clear`, `/model`, graceful unsupported `/compact`, input history, message navigation/status visibility, and pending interactive-request visibility/clearing seams).
5. Carry-forward deferred scope still applies from earlier milestones/planning (auth/config breadth, provider breadth, plugin/MCP breadth, and web/desktop non-goal surfaces).

## Subsystem Gap Matrix (Classified + Traceable)

| Subsystem | Bucket | Current baseline | Remaining gap | Provenance | Priority | Evidence / test target |
|---|---|---|---|---|---|---|
| Interactive lifecycle (`approval`/`question`/`plan`) | completion-critical | Request-store + orchestration bridge + `run_id` correlation are implemented | Close minimal adapter-driven terminal-state flows (`pending -> resolved/cancelled/timeout`) without moving ownership into TUI. **Done when:** approval/question/plan can each enter `pending` and settle to `resolved`/`cancelled`/`timeout` with stable `run_id` + `request_id` correlation and idempotent terminal-state handling. | M1 freeze interactive semantics + M14/M15/M16 boundary docs | RP-1 | `interactive request store tracks pending and terminal lifecycle`; `interactive bridge tracks approval/question/plan request lifecycle`; `interactive bridge terminal cancelled/timedout outcomes are fail-closed` |
| TUI workflow parity (core operator flow) | completion-critical | Slash-command/history/navigation/status/pending-summary seams are implemented | Add narrow in-TUI interactive actions and deterministic state/event transitions (not full widget/visual parity). **Done when:** existing focused `ava_tui_tests` state/event coverage for submit/slash/history/navigation/status + pending-request visibility passes, and the future adapter-action harness for in-TUI approve/reject/answer/accept-plan actions is added and passing before RP-2 can close. | M1 plan Phase 4 + M16 deferred notes | RP-2 | `tui state slash commands provide help/clear/model and unsupported compact handling`; `tui state keeps input history and restores draft with up/down`; `tui state reports message navigation status and supports top/bottom jumps`; `tui state tracks adapter-facing interactive request visibility and clearing` |
| Runtime scheduling/cancellation semantics | completion-critical | Foreground cooperative cancellation + streaming delta seams are wired | Define minimum background arbitration/watchdog semantics needed for completion claim. **Done when:** existing cancellation/run-lease coverage (`agent runtime emits streaming assistant deltas with run_id`; `agent runtime exits cooperatively when cancelled during streaming`; `agent runtime cancels before tool execution after streamed assistant text`; `run controller issues unique run leases and cooperative cancellation state`) passes, and future targeted child-run cancellation + watchdog/arbitration harness coverage is added and passing before RP-3 can close. | M14/M15 deferred background/watchdog + async notes | RP-3 | `agent runtime emits streaming assistant deltas with run_id`; `agent runtime exits cooperatively when cancelled during streaming`; `agent runtime cancels before tool execution after streamed assistant text`; `run controller issues unique run leases and cooperative cancellation state` |
| Orchestration/subagent lifecycle controls | completion-critical | Native blocking child-run baseline with depth/spawn-budget and metadata exists | Add externally addressable child-run cancellation/visibility seam (narrow scope). **Done when:** parent context can list active child runs, request child cancellation through orchestration-owned APIs, and surface deterministic child terminal summary metadata/events back to the parent flow, with RP-3 closure still requiring the existing cancellation/run-lease coverage plus future targeted child-run cancellation + watchdog/arbitration harness coverage to pass. | M13 deferred async/background subagent ownership + M15 child-run cancel deferral | RP-3 | `native blocking task spawner runs child sessions`; `native blocking task spawner enforces spawn budget`; `native blocking task spawner threads interactive resolvers into child composition`; `ndjson event carries run_id and streaming delta payload` |
| Tool/runtime extension breadth (MCP/plugin/optional surfaces) | deferred inventory | Core local tool stack + permission seam are active | Keep explicit inventory/closure buckets; do not treat MCP/plugin/web-browser breadth as completion-critical by default | M1 out-of-scope + M6/M13/M14/M15 deferred MCP/plugin notes | RP-4A | `default tools registration includes milestone 6 core set`; `read/write/edit reject symlink escapes outside workspace`; `git and git_read execute read-only git commands` + deferred MCP/plugin/tool-extension inventory checklist |
| Provider/auth/config long-tail breadth | deferred inventory | Foundational config/session + one production provider path are active | Keep keychain/OAuth/device/browser flows, YAML/TOML breadth, and long-tail provider parity in deferred inventory unless scope is promoted | M1 out-of-scope + M4/M5 deferred notes | RP-4B | `credential env overrides take precedence`; `credentials and trust stores use owner-only permissions on posix`; `factory selects openai and stubs deferred providers`; `native blocking task spawner threads credentials override into child composition` + deferred provider/auth/config inventory checklist |
| Web/Desktop parity expansion | intentional non-goal | Backend/headless/TUI migration remains the active scope | No completion dependency for current claim; revisit only if roadmap/backlog scope changes | M1 scope/out-of-scope + M4 non-expansion note | — | No backend/TUI completion gate target |

## First Research Priorities

1. **RP-1 Interactive lifecycle contract pass:** lock adapter/runtime responsibilities for approval/question/plan terminal-state ownership.
2. **RP-2 TUI workflow parity slice plan:** sequence only highest-value operator-flow gaps into narrow, testable increments.
3. **RP-3 Background lifecycle semantics pass:** define minimum arbitration/watchdog/cancellation behavior required for completion claims.
4. **RP-4A Deferred tool/runtime-extension inventory pass:** keep MCP/plugin/tool-extension breadth explicitly bucketed and evidence-linked without forcing full parity.
5. **RP-4B Deferred provider/auth/config inventory pass:** keep provider/auth/config breadth explicitly bucketed and evidence-linked without forcing full parity.

## Evidence and Validation Commands

Docs-only validation (this milestone):

```bash
rg -n "Explicitly Deferred|out of scope|interactive|watchdog|plugin|MCP|provider|OAuth|keychain|web|desktop" cpp/MILESTONE4_BOUNDARIES.md cpp/MILESTONE5_BOUNDARIES.md cpp/MILESTONE6_BOUNDARIES.md cpp/MILESTONE13_BOUNDARIES.md cpp/MILESTONE14_BOUNDARIES.md cpp/MILESTONE15_BOUNDARIES.md cpp/MILESTONE16_BOUNDARIES.md docs/architecture/cpp-backend-tui-migration-plan-m1.md
rg -n "completion-critical|deferred inventory|intentional non-goal|Done when|RP-1|RP-2|RP-3|RP-4A|RP-4B" docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md
```

Follow-up implementation validation slices (not required for this docs-only milestone):

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug

# RP-1: interactive lifecycle terminal-state and correlation behavior
ctest --preset cpp-debug -R "ava_cpp_unit|ava_orchestration_unit" --output-on-failure
./build/cpp/debug/tests/ava_cpp_tests "interactive request store tracks pending and terminal lifecycle"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge tracks approval/question/plan request lifecycle"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge terminal cancelled/timedout outcomes are fail-closed"

# RP-2: TUI operator-flow parity slice
ctest --preset cpp-debug -R "ava_tui_unit" --output-on-failure
./build/cpp/debug/tests/ava_tui_tests "tui state slash commands provide help/clear/model and unsupported compact handling"
./build/cpp/debug/tests/ava_tui_tests "tui state keeps input history and restores draft with up/down"
./build/cpp/debug/tests/ava_tui_tests "tui state reports message navigation status and supports top/bottom jumps"
./build/cpp/debug/tests/ava_tui_tests "tui state tracks adapter-facing interactive request visibility and clearing"
# TODO(RP-2): Future required before closing RP-2 — add a focused adapter-action harness for in-TUI approve/reject/answer/accept-plan actions.

# RP-3: cancellation/run-lease and child-run lifecycle controls
ctest --preset cpp-debug -R "ava_agent_unit|ava_orchestration_unit|ava_app_unit" --output-on-failure
./build/cpp/debug/tests/ava_agent_tests "agent runtime emits streaming assistant deltas with run_id"
./build/cpp/debug/tests/ava_agent_tests "agent runtime exits cooperatively when cancelled during streaming"
./build/cpp/debug/tests/ava_agent_tests "agent runtime cancels before tool execution after streamed assistant text"
./build/cpp/debug/tests/ava_orchestration_tests "run controller issues unique run leases and cooperative cancellation state"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner runs child sessions"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner enforces spawn budget"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads interactive resolvers into child composition"
./build/cpp/debug/tests/ava_app_tests "ndjson event carries run_id and streaming delta payload"
# TODO(RP-3): Future required before closing RP-3 — add targeted child-run cancellation + watchdog/arbitration harness coverage.

# RP-4A: deferred MCP/plugin/tool-extension inventory guardrails
ctest --preset cpp-debug -R "ava_tools_unit" --output-on-failure
./build/cpp/debug/tests/ava_tools_tests "default tools registration includes milestone 6 core set"
./build/cpp/debug/tests/ava_tools_tests "read/write/edit reject symlink escapes outside workspace"
./build/cpp/debug/tests/ava_tools_tests "git and git_read execute read-only git commands"
# TODO(RP-4A): add an inventory consistency harness that fails when deferred MCP/plugin buckets drift undocumented.

# RP-4B: deferred provider/auth/config inventory guardrails
ctest --preset cpp-debug -R "ava_cpp_unit|ava_llm_unit|ava_orchestration_unit" --output-on-failure
./build/cpp/debug/tests/ava_cpp_tests "credential env overrides take precedence"
./build/cpp/debug/tests/ava_cpp_tests "credentials and trust stores use owner-only permissions on posix"
./build/cpp/debug/tests/ava_llm_tests "factory selects openai and stubs deferred providers"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads credentials override into child composition"
# TODO(RP-4B): add targeted keychain/OAuth/device/browser-login harness coverage when promoted from deferred inventory.
```
