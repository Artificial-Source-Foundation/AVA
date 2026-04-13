# Changelog

All notable changes to AVA are documented in this file.

## [Unreleased]

### Changed
- Fixed the `prompt_regression_wrong_first_edit_recovery` fixture so it now materializes as a standalone Cargo package with its own `Cargo.toml`, which removes the parent benchmark-workspace trap from `cargo test` and makes recovery benchmarking reflect model behavior instead of workspace leakage.
- Clarified the Alibaba+GLM recovery overlay after a deeper benchmark read: recovery tasks do not ask the model to intentionally make a bad first edit, so hosted GLM models are now told to attempt the most likely correct fix first and only enter recovery mode when verification disproves that fix.
- Refined the Alibaba+GLM recovery overlay again for Rust benchmark fixtures: hosted GLM models are now told to treat the fixture-root project test command as the source of truth and to avoid trusting stale `test_runner`/`test_bin` style binaries during recovery.
- Tightened the Alibaba+GLM provider-family overlay again based on repeat-run failures: after a bad first edit, hosted GLM models are now steered to use the failing assertion as the source of truth, avoid guessing a second implementation, and rerun the same verification command instead of inventing alternate test binaries or wrapper scripts.
- Added a narrow Alibaba+GLM prompt overlay based on prompt-regression failures so hosted GLM models stop over-exploring after locating the target file, recover more directly after a failed verification, and stay focused on edit-then-verify loops; also hardened the `read_before_edit` prompt-regression task to score real verification tool use instead of requiring literal `cargo test` transcript text.
- Tightened benchmark TPS reporting so wall-clock throughput is labeled explicitly, solo benchmark paths now include sub-agent token usage in their token totals, HQ harness runs forward real worker token/cost usage instead of relying on fabricated worker token splits, and solo benchmark reports now also emit TTFT-normalized `generation_tps` plus a `GenTok/s` table column.
- Tightened project trust boundaries again: untrusted projects no longer auto-discover/start `.ava/plugins`, and trusted project instruction loading no longer inherits ancestor `AGENTS.md` files from outside the explicitly trusted project root.
- Added a first-class runtime skills listing path: `/skills` now shows the live filesystem-discovered `SKILL.md` set (global plus trust-gated project-local skills) using the same discovery model as prompt assembly, with matching TUI/headless help and command-surface updates.
- Followed up the runtime-skills milestone with tighter project-local `@include` boundaries, a flat-layout `.../skills/SKILL.md` naming fix, and a lightweight headless `/skills` path that avoids unnecessary full app startup.
- Continued modularizing the benchmark runtime for better DX: shared Tier 2 code extraction/compile-test validation now lives in `benchmark_validation.rs`, shared LLM-as-judge logic now lives in `benchmark_judge.rs`, shared benchmark/harness table rendering now lives in `benchmark_format.rs`, both benchmark runners reuse the shared validation path, and benchmark/judge output truncation plus timestamp rendering are now hardened against UTF-8 and malformed input panics.
- Removed the dead bundled-skill registry/types module from `ava-agent` so the live skill surface is unambiguous again: slash commands remain their own wired command system, while skills are documented and implemented strictly as filesystem `SKILL.md` discovery in `instructions.rs`.
- Prompt-note resolution now supports a separate provider-name dimension (in addition to `ProviderKind`) so provider-family overlays can be layered on top of model-family notes; initial support adds a lean Alibaba+Kimi overlay while preserving existing behavior for providers without overlays.
- Hardened loop-prone provider coverage by adding Kimi-specific stuck-detector regression tests (`for_provider_model("kimi", "k2p5")` plus a `nudge, nudge, stop` similarity-loop check), and fixed benchmark score-input handling so code-task `task_pass` no longer treats compile success alone as a resolved pass when tests fail.
- Added `docs/testing/` and `docs/operations/` sections plus a benchmark deep-dive page, and expanded project/docs navigation so benchmark, validation, testing, and maintainer runbook material now have first-class homes in the docs tree.
- Added a dedicated `docs/benchmark/` section with overview, suite/workflow, report/comparison, and prompt-benchmarking entry pages so benchmark behavior and usage now live in a first-class docs section instead of only project planning pages.
- Added the first provider/system-prompt benchmark runtime slice: benchmark mode now accepts prompt-family/variant/file/version/hash metadata, repeat + seed controls, optional benchmark output paths, raw-per-run plus aggregate report artifacts, benchmark-to-agent prompt override plumbing, and generic left/right report comparison flags (with legacy AVA/OpenCode aliases preserved).
- Added 3.3.1 eval Round 7 first `prompt_regression` benchmark lane: six prompt-sensitive Tier 3 tasks (verify-before-finish, targeted edit only, minimal patch, read-before-edit, wrong-first-edit recovery, and tool-choice/subagent discipline), plus suite/category wiring and deterministic workspace setup + validation integration.
- Added `docs/project/provider-prompt-benchmarking.md`, then expanded it into current implementation/usage documentation for the shipped 3.3.1 prompt-benchmark workflow, including live CLI flags, prompt-regression coverage, repeat-run behavior, comparison flow, and remaining gaps.
- Fixed the 3.3.1 benchmark runner workspace scoping so benchmark and harness tasks run against `~/.ava/benchmarks/workspace` as their actual tool workspace instead of falling back to the repo root, which unblocked frontier Tier 3 tasks that read and edit benchmark fixture files.
- Fixed background sub-agent spawning so optional background helpers (such as a review pass) can run once when allowed without immediately tripping the internal delegation budget check.
- Tightened benchmark result semantics so code-task quality now requires Tier 2/3 validation success instead of allowing regex-only matches to report `PASS` when tests fail, and relaxed overly implementation-specific recovery/maintenance regex expectations to accept equivalent correct fixes.
- Added 3.3.1 eval Round 6 first AVA-vs-OpenCode comparison runner in `ava-tui`: it loads two existing benchmark JSON reports, aligns tasks by name, computes per-side aggregate stats plus time/cost savings and win counts, prints a readable CLI summary, and can optionally emit a structured comparison JSON artifact.
- Added 3.3.1 eval Round 5 first real `lsp_smoke` and `product_smoke` benchmark lanes with deterministic Tier 3 fixtures and validation (LSP config/project/toolchain smoke plus session/config/tool-discovery/permission flows), without implying unsupported full LSP client behavior.
- Added 3.3.1 eval Round 4 first real `mcp_integration` benchmark lane with deterministic local stdio mock MCP servers for filesystem, git, and multi-server workflows, including project-local `.ava/mcp.json` setup and audit-log-backed validation.
- Added 3.3.1 eval Round 3 `tool_recovery` coverage with real Tier 3 tasks (missing-file recovery, targeted edit recovery, and verification-discipline), plus workspace setup and validation wiring for the new lane.
- Expanded the 3.3.1 benchmark corpus with the first real project-scale coding tasks for `small_coding`, `stress_coding`, `large_project`, `test_heavy`, and `maintenance`; added corresponding Tier 3 workspace setup/validation wiring (including stricter test-focused validation for the new `test_heavy` task).
- Reorganized the active docs set into clearer purpose-based sections: roadmap and backlog now live under `docs/project/`, extension and credential references now live under `docs/reference/`, `docs/README.md` now explains the docs taxonomy by audience, and the root `README.md` plus contributor compatibility entrypoints were updated to match the new structure.
- Expanded the docs so the codebase is represented as a usable retrieval layer: added dedicated references for providers/auth and command surfaces, split extension docs into page-sized sections for plugins, MCP, commands/hooks, tools, and instructions, added architecture entrypoint and contributor workflow docs, and corrected stale path and trust-model details to match the current runtime.
- Made the active docs website-ready: added consistent frontmatter across active pages, added section `_meta.json` navigation manifests, created a troubleshooting index page, and aligned sidebar ordering so the Markdown can be imported into a future docs site with minimal extra work.
- Tightened release-hardening coverage around current 3.3 routing and TUI behavior: cheap-route tests now assert against the computed cheapest configured candidate, stale TUI interaction assertions were updated to the current UI contract, and the release verification path is green again across Rust and frontend checks.
- Cleaned the standard verification path by fixing the current frontend lint/reactivity warnings and clearing the remaining workspace clippy warnings in `ava-plugin`, `ava-acp`, and `ava-tui`.
- Made secure credential handling the default runtime path: desktop sync now writes to the Rust secure store, normal settings persistence no longer serializes raw provider API keys, and startup prefers secure storage while still reading an existing plaintext `~/.ava/credentials.json` for compatibility.
- Reworked onboarding into an optional in-app guide instead of a startup gate, with a reopen entry in Settings > General.
- Moved plugin and MCP management into `Advanced` by default so the main settings surface stays focused on core product configuration.
- Started the HQ plugin reintroduction groundwork from the plugin side: `ava-hq` now ships a small `ava-hq-plugin` binary and `plugins/examples/ava-hq/` local-link artifact, but that track is now deferred behind the future roadmap while current work stays focused on core AVA.
- Normalized provider route/region aliases back into the canonical core provider surface: the repo-owned fallback model catalog now collapses legacy coding-plan/region variants into their main providers, and the frontend model/docs helpers now resolve those old IDs through the same canonical paths instead of treating them like separate providers.

