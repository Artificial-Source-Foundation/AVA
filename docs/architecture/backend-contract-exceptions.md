---
title: "Backend Contract Exceptions"
description: "Versioned registry of intentional adapter-specific exceptions to the canonical shared-backend contract."
order: 11
updated: "2026-04-21"
---

# Backend Contract Exceptions

Purpose: record intentional, bounded exceptions to the canonical shared-backend contract so adapter-specific behavior cannot drift silently.

Current status note (2026-04-16):

1. After the desktop/web/TUI normalization work, these documented exceptions are the remaining intentional cross-surface differences still relevant at the current scope.
2. Anything not listed here should be treated as a bug or regression rather than acceptable adapter-specific behavior.

Owner:

- Backend contract owner for `crates/ava-control-plane/src/` and backend-only shims in `crates/ava-agent/src/control_plane/`

Rules:

1. Every exception must name the impacted contract area.
2. Every exception must name impacted adapters.
3. Every exception must include rationale, risk, owner, and expiry/removal trigger.
4. Every exception must include test coverage proving bounded behavior.
5. No adapter may rely on an undocumented exception.

## Active exceptions

### EX-001 Headless non-interactive interactive-resolution bypass

- Contract area: approval/question/plan lifecycle
- Impacted adapters: headless CLI
- Rationale: headless is a scoped non-interactive execution path and cannot rely on live user prompts.
- Current behavior:
  - tool approvals may auto-resolve only for non-dangerous approval requests
  - dangerous approval-requiring actions are rejected/fail closed instead of waiting for interactive approval
  - interactive question/plan UX parity is not required
- Risk: unattended runs still diverge from interactive approval UX, but the exception is now bounded to non-dangerous auto-resolution rather than blanket approval.
- Owner: backend contract owner
- Expiry/removal trigger: replace current headless-specific bypasses with a fully contract-owned non-interactive policy module, or explicitly ratify the long-term headless policy in a later contract revision.
- Required tests:
  - headless non-interactive conformance test
  - no-TTY operation test
  - scoped exception behavior documented and asserted

### EX-002 Desktop completion-bound command calls for run-start/replay actions

- Contract area: command completion mode for `submit_goal`, `retry_last_message`, `edit_and_resend`, `regenerate_response`
- Impacted adapters: desktop (Tauri IPC command transport)
- Rationale: current desktop `invoke` contract resolves these commands after `run_agent_inner(...)` completes, while web/headless paths already return accepted run handles and stream lifecycle events.
- Current behavior:
  - desktop command calls above are completion-bound at command-return time
  - run lifecycle events still stream during execution via `agent-event`
  - `SubmitGoalResult.success` and `SubmitGoalResult.turns` must not be treated as canonical completion state across adapters; they are acceptance placeholders on web accepted-and-streaming paths and terminal fields on desktop until this exception is removed
- Risk: adapter consumers may assume accepted-and-streaming parity and incorrectly treat desktop command return as an early acceptance signal.
- Owner: backend contract owner + desktop runtime owner
- Expiry/removal trigger: desktop command handlers are switched to accepted-and-streaming return semantics with matching run/session metadata and regression coverage.
- Required tests:
  - command-contract fixture marks canonical mode and explicit desktop exception
  - desktop/Tauri wrapper timing tests assert current completion-bound behavior remains intentional until exception removal
  - cross-surface conformance test guards web accepted-and-streaming behavior for the same command family

### EX-003 Manual `/compact` remains adapter-local and does not consume per-run run-context metadata

- Contract area: compaction invocation semantics and per-run context reuse
- Impacted adapters: interactive TUI, headless CLI slash-command path
- Rationale: desktop and web submit/replay flows now reuse the same persisted per-run thinking/model/auto-compaction context shape, but manual `/compact` in TUI/headless still runs a local heuristic compaction path rather than reusing persisted `runContext` metadata or adapter submit/replay semantics.
- Current behavior:
  - per-run `submit_goal` options (`thinkingLevel`, provider/model override, `autoCompact`, compaction threshold/model override) now flow through desktop and web run-start paths
  - desktop and web replay flows now rehydrate the session's persisted `runContext` metadata before launching retry/edit/regenerate runs
  - manual `/compact` in TUI/headless remains a separate adapter-local command based on local message condensation
  - manual `/compact` does not currently rehydrate/apply the session's last persisted per-run compaction model override or auto-compaction threshold metadata
- Risk: users can reasonably assume a manual `/compact` command will use the same compaction model/settings as the run that created the session, but today that assumption is only valid for submit-triggered auto-compaction, not the manual slash command.
- Owner: backend contract owner + TUI/headless runtime owner
- Expiry/removal trigger: move manual `/compact` behind a shared backend-owned compaction invocation contract that can consume persisted run-context metadata (or explicitly redesign the command surface and document the long-term split as canonical instead of exceptional).
- Required tests:
  - focused web/desktop submit parity regressions proving per-run context is honored on run start
  - focused web replay regressions proving retry/edit/regenerate reuse persisted `runContext` metadata
  - TUI/headless slash-command tests that keep the current adapter-local `/compact` behavior explicit until the exception is removed
