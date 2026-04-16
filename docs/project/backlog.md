---
title: "Backlog"
description: "Active priorities for the AVA 3.3 core baseline and the 3.3.1 validation follow-up."
order: 3
updated: "2026-04-15"
---

# AVA Backlog

This backlog tracks the current AVA core execution work.

Source of truth for direction: `docs/project/roadmap.md`

## Now

1. ~~Finished the remaining Milestone 2 queue/session normalization pass: deferred follow-up/post-complete enqueue paths now stay session-owned in the backend, session-switch queue clearing only removes rows for the session being left, and focused regressions now cover real session-switch plus backend-managed row-control boundaries.~~ ✅ Completed 2026-04-15.
2. Stand up the `3.3.1` validation program for core AVA.
2. Expand the benchmark system from current coding/tool lanes into project-scale and integration-heavy eval suites.
3. Add the first real MCP/LSP/product-surface E2E coverage for core workflows.
4. Build a comparison path against OpenCode.
5. Harden CLI prompt/runtime discipline so AVA cannot claim file writes, edits, or tool success unless those actions actually happened and were verified.
6. Compare AVA system prompts against OpenCode's prompt strategy on key models such as `gpt-5.4` to identify prompt-grounding and tool-discipline gaps.
7. Make CLI tool awareness fully runtime-grounded so AVA only describes tools that are actually callable in the current session, with correct names and capability availability.
8. Compare AVA's CLI question/selection UX against OpenCode and adopt the strongest interaction patterns where they improve clarity, speed, and consistency.
9. Tighten CLI prompt rules for explicit user commands so AVA uses mandatory tools immediately for requests like todo creation instead of narrating intent or waiting to be corrected.
10. Audit TUI-to-backend wiring against the headless CLI path and fix any drift so the interactive TUI uses the same current runtime, tool surface, and prompt assembly as the up-to-date headless flow.
11. Add target-aware completion grounding so successful edits/writes must match the specific claimed file or todo action, not just the broad tool category used somewhere earlier in the session.
12. Further refine completion-claim heuristics so inspection-only summaries that mention file paths or reviewed files do not trigger file-mutation grounding nudges.
13. Fix the headless slash-command runtime path so it does not rely on `tokio::task::block_in_place` in contexts where the current-thread runtime panics.

## Milestone 3 Backend Correction Plan

Planning inputs:

1. `docs/architecture/agent-backend-capability-audit-m1.md` (Milestone 1 inventory)
2. `docs/architecture/agent-backend-capability-comparison-m2.md` (Milestone 2 comparison matrix)
3. `docs/architecture/backend-correction-roadmap-m7.md` (implementation-ready dependency order, owner seams, and conformance gates for cross-surface backend correction adoption)

Priority-ordered correction tracks:

| Priority | Track | Canonical owner seam | Dependent adapters / surfaces | Success criteria (short) | Why it matters |
|---|---|---|---|---|---|
| P0 | Runtime parity and behavior coherence | `crates/ava-agent/` runtime assembly/execution seams with shared approval-policy layers (`crates/ava-permissions/`, `crates/ava-tools/src/permission_middleware.rs`) | `crates/ava-tui/`, `src-tauri/src/`, `src/services/rust-bridge.ts`, web/headless adapters | Tool visibility, delegation behavior, and approval semantics resolve from one backend policy path across interactive and non-interactive surfaces. | Surface drift causes correctness regressions and erodes trust because behavior changes by entrypoint. |
| P1 | Control-plane contract unification | Shared backend command/control-plane contract seam (`crates/ava-agent/` adjacent shared runtime modules) | TUI runtime command handling, Tauri command DTOs, TypeScript bridge and UI contract types | Cross-surface runtime command semantics use one canonical backend contract with thinner adapter mapping. Built-in-vs-custom slash-command registry unification remains a later follow-up track. | Contract duplication is a persistent source of cross-surface mismatch bugs and maintenance drag. |
| P2 | Headless unattended approval policy | Shared approval semantics in `crates/ava-permissions/` with runtime wiring in `crates/ava-agent/` | `crates/ava-tui/src/headless/`, CLI/headless automation paths | Headless approval behavior is explicit and policy-driven, not implicit auto-approve behavior. | Unattended execution is high risk; unclear policy semantics can produce unsafe or surprising outcomes. |
| P3 | Ownership boundary and extension seam hardening | `crates/ava-plugin/` plus core/runtime ownership seams and architecture ownership docs | Tool/command/skill delivery surfaces, plugin registration paths, project-local extension configuration surfaces | Core-vs-plugin placement decisions are explicit in architecture docs before broad implementation starts. | Prevents accidental core-surface growth and keeps optional capability ownership maintainable long-term. |

Backlog vs architecture planning split:

1. Backlog implementation queue now: P0, P1, P2.
2. Architecture-doc/planning queue first: P3.
3. `docs/architecture/plugin-boundary.md` is HQ-specific migration planning and is not the general backend-correction intake surface.

Recent core progress:

