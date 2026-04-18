---
title: "Backlog"
description: "Pending work for AVA after the recent 3.3 refactor and normalization passes."
order: 3
updated: "2026-04-18"
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
12. Follow the Milestone 2/3 multi-run backend ownership work with the remaining adapter/frontend follow-up slices: browser attach/rebind UX for ambiguous no-target status/cancel paths, per-run todo/edit-history state scoping, the later full run-agent orchestration unification that should build on the now-landed shared helper seam, and any later desktop multi-window polish that builds on the current session-attachment hardening.
13. Finish the deferred Settings shell baseline cleanup with the larger root-modal/tabs-primitive refactor once the current narrow search/dialog/accessibility fixes settle.
14. Continue the `ava-agent` runtime-boundary cleanup after the landed `stack_mcp` seam by extracting the next safe tool/runtime-assembly helper out of `AgentStack` without changing shared surface contracts.

## Current Validation Focus

1. Prefer desktop/web/TUI smoke journeys that exercise the real shared backend contract instead of only unit-level assertions.
2. Use the existing Rust, frontend, Playwright, and benchmark surfaces together so validation failures can be bucketed by backend, frontend, transport, or docs drift.
3. Treat missing automated coverage as backlog-worthy when a manual desktop pass reveals a real regression-prone seam.
4. After feature removals, run narrow verification across settings navigation, frontend bridge maps, and desktop/web command exposure so dead surfaces do not linger behind the visible UI cut.
5. Keep focused frontend coverage on Settings child-dialog keyboard dismissal (including the real MCP Add Server nested Escape path) and OAuth-backed provider credential flows so future settings polish does not regress nested Escape handling or OAuth-only provider actions.
6. Keep provider-settings smoke coverage on refreshed model persistence, default-model selection, and Ollama local-model management so future consistency passes do not strand provider actions behind presentational refactors.
7. Keep a real chat-surface regression on tool visibility after web sync/session restore so persisted backend snapshots cannot strip argument payloads or richer inline tool-call detail from the message list.
8. Keep focused desktop/web regression coverage on off-screen run continuity so session switches cannot cancel a live run or leak its events, approvals, or streaming state into the newly visible chat.
9. Keep focused coverage on visible-session action gating and hidden-run completion cleanup so a run in session A cannot block start/retry flows in session B or leave the visible session detached after the hidden run settles.
10. Keep detached-run handling run-scoped in frontend runtime regressions so a later run cannot overwrite the detach outcome needed by an older off-screen run, and rehydrate rollback paths always clear tracked ownership when no active run survives.
11. Keep desktop switch-back recovery coverage on detached completions so returning to a Tauri session after an off-screen run reloads the authoritative backend `load_session` result instead of leaving only the detached partial snapshot in memory or local DB.
12. Keep desktop reattach-completion coverage on detached runs reopened before finish so the authoritative recovery marker survives the early return, and the now-visible session still reloads final backend output when that rehydrated run later stops.
13. Keep the new bounded frontend session caches covered: recent session switch-back should restore cached artifacts/runtime state instantly, but backend rehydrate must still be able to invalidate stale optimistic state without leaking the wrong session's live UI.
14. Keep inactive-session mutation/recovery coverage on the bounded session caches so hidden authoritative replacements and off-screen run settlement refresh or evict cached state instead of replaying stale artifacts/runtime UI on the next switch-back.
15. Keep Milestone 3 tool-surface regressions around tool-only assistant turns and reopened desktop recovery so rich tool-call-only results cannot be dropped during settlement or flattened on reload.
16. Keep browser IPC cancel regressions condition-based in combined suites so late websocket filtering tests wait for real listener/correlation state instead of relying on incidental microtask timing.
17. Keep new-chat/session-creation regressions on the bounded artifact cache so opening a fresh chat snapshots the origin session before switch-away and hidden web-mode completions still restore when switching back.
18. Keep chat/session-path coverage honest on warm-cache performance hardening so bounded event history stays incremental after saturation and session switch-back avoids redundant cache-touch/reload work without weakening authoritative refresh corrections.
19. Keep queue-widget regressions section-aware so visible-session edit/remove/reorder controls cannot drift onto hidden-session rows or cross the regular-vs-post-complete boundary during future queue/runtime cleanup.
20. Keep web detached-completion regressions on real switch-back recovery so off-screen browser runs reload the backend-authored final message/tool state instead of preserving only the hidden partial placeholder.
21. Keep authoritative recovery regressions on rich tool-call preservation so thinner `load_session` or `/api/sessions/:id/messages` payloads cannot wipe already-known tool outputs, timestamps, statuses, or content offsets when final assistant text changes on switch-back.
22. Keep browser cache-miss recovery and replay-sync regressions on origin-session identity plus durable tool metadata so divergent backend session IDs remain an internal lookup detail and web reloads still recover full tool cards after cache eviction.
23. Keep browser archive/unarchive regressions on the real web backend path so archived status survives reload/list filtering and mapped frontend sessions still resolve later archive/unarchive operations through the backend alias seam.
24. Keep active web switch-back regressions on the rehydrate/refresh seam so `rehydrateStatus().running === true` preserves cached live assistant/tool state until final sync instead of replaying stale persisted messages mid-run.
25. Keep deterministic browser approval coverage on the real rehydrate/resolve path so ApprovalDock regressions are caught without relying on model-specific tool behavior to raise an approval request.
26. Keep Playwright smoke harness selectors and entrypoints aligned with the current frontend shell so stress/smoke specs wait on the Vite app, target the semantic "Message composer" textbox, and avoid stale server flags or backend-root navigation assumptions.
27. Keep Playwright shell smoke helpers aligned on the robust changelog-dismiss path so the What's New modal cannot intermittently intercept composer input after shell readiness in app/stress/browser smoke runs.
28. Keep live stress assertions anchored to newly-added assistant artifacts so browser smokes cannot false-pass by re-reading preexisting transcript text or the just-submitted user prompt when no assistant response is actually produced.
29. Keep live agent stress smokes validating real success outcomes so UI/API flows assert the expected assistant marker, fail on clear assistant error output, verify persisted transcript content after API submission, and skip cleanly when no provider is configured instead of timing out with misleading noise.

## Out Of Scope For This Queue

1. HQ remains deferred as optional plugin-surface work, not active core backlog.
2. Historical milestone completion notes belong in `CHANGELOG.md` and architecture docs, not this backlog.