## [3.3.0] — 2026-04-07

### Changed
- Added a Claude Code auth and resume helper path in `ava-acp`: CLI discovery now surfaces cached Claude auth metadata, the Claude SDK adapter can refresh local Claude OAuth credentials and retry once on token-expiry style startup failures, and the direct ACP provider path now persists conversation-prefix session mappings so `cli:claude-code` can resume prior Claude sessions across turns and process restarts.
- Reset the docs tree around the AVA 3.3 baseline: active architecture docs now live under `docs/architecture/`, release docs under `docs/contributing/`, historical analysis under `docs/archive/research/`, and compatibility files like `CLAUDE.md`, `llms.txt`, and `CODEBASE_STRUCTURE.md` now point back to the active docs instead of preserving stale architecture snapshots.
- Clarified the repo source-of-truth model: `AGENTS.md` now owns workflow and architectural guidance, `docs/ava-3.3-plan.md` owns product direction, `docs/backlog.md` tracks active work, and the docs/readme/contributing entrypoints now reflect that split cleanly.
- De-cored HQ from the default product surface: core settings, sidebar, chat, desktop command, web-route, and event-contract paths no longer assume HQ exists; `src-tauri` no longer has a live direct `ava-hq` dependency path; `ava hq` is gone from the default CLI; and the remaining core HQ baggage is limited to benchmark-only linkage plus historical DB migrations kept for compatibility.
- Narrowed the core/plugin boundary further by moving review logic into `ava-review`, moving `hq_roles` into `ava-hq`, removing `config.hq` from core config, and documenting the remaining plugin-boundary work through `docs/architecture/plugin-boundary.md`.
- Reduced the core provider surface to the official AVA 3.3 set, removed long-tail provider files and stale branding/config defaults, and tightened provider routing/alias handling around the canonical provider inventory.
- Simplified the settings and frontend surface around the 3.3 model: the desktop shell now centers on the six core settings sections, Skills owns rules/commands directly, `Permissions & Trust` is a true unified section, and the remaining team/director-only UI branches and preset assumptions are gone from core frontend state.
- Continued the TUI refresh with a calmer premium shell, cleaner chat/composer/layout hierarchy, built-in theme updates, live LSP/MCP sidebar state, and follow-up layout hardening plus regression coverage for recent wrap/selector bugs.
- Improved core runtime maintainability by splitting `ava-agent` execution into clearer phases, centralizing `AgentStackConfig` presets across product surfaces, and wiring the shared JSONL run-trace path through TUI, web, and desktop flows.
- Hardened risky systems with stronger regression coverage in `ava-llm`, `ava-permissions`, and `ava-tools`, including fixes for real edge-case bugs found while adding those tests.
- Removed additional stale compatibility layers and naming drift across frontend/runtime surfaces, including dead shims, stale type exports, and the obsolete `useChat` wrapper.
- Normalized remaining extension/MCP boundaries: MCP now uses `McpManager` terminology instead of the stale `ExtensionManager` name, `ava-extensions` is documented as a separate native/WASM descriptor surface, the unused desktop `AppState` extension manager state was removed, and the historical HQ SQL migrations are now explicitly marked as compatibility-only.
- Trimmed another batch of real dead code from core paths: removed unused desktop/web helpers, deleted the unreferenced legacy single-command permission classifier, dropped the redundant stored `yolo` field from `AgentStack`, and narrowed the remaining `#[allow(dead_code)]` usage to intentional compatibility or future-facing placeholders.
- Expanded benchmarks with explicit `tool_reliability` and `normal_coding` lanes, plus scoring that separates tool errors from coding-quality regressions.
- Removed runtime dependence on `models.dev`: backend and frontend now use the repo-owned curated `list_models` catalog path (`curated-model-catalog`) instead of network catalog enrichment.
- Split prompt notes out of Rust source into `crates/ava-agent/src/prompts/families/` and `prompts/providers/`, keeping `system_prompt.rs` focused on family detection + assembly.
- Started the family prompt-tuning loop on top of the new benchmark lanes (GPT-family first), and tightened family detection to avoid false Claude/MiniMax matches.
- Refined benchmark signal quality by fixing streamed tool-argument snapshot merging (replace growing JSON snapshots instead of concatenating), which removed Gemini parser noise from tool-reliability runs.
- Added explicit credential-storage guidance: plaintext `~/.ava/credentials.json` remains supported but is not the preferred model when keychain/encrypted or env-var paths are available.
- Introduced and refined model-specific doom-loop policy: loop-prone models now follow `nudge, nudge, stop`, with follow-up hardening for cooldown cost accounting, empty-response safety, hidden judge nudges, UTF-8-safe truncation, and escalation reset only after real progress.