1. The remaining Milestone 7 WS5/WS6 backend-correction slice is now closed too: desktop/web queue cancel and interactive resolve/forwarder paths now share lifecycle locks so cancellation is a real boundary for queue ownership plus approval/question/plan state, web queue endpoints now match desktop non-empty validation, and canonical delegation-visibility events now require snake_case `run_id`/`call_id` correlation all the way through desktop/web projections, TS unions, and frontend handlers with malformed-event drops plus focused regressions.
1. The last desktop/web same-kind interactive timeout blocker is now fixed in the shared backend seam too: `timeout_request(request_id)` no longer consumes queued non-front approval/question/plan requests, so hidden FIFO items stay pending until promotion instead of auto-timing out invisibly, and focused regressions now cover the two-request threshold-overrun path across all three kinds.
1. The final narrow normalization follow-up is now closed too: frontend queue-row edit/remove/reorder operations now remap visible rows back onto the correct current-session queue entries even when other sessions still own hidden rows in the full queue, and desktop/web same-kind interactive watchdogs now wait for promotion before starting a queued request's timeout window so promoted requests do not inherit stale registration-time timers.
1. The final broad-review interactive lifecycle blockers are now closed in one narrow pass: stale `ava-agent` stack tests compile again with the new `interactive_run_id` run argument, the desktop/frontend agent store now queues same-kind approval/question/plan requests by `request_id` instead of overwriting older hidden requests, and the shared interactive request store now rejects out-of-order resolve attempts against queued non-front request IDs.
1. The current Milestone 7 proof-first follow-up is now tighter on the shared control-plane seam too: `ava-agent` ships one backend-owned command+event fixture matrix that locks the canonical wire contract in a single test, frontend control-plane event consumers now read the canonical snake_case correlation fields directly on the remaining approval/interactive-clear/delegation/edit-progress paths instead of normalizing duplicate camelCase shapes, and the remaining explicit contract risk is now mostly outside WS1/WS2 (lifecycle/session/queue/headless follow-up plus eventual generated TS schemas).
1. The final bounded Milestone 3 TUI cross-kind modal-ownership review bug is now closed too: approval/question/plan arrivals share one deterministic interactive queue so no cross-kind request can hide the currently visible owner, the next pending interactive request now promotes regardless of kind when the active modal clears, hidden queued question/plan timeouts only start after promotion, and focused `ava-tui` regressions cover approval↔question/plan arbitration plus the missing background question/plan no-foreground-start survival cases.
1. The last bounded Milestone 3 TUI run-ownership bug is now closed in one narrow pass: run-scoped agent tool registries stamp approval/question/plan requests with the true originating TUI `run_id`, the TUI interactive adapter now supersedes/cancels only requests owned by that run instead of the current foreground run, and focused `ava-tui` regressions cover background-run approval survival across a foreground switch plus question/plan same-run supersession without cross-run loss.
1. The remaining Milestone 3 TUI ownership follow-up is now closed in one small pass too: starting a new foreground goal with no current foreground run no longer degenerates into "cancel every pending interactive request", and cross-run question/plan arrivals now preserve the visible modal owner while queueing the newcomer deterministically until the active request clears.
1. The final in-scope Milestone 3 TUI lifecycle review gaps are now closed in one narrow pass: queued approval timeouts only arm for the visible front request and re-arm when the next approval becomes active, timeout/cancel clear paths now reset stale approval modal stage/text before showing the next item, and TUI question/plan intake now explicitly supersedes any older pending sender so replies cannot be stranded behind a second request.
1. The remaining Milestone 3 TUI run/event-normalization approval-lifecycle review finding is now closed in one narrow pass: the shared interactive request store keeps multiple pending replies per kind keyed by request ID instead of overwriting older approvals, TUI cancellation now drains only the owning run's pending interactive requests, and a focused `ava-tui` regression proves two queued approvals resolve in order without dropping either reply sender.
1. The bounded TUI Milestone 3 follow-up slice is now tightened too: `plan_approval.rs` and the new `AgentActivity` assertion build cleanly again, focused `ava-tui` regressions now prove plan resolve + timeout cleanup through the shared interactive lifecycle store, and a new stale-plan regression covers the "superseded by a new TUI run" clear path so the previous run cannot leave modal state behind.
1. The first bounded interactive-TUI Milestone 7 follow-up slice is now landed too: TUI approval/question/plan request handling now uses the shared backend `control_plane::interactive` lifecycle store with canonical request IDs plus shared timeout/cancel cleanup instead of app-local reply ownership, focused TUI regressions now prove timeout/resolve/cancel behavior against that seam, and the TUI foreground/background run routers now surface the remaining obvious required canonical events (`plan_step_complete`, `streaming_edit_progress`, and background `complete`) instead of silently dropping them.
1. Milestone 2 session/history normalization is tighter on the remaining web ownership seam too: the web `WebStateInner` test builder now matches the live state shape again, `/api/agent/submit` route coverage now proves omitted `session_id` starts a new session instead of inheriting process-global `last_session_id`, and web retry/edit/regenerate now require explicit `session_id` ownership (with matching browser-side forwarding/tests) so one browser client cannot silently replay another client’s last active session.
1. Milestone 2 session/history normalization has now started in the shared backend contract itself: `ava-agent::control_plane::sessions` owns canonical prompt-context/history collection plus retry/edit/regenerate replay-payload builders, desktop/web adapters now import that shared seam instead of keeping local history/replay copies, and focused regressions lock the shared image/history semantics along with desktop's now-normalized user-only edit target behavior.
1. The remaining Milestone 7 contract follow-up on the web correlation seam is now covered in one narrow pass: web submit/retry/regenerate/edit runs keep an adapter-owned `run_id` even when the caller omits one, active browser listeners now ignore terminal plus interactive events that do not carry the winning run correlation, and focused proof now covers submit/compaction precedence, queue-clear supported-vs-unsupported behavior, and a desktop interactive-clear smoke path.
1. The last desktop-facing closure blockers are now covered in one narrow pass: onboarding's single-auth provider card now anchors its full-card button to a positioned tile so click capture cannot spill into adjacent cards, and the Tauri desktop event bridge now threads `run_id` through late per-run streaming/progress payloads so the frontend rejects stale run-A token/thinking/tool/progress updates once run B is active.
1. Milestone 7 contract adoption now has its first backend-owned normalization slice beyond commands/interactive: `ava-agent` owns canonical `control_plane/events.rs`, `sessions.rs`, and `queue.rs` modules, desktop/web submit + compaction paths now reuse the shared requested > last > new session precedence helper, queue-clear adapters now share one unsupported-follow-up/post-complete rule, and web event projections now carry the missing canonical correlation fields (`tool_call_id` / `call_id`) for approval/delegation/edit-progress payloads.
1. The final desktop broad-review follow-up is now closed on the provider/model submit seam too: `use-model-state` now repairs missing/stale provider selection whenever the composer auto-selects or restores a model, `QuickModelPicker` now writes provider+model together instead of model-only, and focused frontend/Tauri regressions cover both desktop submit-arg forwarding and terminal-event completion without transport drift.
1. The last desktop-facing broad-review follow-up is now closed in a tight final pass: restored desktop session sync snapshots now preserve user image attachments through frontend snapshot build plus Tauri materialization so retry/regenerate/edit can keep multimodal context after a backend rebind, and device-code OAuth success now updates onboarding/settings state immediately after credentials are stored so the success transition cannot be lost if the dialog closes.
1. The remaining desktop broad-review correctness follow-up is now covered with a narrow adapter+backend pass: opened/switched/created desktop sessions now send a frontend session snapshot through `notifySessionOpened` into Tauri `set_active_session`, allowing the Rust backend to lazily materialize a missing session with matching IDs/history instead of leaving retry/regenerate/edit blocked forever, and Settings/provider OAuth success now performs a real provider-state update that marks the provider connected while clearing stale API-key precedence in desktop settings state.
1. Milestone 7 WS2 is now landed for the desktop + web adapter slice on the shared backend seam: `ava-agent` owns interactive request state, request IDs, and the canonical timeout policy under `crates/ava-agent/src/control_plane/interactive.rs`; desktop and web now resolve/cancel/time out approval/question/plan requests through that same lifecycle store, require `request_id` for interactive resolve calls, reject missing/stale IDs without consuming newer pending state, and emit the same `interactive_request_cleared` event on successful resolve as well as timeout/cancel paths so subscribed UI clears consistently. Interactive TUI still needs one bounded adapter pass to (a) replace its app-local approval/question/plan reply handles with `InteractiveRequestStore` ownership, (b) thread request IDs through modal state and local clear transitions, and (c) adopt the shared timeout/cancel cleanup seam; headless remains intentionally scoped under `EX-001` and unchanged in this slice.
1. WS2 review cleanup is now closed on the remaining stale-interactive seam for desktop + implemented web lifecycle paths: interactive request handles keep their originating `run_id` through resolve/cancel/timeout cleanup, the frontend now drops stale correlated approval/question/plan/clear events instead of only terminal Tauri events, and web acceptance coverage now includes the timed-out `interactive_request_cleared` projection plus correlated cancel cleanup. The only explicit follow-up risk left in this area is the still-unadopted TUI interactive adapter path already called out above.
1. Milestone 4 desktop stress polish is now closed in a narrow final pass: late-settling desktop runs still persist their originating session placeholder/final output/cancel note after a fast session switch while active-session cleanup stays guarded, retry/regenerate/edit preflights still abort if the initiating session loses ownership during backend-sync preflight, desktop approval/question/plan requests now emit explicit clear/timeout events when Tauri auto-times them out or auto-clears them so stale docks disappear, plan request correlation IDs survive the higher-level `useAgent` pending-plan state, the Tauri agent-event bridge now projects plan-step completion plus delegated/edit progress events so desktop progress stays in sync with the shared runtime, and the last timing/correctness follow-up now run-scopes Tauri terminal settlement so stale run A terminal outcomes cannot settle run B while keeping approval/question/plan UI visible until resolve IPC success.
1. Dependency security maintenance moved forward again: the repo lockfiles/manifests now clear the current npm/pnpm advisory set and refresh the fixable Rust advisories (`rand`, `bytes`, `time`, `quinn-proto`, `rustls-webpki`, `lru`, `lz4_flex`, plus the latest `tauri` 2.10 patch line). The only remaining blocker is the upstream-constrained Linux `glib` alert, because current `tauri 2.10.x` still pins `gtk 0.18` / `glib 0.18`.
1. Milestone 7 WS1 is now started in code with the smallest P0 slice: `ava-agent` owns a canonical shared command/completion contract under `crates/ava-agent/src/control_plane/commands.rs`, web frontend routing now includes the missing `resolve_plan` mapping, and desktop/web/TUI/headless queue-command semantics now reuse the same backend command-family map with focused contract/route regressions.
1. WS1 review follow-up tightened that same slice: command-spec terminal/correlation semantics were corrected for cancel + interactive resolve commands, headless JSON queue input now accepts canonical queue command names and rejects unknown tiers, `resolve_plan` route coverage now exercises both success and bad-request paths, and the desktop completion-mode divergence remains intentionally documented as backend-contract exception `EX-002` until a later parity refactor.
1. Milestone 3 desktop agent-run reliability moved forward with a narrow backend-first fix set: desktop OAuth storage now writes real provider auth through a Rust credential bridge (including refresh/expiry/account metadata and live router-cache refresh), OAuth-backed providers clear stale API-key state so routing actually prefers OAuth, native desktop auth write/delete failures now surface instead of silently falling back to local storage, and restored/switched sessions now use an acknowledged backend-session sync contract before retry/regenerate/edit-and-resend continue (with focused regressions for the sync/auth/error-settlement paths).
1. Milestone 3 desktop reliability follow-up is now in place too: open/switch/restore lifecycle sync is best-effort when the backend has not seen that session yet, retry/regenerate/edit preflights still enforce strict backend-session existence, Tauri keeps the agent-event listener alive through a short post-`invoke` grace window so late final updates land, onboarding/provider OAuth now treats pending device-code auth honestly instead of persisting `"(oauth)"` placeholders, and direct regressions now cover the desktop auth/session owner seams plus happy-path action ordering.
1. The final narrow Milestone 3 review follow-up is now covered as well: stale session-open/switch sync completions re-assert the newest winning desktop session instead of letting an older request rebind the backend, onboarding OAuth clears conflicting API-key state before completion finalizes provider settings, and Tauri terminal settlement now normalizes to one stable `SubmitGoalResult` contract even when the backend event and invoke timing differ.
1. The last remaining Milestone 3 desktop review nits are now closed too: secure-store API-key sync clears stale OAuth metadata so explicit API-key auth really takes over, onboarding completion now prefers the entered API key over any stale OAuth marker, archive/delete fallback session switches reuse the same backend active-session rebind path as normal switch/create flows, and focused regressions cover the App seam plus the symmetric stale-sync late-error repair branch.
1. The last Milestone 3 desktop run-reliability correctness gaps are now closed: Tauri `create_session` persists new backend sessions before active-session binding checks run, and desktop retry/regenerate/edit preflights now retry same-session backend sync after a prior missing/error result instead of treating that failed cache as permanent.
1. Milestone 1 desktop/web onboarding shell usability is tighter: dismiss/complete now close the onboarding overlay before follow-up settings persistence runs, the selected onboarding theme mode now persists correctly, Settings-triggered reopening closes the settings surface before showing onboarding again, guide-mode reopening now exposes a visible dismiss path on every step plus Escape support for keyboard users, every onboarding step moves focus to its current heading, onboarding close restores shell focus predictably (including the Settings reopen path and the later-guide dismiss return to the Settings affordance), the workspace/import copy no longer implies unimplemented behavior, and regression coverage now exercises those paths at unit and browser level.
1. Milestone 2 desktop/web tooling visibility parity is tighter now too: web has a real backend `/api/tools/agent` route with session-aware introspection payloads, the Tool List dialog now also sends explicit fresh goal/history/image context on same-session refetches (with desktop/web backends preferring that explicit context when present), desktop MCP IPC exposes live status plus enable/disable commands like web, and the Settings MCP surface refreshes from backend live state, clears stale live-cache data after local add/remove mutations, disables config-disabled enable toggles that the current backend cannot honor, ignores stale in-flight live fetches after local MCP edits instead of letting outdated backend responses repopulate the list, and now initializes MCP status on the first fresh Settings open so real connected/failed/zero-tool rows replace the old placeholder `connecting` default.
1. The remaining narrow Milestone 2 session MCP visibility blocker is now covered in the backend registry rebuild path too: disabling an MCP server for the current session now keeps that server’s tools out of both run-scoped registries and interactive/introspection tool listings for the rest of the same session instead of letting cached runtime MCP metadata re-register them.
1. Web backend startup is now compatible with older local AVA databases again: `ava-db` repairs the known SQLx checksum drift for the historical HQ migrations `003_hq.sql` and `004_hq_agent_costs.sql`, which unblocks `ava serve` on lived-in `~/.ava/ava.db` installs after the 3.3 baseline comment-only migration edits.
1. Provider route/region aliases now normalize to the canonical core providers across the repo-owned model catalog and frontend model-loading path.
2. Legacy IDs such as `alibaba-cn`, `zhipuai-coding-plan`, `kimi-for-coding`, and `minimax-cn-coding-plan` continue to resolve for compatibility, but they no longer need distinct provider buckets in the active core catalog.
3. The 3.3 core baseline is now treated as complete enough to begin project-scale validation rather than more baseline reshaping.
4. 3.3.1 eval Round 2 landed first real tasks for `small_coding`, `stress_coding`, `large_project`, `test_heavy`, and `maintenance`, with Tier 3 workspace/validation support now wired for those suites.
5. 3.3.1 eval Round 3 now includes first real `tool_recovery` tasks, including explicit verification-discipline coverage, with matching Tier 3 workspace setup and validation handling.
6. 3.3.1 eval Round 4 now includes first real `mcp_integration` tasks using deterministic local stdio mock MCP servers for filesystem, git, and multi-server flows, with project-local `.ava/mcp.json` plus audit-log-backed validation.
7. 3.3.1 eval Round 5 now includes first real `lsp_smoke` and `product_smoke` tasks with deterministic workspace fixtures and Tier 3 validation (config/project/toolchain LSP-adjacent checks plus session/config/permission/tool-discovery smoke flows).
8. 3.3.1 eval Round 6 now includes the first AVA-vs-OpenCode report comparison runner: it compares two existing benchmark JSON reports by aligned task name, computes per-side aggregate stats, reports time/cost savings and win counts, and can save a structured comparison artifact.
9. A concrete implementation plan now exists for provider-family and system-prompt benchmarking, including prompt metadata in reports, repeat-run support, prompt-regression suites, and prompt-vs-prompt comparison flows. See `docs/project/provider-prompt-benchmarking.md`.
10. 3.3.1 eval Round 7 now includes the first `prompt_regression` lane with deterministic fixtures and Tier 3 validation for prompt-sensitive behaviors: verify-before-finish, targeted edits, minimal patching, read-before-edit discipline, wrong-first-edit recovery, and tool-choice/subagent discipline.
11. The benchmark runtime now records prompt metadata in reports, supports repeat-run aggregate summaries plus explicit output paths, threads prompt-family/file overrides into agent prompt assembly, and compares AVA-vs-AVA prompt reports through generic left/right comparison flags (legacy AVA/OpenCode aliases still work).
12. Prompt-note assembly now supports provider-family overlays keyed by provider name + model family (separate from `ProviderKind`), with an initial lean Alibaba+Kimi overlay layered above family notes.
13. Provider-family prompt tuning is continuing on real benchmark evidence: Alibaba-hosted GLM now has its own narrow overlay for edit-then-verify discipline, and the prompt-regression `read_before_edit` task now keys verification off real `bash` tool evidence instead of brittle literal `cargo test` wording.
14. Alibaba GLM repeat benchmarking exposed a remaining recovery weakness: after a bad first edit it sometimes guesses another implementation and invents alternate test commands, so the current overlay now explicitly tells it to trust failing assertions and rerun the same verification command after correcting the file.
15. A follow-up GLM finding was more specific: the model sometimes trusted stale fixture-local binaries like `test_runner` instead of the project test command, so the overlay now explicitly points Rust fixture verification back to `cargo test` at the fixture root.
16. The deeper GLM recovery root cause was task misinterpretation: on `wrong_first_edit_recovery` it sometimes intentionally made a bad first edit to satisfy the wording, so the overlay now states explicitly that the first attempt should still be the most likely correct fix.
17. The remaining GLM recovery instability also exposed a benchmark-fixture issue: `prompt_regression_wrong_first_edit_recovery` was not a standalone Cargo package, so `cargo test` could leak into the parent benchmark workspace and fail for unrelated reasons; the fixture now writes its own local `Cargo.toml` to keep verification honest.
18. Alibaba-hosted Qwen now has its own narrow provider-family overlay too: baseline prompt-regression showed it could diagnose the recovery fixture but still stop before the direct edit, so the hosted-Qwen notes now push it toward the identified target file and away from unrelated file exploration.
19. A follow-up Alibaba Qwen run showed another narrow failure mode: it could still stop with diagnosis-only text after reading the failing assertions, so the overlay now explicitly says not to end the turn with summary output once the concrete edit is already implied.
20. Milestone 3 prompt tuning now also covers Alibaba-hosted MiniMax with a narrow provider-family overlay: it keeps MiniMax on assertion-led direct edits, prevents diagnosis-only turn endings when the fix is already implied, and reinforces immediate fixture-root rerun behavior.
21. MiniMax Milestone 3 overlay guidance was tightened on the two remaining weak points with more generic behavior rules: minimal normalization fixes now avoid extra dependencies and brittle fallback paths, while default-value fixes now prefer direct file-tool usage plus one complete atomic update.
22. MiniMax Milestone 3 received one final wording cleanup pass before commit so the Alibaba-hosted notes keep the proven behavioral guidance without embedding benchmark-answer details directly into the provider-family overlay.
23. Benchmark runtime modularization has started landing: shared Tier 2 validation/code extraction now lives in `benchmark_validation.rs`, shared LLM-as-judge logic lives in `benchmark_judge.rs`, shared benchmark/harness rendering lives in `benchmark_format.rs`, and the benchmark runner files are meaningfully smaller and easier to navigate.
24. Benchmark throughput reporting now carries two solo-run views: `WallTok/s` keeps full-task wall-clock throughput for responsiveness tracking, while additive `generation_tps` / `GenTok/s` normalizes by subtracting TTFT so benchmark users can compare a decode-style TPS that better matches external tooling.
25. Skills now have a first-class runtime listing surface: `/skills` shows the live filesystem-discovered `SKILL.md` set using the same trust-gated discovery model as instruction loading, instead of relying on any bundled registry.
26. Project trust hardening now also covers project-local plugins and trusted-root instruction discovery, so untrusted `.ava/plugins` stay inert and instruction loading no longer inherits ancestor `AGENTS.md` files from outside the explicitly trusted project root.
27. Benchmark TPS reporting now better matches real runtime accounting: solo benchmark totals include sub-agent token usage, benchmark tables label TPS as wall-clock throughput, and HQ harness runs consume real worker usage events instead of inventing worker token splits from output text.
28. Milestone 2 doom-loop coverage now explicitly includes Alibaba-hosted Qwen variants in `LoopThresholds::for_provider_model` (provider+family scoped, not provider-only blanket matching), with regression tests covering Alibaba Qwen plus preserved Alibaba GLM/Kimi/MiniMax behavior.
29. Milestone 4 Alibaba+Kimi tuning has started from a full `prompt_regression --repeat 3` acceptance run: baseline was not acceptable (especially `wrong_first_edit_recovery` and unstable `tool_choice_discipline`), so a minimal provider-family overlay refinement now emphasizes direct target-file edits, first-attempt correctness before recovery, and one-shot tuple editing for the retry-policy fixture.
30. Milestone 5 Alibaba+Qwen tuning has now run against a fresh strongest-practical representative baseline (`qwen3-coder-plus`, `prompt_regression --repeat 3`): baseline was not acceptable (hard 0% `wrong_first_edit_recovery`, plus instability on `minimal_patch`/`tool_choice_discipline`), so a minimal overlay refinement now pushes hosted Qwen away from repeated shell listing loops once the target function is known and back to direct file-tool edit+verify recovery.
31. Milestone 3 fake tool-claim hardening is now in the core runtime too: the shared system prompt plus GPT-family prompt text explicitly forbid claiming edits/writes/todo updates without successful tool evidence, and `ava-agent` now rejects obvious ungrounded completion claims for those categories when matching successful tools never ran.
32. The first Milestone 3 runtime guard is intentionally narrow and heuristic-based: it only checks obvious completion claims about file mutation and todo mutation, and it reuses existing session tool history instead of introducing a broad NLP classifier.
33. Milestone 1 of TUI/Desktop tool-surface grounding landed: interactive introspection now reads an effective run-time tool surface API from `AgentStack`, which can include run-scoped `subagent` metadata when delegation is enabled without incorrectly adding `subagent` to the long-lived stack registry.
34. Milestone 2 of that grounding work is now landed too: explicit delegation detection can carry across short follow-up turns in the same conversation, and interactive tool introspection now follows the same run-time delegation/tool-visibility analysis path as `run()` instead of a standalone boolean gate.
35. Milestone 1 capability audit doc now tracks this tool-surface path explicitly (including desktop command exposure): `docs/architecture/agent-backend-capability-audit-m1.md`.
36. Milestone 2 backend-capability comparison artifact now exists at `docs/architecture/agent-backend-capability-comparison-m2.md`, providing a concise AVA-vs-OpenCode/Pi/Claude Code/Codex matrix and naming the strongest correction references for backend follow-up work.

