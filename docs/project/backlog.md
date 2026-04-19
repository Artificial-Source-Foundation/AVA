---
title: "Backlog"
description: "Active AVA 0.6 work for the V1 push, plus an archive of the previous detailed backlog."
order: 3
updated: "2026-04-18"
---

# AVA Backlog

This backlog now tracks the active `0.6 -> V1` checklist first.

The older `3.3 / 3.3.1` planning backlog is preserved below in archived form so the current priority stays easy to read.

Source of truth for direction: `docs/project/roadmap.md`

## Active Now

1. Polish the desktop app until it feels ready for daily use, especially chat layout, settings, tool cards, and overall fit and finish.
2. Make sure every core tool and important agent action has clear UI so users can understand what AVA is doing without reading logs.
3. Verify multi-chat behavior end to end so users can switch sessions while runs continue without losing state, mixing outputs, or breaking approvals/plans.
4. Prove the backend can do real coding work reliably in headless, TUI, desktop, and web flows.
5. Add a simple automated product smoke suite for the core journey: prompt -> tools -> edit -> verify -> persist.
6. Keep a repeatable AVA-vs-OpenCode comparison path so quality can be measured, not argued.
7. Finish the docs reset around the `0.6` story so roadmap, backlog, README, and release language all describe the same product stage.

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

## Out Of Scope For This Queue

1. HQ remains deferred as optional plugin-surface work, not active core backlog.
2. Historical milestone completion notes belong in `CHANGELOG.md` and architecture docs, not this backlog.
