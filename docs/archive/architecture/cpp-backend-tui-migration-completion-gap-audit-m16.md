---
title: "C++ Backend/TUI Migration Completion Gap Audit (Post-M16)"
description: "Planning-only audit of remaining backend/TUI migration completion gaps after C++ Milestone 16."
order: 13
updated: "2026-04-24"
---

# C++ Backend/TUI Migration Completion Gap Audit (Post-M16)

This planning-only artifact captures durable backend/TUI migration-completion gaps after M16.

It includes both immediate M14-M16 findings and carry-forward deferred scope from earlier milestones/planning so plugin/MCP/provider breadth and web/desktop scope are not implicitly treated as completion-critical.

## Planning Findings Sources and Provenance

Primary post-M16 boundary sources:

1. `cpp/MILESTONE14_BOUNDARIES.md`
2. `cpp/MILESTONE15_BOUNDARIES.md`
3. `cpp/MILESTONE16_BOUNDARIES.md`
4. `cpp/MILESTONE17_BOUNDARIES.md`
5. `cpp/MILESTONE18_BOUNDARIES.md`
6. `cpp/MILESTONE19_BOUNDARIES.md`
7. `cpp/MILESTONE20_BOUNDARIES.md`

Carry-forward sources referenced by this audit where applicable:

5. `cpp/MILESTONE4_BOUNDARIES.md`
6. `cpp/MILESTONE5_BOUNDARIES.md`
7. `cpp/MILESTONE6_BOUNDARIES.md`
8. `cpp/MILESTONE13_BOUNDARIES.md`
9. `docs/architecture/cpp-backend-tui-migration-plan-m1.md`

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
| Interactive lifecycle (`approval`/`question`/`plan`) | completion-critical | Request-store + orchestration bridge + `run_id` correlation are implemented; M17 adds explicit stale/non-existent request-store rejection coverage and M18 adds adapter-action evidence without moving lifecycle ownership into TUI | Closed for scoped backend/headless/TUI completion. Full modal/widget parity remains deferred. | M1 freeze interactive semantics + M14/M15/M16/M17/M18 boundary docs | RP-1 | `interactive request store tracks pending and terminal lifecycle`; `interactive request store rejects stale and non-existent requests`; `interactive bridge tracks approval/question/plan request lifecycle`; `interactive bridge terminal cancelled/timedout outcomes are fail-closed`; M18 adapter-action filters |
| TUI workflow parity (core operator flow) | completion-critical | Slash-command/history/navigation/status/pending-summary seams are implemented; M18 adds request-id-bearing approve/reject/answer/accept-plan/cancel-question/reject-plan adapter-action evidence that settles through `InteractiveBridge` while `AppState` remains display-only; M26 adds a bounded inline approval/question/plan dock UX over the same adapter seam with minimal payload previews, cancellation-aware blocking resolvers, and disabled approval for truncated tool payloads | Closed for scoped backend/headless/TUI completion plus the M26 operator-flow increment. Full Rust modal/widget parity and polished request-payload rendering remain deferred. | M1 plan Phase 4 + M16/M18/M26 boundary notes | RP-2 | `tui state slash commands provide help/clear/model and unsupported compact handling`; `tui state keeps input history and restores draft with up/down`; `tui state reports message navigation status and supports top/bottom jumps`; `tui state tracks adapter-facing interactive request visibility and clearing`; `tui state opens interactive dock for pending requests by priority`; `tui state ignores terminal interactive handles and renders request details`; `tui state builds dock actions for approval question and plan`; `tui state applies dock adapter result without clearing backend-owned visibility`; `tui adapter action approve resolves pending approval via bridge`; `tui adapter action reject cancels pending approval via bridge`; `tui adapter action answer carries request_id to bridge`; `tui adapter action cancel-question cancels pending question via bridge`; `tui adapter action accept-plan delegates to orchestration bridge`; `tui adapter action reject-plan cancels pending plan via bridge`; `tui adapter action rejects stale or missing request id through bridge`; `tui adapter action rejects unknown action kind`; `tui adapter action rejects unavailable bridge` |
| Runtime scheduling/cancellation semantics | completion-critical | Foreground cooperative cancellation + streaming delta seams are wired; M19 adds optional RunController per-run deadlines where deadline expiry participates in cooperative cancellation and produces deterministic watchdog timeout summaries; M20 adds cancellation transcript-integrity coverage; M24 adds parent-to-child cooperative cancellation propagation plus headless signal-to-cancel wiring | Closed for scoped backend/headless/TUI completion. Broad async/background scheduler parity and hard-kill provider interruption remain deferred. | M14/M15 deferred background/watchdog + async notes + M19/M20/M24 boundary docs | RP-3 | `agent runtime emits streaming assistant deltas with run_id`; `agent runtime exits cooperatively when cancelled during streaming`; `agent runtime cancels before tool execution after streamed assistant text`; `agent runtime preserves tool-call-only assistant message when cancelled before tool execution`; `agent runtime preserves tool-call-only assistant message when cancelled during streaming`; `agent runtime cancellation preserves session transcript integrity`; `run controller issues unique run leases and cooperative cancellation state`; `native blocking task spawner watchdog timeout surfaces deterministic terminal summary`; `native blocking task spawner propagates parent cancellation into child run`; `headless signal cancellation bridge records cancellation requests` |
| Orchestration/subagent lifecycle controls | completion-critical | Native blocking child-run baseline with depth/spawn-budget and metadata exists; M19 adds active child-run listing/lookup/cancel APIs, deterministic child terminal summaries, persisted `metadata.orchestration.subagent_run`, and TUI observer projection | Closed for scoped backend/headless/TUI completion. Canonical headless `subagent_complete` NDJSON parity is tracked as `EX-004`; full async/background subagent parity remains deferred. | M13 deferred async/background subagent ownership + M15 child-run cancel deferral + M19/M20 boundary docs | RP-3 | `native blocking task spawner runs child sessions`; `native blocking task spawner enforces spawn budget`; `native blocking task spawner threads interactive resolvers into child composition`; `native blocking task spawner exposes active child runs for cancellation`; `native blocking task spawner watchdog timeout surfaces deterministic terminal summary`; `tui state projects child-run terminal metadata without owning lifecycle` |
| Tool/runtime extension breadth (MCP/plugin/optional surfaces) | deferred inventory | Core local tool stack + permission seam are active; M25 adds a narrow `ava_mcp` protocol/config/client foundation without runtime tool registration | Keep explicit inventory/closure buckets; do not treat MCP/plugin/web-browser breadth as completion-critical by default. MCP stdio process spawning, tool-registry integration, HTTP/SSE/OAuth, custom TOML tools, and plugin runtime remain deferred. | M1 out-of-scope + M6/M13/M14/M15/M25 deferred MCP/plugin notes | RP-4A | `default tools registration includes milestone 6 core set`; `read/write/edit reject symlink escapes outside workspace`; `git and git_read execute read-only git commands`; `mcp client runs initialize list tools and call tool flow`; `mcp config parses stdio servers and rejects unsupported transports` + deferred MCP/plugin/tool-extension inventory checklist |
| Provider/auth/config long-tail breadth | deferred inventory | Foundational config/session + two scoped production provider paths are active (`openai` + `anthropic`, both CPR-gated for live HTTP transport) | Keep keychain/OAuth/device/browser flows, YAML/TOML breadth, Anthropic streaming parity, and long-tail provider parity in deferred inventory unless scope is promoted | M1 out-of-scope + M4/M5/M23 deferred notes | RP-4B | `credential env overrides take precedence`; `credentials and trust stores use owner-only permissions on posix`; `factory selects openai + anthropic and stubs deferred providers`; `native blocking task spawner threads credentials override into child composition` + deferred provider/auth/config inventory checklist |
| Web/Desktop parity expansion | intentional non-goal | Backend/headless/TUI migration remains the active scope | No completion dependency for current claim; revisit only if roadmap/backlog scope changes | M1 scope/out-of-scope + M4 non-expansion note | — | No backend/TUI completion gate target |