## 3.3.1 Execution Order

1. Write and lock the eval spec: `docs/project/ava-3.3.1-evals.md`.
2. Expand core coding/task suites: `small_coding`, `stress_coding`, `large_project`, `test_heavy`, `maintenance`.
3. Extend tool-use coverage: verification discipline, wrong-tool recovery, tool-error handling, efficiency scoring.
4. Add MCP integration suites: filesystem, git, and multi-server core scenarios.
5. Add LSP-adjacent and product-surface smoke journeys for TUI/desktop/web.
6. Add AVA-vs-OpenCode comparison runs.
7. Extend the benchmark system into provider-family and system-prompt tuning with repeated runs, prompt metadata, and prompt-regression suites.

## 3.3.1 Core Missions

### Mission A — Project-Scale Coding Evals

Goal:

1. Prove that AVA can write and extend real projects, not only solve narrow benchmark snippets.

Success criteria:

1. Multi-file project tasks build and test successfully through automated validation.
2. Suites cover small, normal, stress, maintenance, and large-project workflows.

### Mission B — Tool Reliability And Recovery

Goal:

1. Make tool-use quality measurable beyond final success or failure.

Success criteria:

1. Evals capture wrong-tool choices, repetitive failures, verification discipline, and recovery quality.
2. Tool-quality regressions are visible separately from coding-quality regressions.

