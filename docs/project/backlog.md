---
title: "Backlog"
description: "Pending work for AVA after the recent 3.3 refactor and normalization passes."
order: 3
updated: "2026-04-17"
---

# AVA Backlog

This backlog now tracks pending work only.

Source of truth for direction: `docs/project/roadmap.md`

## Active Now

1. Run a focused `3.3.1` validation sweep across desktop, web, and TUI, then turn failures into narrow reproducible backlog items.
2. Add stronger product-surface smoke coverage for the real core journeys: prompt -> tools -> edit -> verify -> persist.
3. Expand desktop validation around the highest-risk refactor seams: session lifecycle, provider/model selection, onboarding/settings flows, approvals/questions/plans, queue behavior, and active-run correlation.
4. Build a repeatable AVA-vs-OpenCode comparison path for the current core eval corpus so regressions can be measured instead of argued about.

## Pending Engineering Queue

1. Expand the benchmark system from narrow coding/tool lanes into more project-scale and integration-heavy eval suites.
2. Add the first real MCP/LSP/product-surface E2E coverage for core workflows.
3. Harden CLI prompt/runtime discipline so AVA cannot claim edits, writes, or successful tool outcomes unless those actions actually happened and were verified.
4. Compare AVA system prompts against OpenCode prompt strategy on key models such as `gpt-5.4` to identify prompt-grounding and tool-discipline gaps.
5. Make CLI tool awareness fully runtime-grounded so AVA only describes tools that are actually callable in the current session.
6. Compare AVA's CLI question/selection UX against OpenCode and adopt the strongest interaction patterns where they improve clarity, speed, and consistency.
7. Tighten CLI prompt rules for explicit user commands so AVA uses mandatory tools immediately for requests like todo creation instead of narrating intent.
8. Audit TUI-to-backend wiring against the headless CLI path and fix any remaining drift so the interactive TUI uses the same current runtime, tool surface, and prompt assembly as the up-to-date headless flow.
9. Add target-aware completion grounding so successful edit/write claims must match the specific claimed file or todo action, not just the broad tool category.
10. Refine completion-claim heuristics so inspection-only summaries that mention file paths or reviewed files do not trigger false file-mutation grounding nudges.
11. Fix the headless slash-command runtime path so it does not rely on `tokio::task::block_in_place` where the current-thread runtime can panic.
12. Follow Milestone 2 web multi-run backend ownership with the remaining adapter/frontend follow-up slices: browser attach/rebind UX for ambiguous no-target status/cancel paths, per-run todo/plan/edit-history state scoping, and cross-surface parity for desktop/Tauri once the web adapter proves stable (session/run-scoped web rehydration, replay ownership, and per-run same-kind interactive queues are now in place).
13. Finish the deferred Settings shell baseline cleanup with the larger root-modal/tabs-primitive refactor once the current narrow search/dialog/accessibility fixes settle.

## Current Validation Focus

1. Prefer desktop/web/TUI smoke journeys that exercise the real shared backend contract instead of only unit-level assertions.
2. Use the existing Rust, frontend, Playwright, and benchmark surfaces together so validation failures can be bucketed by backend, frontend, transport, or docs drift.
3. Treat missing automated coverage as backlog-worthy when a manual desktop pass reveals a real regression-prone seam.
4. After feature removals, run narrow verification across settings navigation, frontend bridge maps, and desktop/web command exposure so dead surfaces do not linger behind the visible UI cut.
5. Keep focused frontend coverage on Settings child-dialog keyboard dismissal (including the real MCP Add Server nested Escape path) and OAuth-backed provider credential flows so future settings polish does not regress nested Escape handling or OAuth-only provider actions.
6. Keep provider-settings smoke coverage on refreshed model persistence, default-model selection, and Ollama local-model management so future consistency passes do not strand provider actions behind presentational refactors.

## Out Of Scope For This Queue

1. HQ remains deferred as optional plugin-surface work, not active core backlog.
2. Historical milestone completion notes belong in `CHANGELOG.md` and architecture docs, not this backlog.