### Fixed
- Restored the baseline repository CI path by refreshing the workspace `pnpm-lock.yaml`, updating `cargo-deny` config for the current schema and license set, tightening rustdoc comments that failed under `RUSTDOCFLAGS=-D warnings`, and reducing `typos` noise from repo-specific fixtures and design artifacts.
- Consolidated message-list overflow handling into shared safe-render helpers, fixing duplicated truncation logic and keeping alignment behavior consistent.
- Cleared a strict clippy regression in `ava-config` by replacing a manual early-return match with `let ... else`.
- Kept the TUI animation refresh compatible with Ratatui's immediate-mode renderer by staying inside the existing tick-driven color/spinner path.

## [3.0.0] — 2026-03-30

Note: this section reflects the product at the 3.0.0 release. Since then, the 3.3 simplification pass has removed HQ from the default core product surface, reduced the official provider surface, and collapsed the settings model. Use `[Unreleased]` above for the current baseline.

AVA v3 — the complete rewrite. Pure Rust backend, 21 crates, 21 LLM providers,
9 default tools (all stress-tested at 0% error rate), HQ multi-agent orchestration,
macOS-luxury SolidJS desktop, and a web serve mode with 65+ REST endpoints.

**Highlights:**
- **Pure Rust architecture** — 40K LOC, 1,962+ tests, 0 failures
- **22 LLM providers** — Anthropic, OpenAI, Gemini, Copilot, DeepSeek, Inception, and 16 more
- **13 model families** — per-model system prompt tuning for Claude, Codex, GPT, Gemini, DeepSeek, Mercury, Grok, GLM, Kimi, MiniMax, Qwen, Mistral, Local
- **9 default tools** — read, write, edit, bash, glob, grep, web_fetch, web_search, git_read — all verified via CLI smoke tests (0% error rate)
- **HQ multi-agent** — Director → Scouts → Leads → Workers with LLM-powered planning and Board of Directors
- **Desktop app** — SolidJS + Tauri with 15 settings tabs, 25 slash commands, 19+ keyboard shortcuts
- **Web mode** — `ava serve` with 65+ REST API endpoints, WebSocket streaming, full session/HQ CRUD
- **Two products, one backend** — TUI/CLI and Desktop/Web share config.yaml, credentials.json, and the Rust agent runtime
- **3-tier mid-stream messaging** — Steer (Enter), Follow-up (Alt+Enter), Post-complete (Ctrl+Alt+Enter)
- **E2E tested** — 182 Playwright UI tests, 65 API endpoint tests, 25 CLI slash command tests, 0 tool errors across all models
- **Instant startup** — TUI loads in <40ms (deferred codebase indexing + CLI agent discovery)