M20 closes the completion-critical rows above for the scoped backend/headless/TUI claim. Deferred inventory and intentional non-goal rows remain tracked guardrails, not completion blockers.

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
just cpp-test cpp-debug -R "ava_cpp_unit|ava_orchestration_unit" --output-on-failure
./build/cpp/debug/tests/ava_cpp_tests "interactive request store tracks pending and terminal lifecycle"
./build/cpp/debug/tests/ava_cpp_tests "interactive request store rejects stale and non-existent requests"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge tracks approval/question/plan request lifecycle"
./build/cpp/debug/tests/ava_orchestration_tests "interactive bridge terminal cancelled/timedout outcomes are fail-closed"

# RP-2: TUI operator-flow parity slice
just cpp-test cpp-debug -R "ava_tui_unit" --output-on-failure
./build/cpp/debug/tests/ava_tui_tests "tui state slash commands provide help/clear/model and unsupported compact handling"
./build/cpp/debug/tests/ava_tui_tests "tui state keeps input history and restores draft with up/down"
./build/cpp/debug/tests/ava_tui_tests "tui state reports message navigation status and supports top/bottom jumps"
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

# RP-3: cancellation/run-lease and child-run lifecycle controls
just cpp-test cpp-debug -R "ava_agent_unit|ava_orchestration_unit|ava_app_unit" --output-on-failure
./build/cpp/debug/tests/ava_agent_tests "agent runtime emits streaming assistant deltas with run_id"
./build/cpp/debug/tests/ava_agent_tests "agent runtime exits cooperatively when cancelled during streaming"
./build/cpp/debug/tests/ava_agent_tests "agent runtime cancels before tool execution after streamed assistant text"
./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled before tool execution"
./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled during streaming"
./build/cpp/debug/tests/ava_orchestration_tests "run controller issues unique run leases and cooperative cancellation state"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner runs child sessions"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner enforces spawn budget"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads interactive resolvers into child composition"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner exposes active child runs for cancellation"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner watchdog timeout surfaces deterministic terminal summary"
./build/cpp/debug/tests/ava_tui_tests "tui state projects child-run terminal metadata without owning lifecycle"
./build/cpp/debug/tests/ava_app_tests "ndjson event carries run_id and streaming delta payload"
./build/cpp/debug/tests/ava_app_tests "ndjson tool call and result correlate call_id"
./build/cpp/debug/tests/ava_app_tests "resume by id preserves tool heavy message metadata"
./build/cpp/debug/tests/ava_agent_tests "agent runtime cancellation preserves session transcript integrity"
./build/cpp/debug/tests/ava_app_integration_tests "headless auto approve rejects dangerous mutating tool"
./build/cpp/debug/tests/ava_app_integration_tests "headless scripted tool loop executes tool and persists transcript"
./build/cpp/debug/tests/ava_tools_tests "edit no match returns error without mutating file"

# RP-4A: deferred MCP/plugin/tool-extension inventory guardrails
just cpp-test cpp-debug -R "ava_tools_unit" --output-on-failure
./build/cpp/debug/tests/ava_tools_tests "default tools registration includes milestone 6 core set"
./build/cpp/debug/tests/ava_tools_tests "read/write/edit reject symlink escapes outside workspace"
./build/cpp/debug/tests/ava_tools_tests "git and git_read execute read-only git commands"
# TODO(RP-4A): add an inventory consistency harness that fails when deferred MCP/plugin buckets drift undocumented.

# RP-4B: deferred provider/auth/config inventory guardrails
just cpp-test cpp-debug -R "ava_cpp_unit|ava_llm_unit|ava_orchestration_unit" --output-on-failure
./build/cpp/debug/tests/ava_cpp_tests "credential env overrides take precedence"
./build/cpp/debug/tests/ava_cpp_tests "credentials and trust stores use owner-only permissions on posix"
./build/cpp/debug/tests/ava_llm_tests "factory selects openai + anthropic and stubs deferred providers"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads credentials override into child composition"
# TODO(RP-4B): add targeted keychain/OAuth/device/browser-login harness coverage when promoted from deferred inventory.
```
