---
title: "Backlog"
description: "Active priorities for the AVA 3.3 core baseline and the 3.3.1 validation follow-up."
order: 3
updated: "2026-04-13"
---

# AVA Backlog

This backlog tracks the current AVA core execution work.

Source of truth for direction: `docs/project/roadmap.md`

## Now

1. Stand up the `3.3.1` validation program for core AVA.
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

Recent core progress:

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