This release included the full Rust-first rewrite, the large desktop/web surface expansion, the old built-in HQ product path, and the pre-3.3 provider/settings surface.

Detailed 3.0.0 implementation history has been intentionally collapsed so the current 3.3 baseline remains the primary readable state in this changelog.

Detailed per-sprint history for the removed 2.2.x-3.1.x docs changelog is recoverable from git history if older implementation context is needed.

## [2.1.0] - 2026-03-08

Release polish, documentation, and E2E test matrix.

### Added
- E2E test matrix (`docs/development/test-matrix.md`) — 19 tools, 5 modes, 3 providers verified
- Version badge and smoke test command in README

### Changed
- Workspace version bumped to 2.1.0
- README refreshed with current architecture and test verification line
- docs/README.md updated with test-matrix link and corrected sprint references
- CLAUDE.md and AGENTS.md verified and updated with v2.1 version reference

## [2.0.0] - 2026-03-05

### Breaking Changes
- Backend runtime finalized on `core-v2` + extension architecture; legacy `packages/core/` reduced to compatibility shim behavior.
- Tool surface reduced from roughly 55 to ~39 tools.
- Extension surface reduced from 37 to 20 built-in modules.

### Performance
- Edit cascade reliability improved to target >85% first-pass replacement success.
- Streaming UX latency reduced for sub-500ms incremental updates.
- Startup path tuned for <1s warm-start target on desktop.
- Rust hotpaths expanded for grep, edit, validation, permissions, memory, and sandbox routing.