### Mission C — MCP, LSP, And Integration Coverage

Goal:

1. Cover the integration-heavy core surfaces that matter for real coding workflows.

Success criteria:

1. Real MCP server workflows are exercised end to end.
2. The current LSP-related surface is covered honestly, with deferred gaps documented instead of implied away.

### Mission D — Product-Surface Smoke Evals

Goal:

1. Verify the default user journeys across TUI, desktop, and web.

Success criteria:

1. Prompt -> tool -> edit -> verify -> persist flows work in automated smoke coverage.
2. Provider/model switching, permissions, and session persistence are included in core journeys.

### Mission E — Competitive Baselines

Goal:

1. Measure AVA against OpenCode on the same task corpus.

Success criteria:

1. The repo can produce structured AVA-vs-OpenCode results for the core eval suites.
2. Comparative regressions are visible in normal development, not just anecdotal testing.

Current release-hardening state:

1. `cargo test --workspace`, `cargo clippy --workspace --all-targets`, `cargo fmt --all -- --check`, and `pnpm lint && pnpm format:check && pnpm typecheck` are green on the current 3.3 baseline.

Docs reset progress:

1. Active architecture docs now live under `docs/architecture/`, release docs now live under `docs/contributing/`, and historical gap-analysis material now lives under `docs/archive/research/`.
2. `docs/README.md` now separates active docs from historical docs, and stale compatibility entrypoints (`CLAUDE.md`, `llms.txt`, `CODEBASE_STRUCTURE.md`) now point back to the active 3.3 docs set.
3. Active docs pages now carry consistent frontmatter and section navigation manifests so they can be imported into a future docs website with minimal reshaping.
4. The remaining docs work is mostly incremental coverage and future site-generator integration, not baseline structure.

