# Changelog

All notable changes to AVA are documented in this file.

## [Unreleased]

### Changed
- Reorganized the active docs set into clearer purpose-based sections: roadmap and backlog now live under `docs/project/`, extension and credential references now live under `docs/reference/`, `docs/README.md` now explains the docs taxonomy by audience, and the root `README.md` plus contributor compatibility entrypoints were updated to match the new structure.
- Expanded the docs so the codebase is represented as a usable retrieval layer: added dedicated references for providers/auth and command surfaces, split extension docs into page-sized sections for plugins, MCP, commands/hooks, tools, and instructions, added architecture entrypoint and contributor workflow docs, and corrected stale path and trust-model details to match the current runtime.
- Made the active docs website-ready: added consistent frontmatter across active pages, added section `_meta.json` navigation manifests, created a troubleshooting index page, and aligned sidebar ordering so the Markdown can be imported into a future docs site with minimal extra work.
- Tightened release-hardening coverage around current 3.3 routing and TUI behavior: cheap-route tests now assert against the computed cheapest configured candidate, stale TUI interaction assertions were updated to the current UI contract, and the release verification path is green again across Rust and frontend checks.
- Cleaned the standard verification path by fixing the current frontend lint/reactivity warnings and clearing the remaining workspace clippy warnings in `ava-plugin`, `ava-acp`, and `ava-tui`.

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