### Added
- Hybrid v2 architecture finalized around `core-v2` + extension-first runtime.
- Rust hotpath dispatch pattern (`dispatchCompute`) wired across compute, context, safety, and validation paths.
- Multi-tier edit cascade and streaming edit parsing support.
- Context intelligence upgrades: repo-map ranking, tiered compaction thresholds, post-edit diagnostics middleware.
- Agent reliability middleware for stuck-loop detection, recovery retries, and completion validation.
- Sandbox policy routing and checkpoint refs under `refs/ava/checkpoints/`.
- Desktop UX upgrades including token streaming debounce and hunk-level diff review controls.
- PageRank-style repository mapping for context prioritization.
- Dynamic permission learning with dangerous-command generalization safeguards.
- Git checkpoint references with pre-destructive operation capture.
- MCP plugin support with namespaced tool execution paths.
- 3-tier compaction strategy for long-running conversations.

### Changed
- Plugin SDK examples aligned with exported `core-v2` APIs and middleware return contracts.
- E2E harness defaults to real backend unless explicitly mocked.
- Documentation updated to current hybrid architecture and v2 release state.

### Fixed
- Known flaky tests in chat integration and extension loader suites.
- Permission learning safeguards to prevent dangerous command over-generalization.

### Removed
- Migration-era backlog and architecture docs no longer relevant to v2 release.