## Active Core Focus

Current priority is making default-core AVA work well on its own.

Current core focus:

1. Prove the solo-first runtime in realistic end-to-end coding scenarios.
2. Keep the visible settings and default product surface simple while we add stronger evals.
3. Validate MCP/tool/provider/session workflows under real pressure before reopening optional roadmap scope.
4. Use evaluation failures to drive narrow fixes and permanent regressions.

## Remaining Intentional Baggage

1. `crates/ava-tui` still links `ava-hq` behind `--features benchmark` for benchmark-only coverage.
2. `crates/ava-db/src/migrations/003_hq.sql` and `004_hq_agent_costs.sql` remain as historical compatibility migrations until a deliberate cleanup migration is introduced.

## Deferred Roadmap Items

These are intentionally not part of the active 3.3 core execution track.

1. HQ can return later only as an optional plugin, not as part of the default core product.
2. The existing HQ plugin-boundary notes and first plugin artifact are retained as future roadmap groundwork, not as current backlog work.

Normalization notes:

1. MCP and extensions are now explicitly treated as different surfaces: MCP owns external server/tool integration, while `ava-extensions` remains a separate native/WASM descriptor and hook surface.
2. `ava-extensions` is currently narrow and desktop-facing rather than part of the main 3.3 customization story.
3. HQ SQL migrations remain intentionally in place for compatibility, but no new core work should extend that schema path without a deliberate cleanup/deprecation plan.
4. The remaining HQ-only dormant runtime path is isolated to `ava-hq`'s `run_external_worker()` helper; it is no longer part of the default 3.3 core flow.
5. The remaining `#[allow(dead_code)]` cases are now limited to intentional compatibility fields, future-facing hooks, the stubbed WASM extension loader, and that isolated HQ runtime helper.
6. Benchmarks now run two explicit lanes: `tool_reliability` (headless scripted tool use) and `normal_coding` (representative implementation quality), with separate tool-failure scoring.
7. Runtime model metadata is repo-owned end-to-end: backend and frontend now use curated `list_models` via `curated-model-catalog`; `models.dev` runtime fetches are removed.
8. Prompt architecture is separated by family/provider files (`prompts/families/*`, `prompts/providers/*`), and `system_prompt.rs` now primarily assembles these notes.
9. Family prompt tuning is now benchmark-driven (GPT-family first), and family detection rules were tightened to avoid false family matches.
10. Gemini benchmark reliability improved after fixing streamed tool-argument snapshot merging; tool-reliability results now better reflect prompt behavior instead of transport parser noise.
11. Secure credential storage is now the default shared path: desktop sync writes into the Rust secure store, frontend settings persistence no longer serializes raw provider API keys, and startup prefers the secure store while still reading existing plaintext `~/.ava/credentials.json` for compatibility.
12. Onboarding is now an optional in-app guide instead of a startup gate, and it can be reopened from Settings > General.
13. Plugin and MCP management remain available but now live under `Advanced` by default rather than the main tools surface.
14. Doom-loop handling now uses a policy layer, not only thresholds: loop-prone models follow `nudge, nudge, stop`, with refinements for cooldown safety/cost tracking, hidden judge nudges, UTF-8-safe truncation, and escalation reset only after real progress.
15. Claude Code integration in `ava-acp` now has both a Rust-side auth baseline and a first file-backed resume layer: discovery can report cached Claude auth state, the Claude SDK adapter retries once after refreshing local Claude OAuth credentials, and the direct ACP provider path persists conversation-prefix session mappings so Claude Code sessions can resume across turns and process restarts. A deeper follow-up still remains for richer lineage/undo recovery beyond prefix-based matching.
16. HQ re-registration groundwork exists on the plugin side too: `plugins/examples/ava-hq/` provides a local-linkable HQ plugin artifact backed by the `ava-hq-plugin` binary, but that work is now intentionally deferred behind the future roadmap rather than treated as active core backlog.

