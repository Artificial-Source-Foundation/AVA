---
title: "Backlog"
description: "Active AVA 0.6 work for the V1 push, plus an archive of the previous detailed backlog."
order: 3
updated: "2026-04-19"
---

# AVA Backlog

This backlog now tracks the active `0.6 -> V1` checklist first.

The older `3.3 / 3.3.1` planning backlog is preserved below in archived form so the current priority stays easy to read.

Source of truth for direction: `docs/project/roadmap.md`

## Active Now

1. Polish the desktop app until it feels ready for daily use, especially chat layout, settings, tool cards, and overall fit and finish.
2. Make sure every core tool and important agent action has clear UI so users can understand what AVA is doing without reading logs.
3. Verify multi-chat behavior end to end so users can switch sessions while runs continue without losing state, mixing outputs, or breaking approvals/plans.
4. Prove the backend can do real coding work reliably in a **headless-first** flow (authoritative for backend correctness under the current scoped non-interactive exception), with TUI, desktop, and web checks to confirm lighter surface parity only (not full interactive approval/question/plan proof by itself).
5. Add a simple automated product smoke suite for the core journey: prompt -> tools -> edit -> verify -> persist.
6. Keep a repeatable AVA-vs-OpenCode comparison path so quality can be measured, not argued.
7. Finish the docs reset around the `0.6` story so roadmap, backlog, README, and release language all describe the same product stage.

Current bounded parity note: web submit and replay flows now honor the shared persisted per-run thinking/model/compaction context used by desktop session runs; the remaining bounded divergence is the manual TUI/headless `/compact` path tracked in `docs/architecture/backend-contract-exceptions.md` as `EX-003`.

Current web hardening note: `ava serve` now defaults to loopback-only bind/origin exposure, token-protects sensitive session/history/status reads plus persisted plan listing/loading routes, high-risk plugin/CLI discovery + plugin route surfaces, and privileged HTTP control-plane routes (and `/ws`), redacts raw control tokens from normal logs, and still keeps broader browser-origin exposure as an explicit `--insecure-open-cors` opt-in.

Current multi-chat correctness note: overlapping frontend session switches now gate async persisted-session finalization on the winning switch/current session, so an older load finishing late cannot re-select the stale session, overwrite the visible session artifacts, or re-persist the old last-session selection after a newer switch has already won.

Current web session note: the browser fallback/session-adapter path now fails closed for backend session writes, archived-session deletion clears archived client state too, and web create/list session payloads now preserve `project_id` through the existing metadata seam so project-scoped browser lists do not silently drift.

## Milestone 1 Proof Definition

1. **Backend scope is headless-first**: the authoritative proof path is `ava` headless/benchmark execution.
2. **Proof is real work**: milestone evidence comes from realistic coding suites (`normal_coding`, `small_coding`, `stress_coding`, `test_heavy`) with compile/test validation, plus selected `tool_reliability` coverage and a minimal product smoke (`prompt -> tools -> edit -> verify -> persist`) as the required proof set; contract checks support but do not drive the milestone gate.
3. **Approval policy baseline**: only dangerous commands/actions require explicit approval; ordinary safe tool calls and workspace-preserving edits remain low-friction by default, and unattended headless proof must exercise that real runtime path rather than silently upgrading dangerous asks through yolo wiring.
4. **Primary comparison reference**: `OpenCode` is the main baseline for backend/runtime contract and automation parity.
5. **Secondary execution reference**: `Goose` is useful as a supplemental reference for execution modes and automation pattern parity.

## Simple V1 Checklist

1. Desktop feels polished and stable.
2. All core tools and approvals/questions/plans are understandable in the UI.
3. Multiple chats work correctly at the same time.
4. AVA can complete real coding tasks with the backend, not just toy prompts.
5. The main product flow is covered by automated tests.
6. Docs and version language clearly present this cycle as `0.6` on the path to V1.

## Archived Previous Backlog

The previous detailed backlog has been intentionally collapsed into the archive themes below.

1. Validation sweep across desktop, web, TUI, and headless with stronger smoke coverage for the real user journey.
2. Desktop hardening around session lifecycle, provider/model selection, onboarding/settings, approvals/questions/plans, queue behavior, and active-run correlation.
3. Benchmark expansion for coding, tool reliability, prompt discipline, MCP integration, LSP-adjacent flows, and product-surface smoke tests.
4. AVA-vs-OpenCode comparison work so regressions can be measured with a repeatable baseline.
5. CLI grounding and runtime-discipline work so AVA only claims tool actions and file changes that actually happened and were verified.
6. TUI-to-headless/backend parity work so interactive and non-interactive paths stay aligned on runtime behavior and tool surface.
7. Multi-run and session-attachment follow-up work across frontend and adapters, especially off-screen runs, switch-back recovery, and per-run ownership.
8. Settings cleanup and accessibility follow-up work, including provider flows, dialog behavior, and remaining shell simplification.
9. Backend contract and runtime-boundary cleanup work around queue semantics, event schemas, session continuity, and `AgentStack` ownership seams.
10. Keep local verification ergonomics healthy so staged-snapshot pre-commit checks and path-aware pre-push gates stay safe for partial commits and mixed-surface pushes.

## Out Of Scope For This Queue

1. HQ remains deferred as optional plugin-surface work, not active core backlog.
2. Historical milestone completion notes belong in `CHANGELOG.md` and architecture docs, not this backlog.