## Completed In 3.3 Baseline

The codebase health plan below has landed and is now retained as a compact record of the completed 3.3 cleanup sweep rather than an active execution queue.

## Codebase Health Plan

1. Shrink the core runtime orchestrators: split the largest `ava-agent` execution paths into explicit phases so streaming, recovery, tool execution, compaction, and completion are easier to reason about and debug independently.
2. Create one canonical runtime assembly path: reduce drift between TUI, web, desktop, and headless startup/run wiring so product surfaces share more of the same `AgentStack` construction and execution path.
3. Improve debuggability end-to-end: add clearer tracing, phase/event visibility, and better runtime diagnostics so "why did AVA do that?" is answerable without deep manual log archaeology.
4. Harden the high-risk subsystems with stronger regression coverage: prioritize provider streaming/parsing, the edit engine, and the permissions system with fixture-based and behavior-focused tests.
5. Keep collapsing UI/settings sprawl: continue turning grouped legacy tabs into real merged sections and remove remaining special-case UI/state branches that make the desktop frontend harder to maintain.
6. Clean up names, ownership, and leftovers: remove legacy terminology, dead abstractions, and ambiguous module boundaries so each subsystem has a clearer responsibility surface.

## Suggested Execution Order

1. Runtime orchestrator refactor (`ava-agent` loop/stack)
2. Shared runtime assembly across TUI/web/desktop/headless
3. Debugging and tracing improvements
4. Regression test expansion for risky systems
5. Settings/UI simplification passes
6. Naming and ownership cleanup sweep

## Concrete Missions

### Mission A — Split the Agent Runtime Into Phases

Goal:

1. Break the largest `ava-agent` runtime flows into smaller, named phases with clearer ownership.

Target areas:

1. `crates/ava-agent/src/agent_loop/mod.rs`
2. `crates/ava-agent/src/agent_loop/tool_execution.rs`
3. `crates/ava-agent/src/agent_loop/response.rs`
4. `crates/ava-agent/src/agent_loop/attachment_state.rs`
5. `crates/ava-agent/src/agent_loop/sidechain.rs`

Success criteria:

1. The main runtime loop is materially smaller and delegates to named helpers/phases.
2. Streaming, tool execution, recovery/compaction, and completion logic are easier to trace independently.
3. Existing `ava-agent` tests still pass.

Progress:

1. Landed. `ava-agent` now has an explicit `context_recovery` phase and a smaller main runtime path. See `CHANGELOG.md` for implementation details.

### Mission B — Unify Runtime Assembly

Goal:

1. Reduce drift between TUI, web, desktop, and headless startup/run wiring by centralizing more `AgentStack` construction and run-path setup.

Target areas:

1. `crates/ava-agent/src/stack/mod.rs`
2. `crates/ava-agent/src/stack/stack_run.rs`
3. `crates/ava-agent/src/stack/stack_tools.rs`
4. `crates/ava-tui/src/headless/`
5. `crates/ava-tui/src/web/`
6. `src-tauri/src/`

Success criteria:

1. Fewer duplicated runtime/tool-registry setup paths exist.
2. Surface-specific startup code becomes thinner.
3. Behavior stays consistent across TUI, web, desktop, and headless modes.

Progress:

1. Landed. Runtime assembly now goes through shared `AgentStackConfig` presets across TUI, web, desktop, headless, and benchmark/review paths. See `CHANGELOG.md` for implementation details.

### Mission C — Improve Debuggability

Goal:

1. Make runtime behavior easier to understand from logs, traces, and structured events.

Target areas:

1. `crates/ava-agent/src/agent_loop/`
2. `crates/ava-agent/src/stack/`
3. `crates/ava-tui/src/state/agent.rs`
4. `crates/ava-tui/src/web/api_agent.rs`
5. `src-tauri/src/bridge.rs`

Success criteria:

1. Important runtime phases have clearer tracing boundaries.
2. Failures and recovery paths are easier to distinguish in logs.
3. Developers can answer “what happened?” without reading multiple unrelated modules.

Progress:

1. Landed. Core runtime tracing is now wired through the shared JSONL run-trace path, and desktop no longer relies on ad-hoc `/tmp` debugging. See `CHANGELOG.md` for implementation details.

### Mission D — Harden Risky Systems With Tests

Goal:

1. Add stronger regression coverage to the parts most likely to fail in subtle, expensive ways.

Target areas:

1. `crates/ava-llm/src/providers/`
2. `crates/ava-llm/tests/`
3. `crates/ava-tools/src/edit/`
4. `crates/ava-tools/tests/`
5. `crates/ava-permissions/src/`
6. `crates/ava-permissions/tests/`

Success criteria:

1. Provider parsing has better fixture/stream coverage.
2. Edit-engine behavior is covered by stronger regression tests.
3. Permission classification and inspection paths have broader safety coverage.

Progress:

1. Landed. Risky-system coverage now includes Anthropic stream parsing, permission-engine safety behavior, and speculative edit-engine edge cases. See `CHANGELOG.md` for implementation details.

### Mission E — Simplify Settings and UI Surface

Goal:

1. Continue collapsing grouped legacy UI into real sections and reduce special-case state branches.

Target areas:

1. `src/components/settings/`
2. `src/stores/settings/`
3. `src/components/chat/`
4. `src/components/layout/`

Success criteria:

1. `Models` and `Tools` move closer to true merged sections.
2. Settings/search/navigation reflect real content structure instead of old tab leftovers.
3. Chat/layout components carry fewer legacy branches.

Progress:

1. Landed. The settings shell is materially simpler: Skills owns rules/commands, deep-linking and search now target real sections, and `Permissions & Trust` is a true unified surface. See `CHANGELOG.md` for implementation details.

### Mission F — Naming, Ownership, and Cleanup Sweep

Goal:

1. Remove legacy terminology, stale abstractions, and ambiguous ownership boundaries.

Target areas:

1. `crates/ava-agent/`
2. `crates/ava-tui/`
3. `src/`
4. `src-tauri/`
5. `docs/`

Success criteria:

1. Module names/comments/docs match the current architecture.
2. Dead abstractions and stale compatibility code are minimized.
3. Ownership boundaries are easier to infer from file layout and naming alone.

Progress:

1. Landed. Dead compatibility layers, stale naming, and leftover ownership confusion were removed across frontend, runtime, and docs surfaces. See `CHANGELOG.md` for implementation details.

## Next

1. Design plugin registration for plugin-owned settings, commands, routes, events, and UI surfaces.
2. Design provider unification for route and region variants inside one provider entry.
3. Decide which advanced extension surfaces stay visible by default and which become toggleable.
4. Define onboarding as an optional in-product guide instead of a separate flow.

## Later

1. Move long-tail providers into installable provider packs.
2. Revisit desktop/web parity after the plugin boundary work is defined.
3. Audit remaining docs and delete anything that does not match AVA 3.3.

## Decisions Locked

1. HQ is no longer core AVA product. It becomes an installable plugin.
2. Core settings collapse to `General`, `Models`, `Tools`, `Permissions`, `Appearance`, `Advanced`.
3. Official core providers are limited and actively tested/tuned.
4. Provider variants stop appearing as separate providers.
5. Core visible customization surface centers on `MCPs`, `Commands`, and `Skills`.
6. Plugins remain part of AVA's core identity, but plugin-owned UX appears only when installed.
7. AVA branding shifts from "AI dev team" to a practical solo-first coding agent.
8. Onboarding becomes optional and reuses the main product UI.

## Not In Scope For Core 3.3

1. Reintroducing HQ into the default product surface.
2. Keeping long-tail providers in core without official support quality.
3. Preserving the current large settings model.
4. Preserving stale docs just because they exist.
