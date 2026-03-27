# Changelog

All notable changes to AVA are documented in this file.

## [Unreleased]

### Removed
- **Deleted `packages/` directory entirely.** The TypeScript desktop layer (`packages/core-v2/`, `packages/extensions/`, `packages/core/`, `packages/platform-node/`, `packages/platform-tauri/`) has been removed. The desktop app now calls Rust crates directly via Tauri IPC commands (`src-tauri/src/commands/`), eliminating the `dispatchCompute` bridge pattern and all Node.js runtime dependencies from the desktop path.

### Changed
- Desktop frontend (SolidJS) retained in `src/` but all backend logic is now pure Rust via `src-tauri/`.
- The `dispatchCompute` pattern is no longer present anywhere in the codebase (previously deprecated for new work, now fully removed).
- Agent/tool hot paths are more robust: streamed tool calls preserve provider ordering, read-only tool fan-out is bounded, transient retries stop once failures turn permanent, context compaction tracks the latest summary, and in-memory SQLite pools now reuse a single connection safely.
- Instruction and delegation DX are sharper: `.ava/rules` now activate only on direct file touches and dedupe for the session, contextual `AGENTS.md` guidance reloads after compaction instead of spamming every read, hidden subagents only appear on broader tasks with explicit spawn budgets, and scout/plan/review helpers now run in enforced read-only specialist lanes.
- Default tool runtime is leaner and more diagnosable: `grep` walks repos in parallel with deterministic ordering, `glob` avoids per-match metadata calls, missing-file errors suggest sibling paths, and edit failures now report similarity hints plus already-applied detection.
- Custom TOML tools now execute with bash-style env scrubbing, plugin-provided `shell.env` variables, structured stdout/stderr/exit metadata, secret redaction, and disk spillover for oversized output. DuckDuckGo search results now validate redirects, unwrap real target URLs, decode HTML entities, and drop blocked or duplicate results.
- Web fetches and recovery state are more resilient: `web_fetch` now enforces redirect safety as hard failures and streams response bodies under a byte cap instead of buffering arbitrarily large payloads, while file backup version discovery tolerates gaps and shadow snapshots ignore inherited `GIT_*` environment contamination.
- The docs tree is now much leaner: `docs/README.md` points at the small set of live references, stale duplicate docs were removed, and `CLAUDE.md` now points at the current doc locations.
- The TUI is sturdier on long sessions and richer diff output: side-by-side tool diffs now pair multi-line replacements correctly, diffs without summary prefixes still render, message scrolling no longer truncates after 65k visual lines, and the message list avoids an extra full-vector drain on every frame.
- The desktop frontend now tracks tool executions more reliably: Tauri emits real tool call IDs and approval events carry the originating tool call, so out-of-order tool results and repeated approvals no longer update the wrong card. The browser API bridge also unwraps GET args correctly, snake-cases query params, and avoids duplicating path params into the query string.
- Desktop streaming is more resilient across reconnects and long trajectories: stale WebSocket callbacks are ignored after reconnect, timeline events get stable timestamps at ingest time instead of being re-timestamped on every render, and targeted browser-mode tests now cover reconnect and query-param edge cases.
- Desktop state now behaves better over long runs and reloads: streaming event history is bounded instead of growing forever, and session-level window listeners are installed through a replaceable binding so hot reloads do not stack duplicate listeners.
- Desktop session state is safer under rapid switching and reloads: stale async session loads are ignored instead of overwriting the active session, and global `instructions-loaded` / `core-settings-changed` listeners now use replaceable bindings so hot reloads do not duplicate handlers.
- Desktop polish tightened further: remaining instruction/diagnostic listeners now clean up predictably, deep-link plugin cleanup is safe even if initialization resolves late, and chat/tool cards share a single elapsed-time ticker instead of spinning one interval per running card.
- Desktop chat surfaces are sturdier under long use: nested scrollable tool outputs are tracked incrementally instead of re-binding listeners to the whole subtree on every mutation, near-top backfill no longer re-triggers repeatedly for the same hidden-count window, and the focus-chain store now uses a replaceable global listener instead of per-mount subscriptions.
- Desktop session transitions now clear and repopulate the whole per-session side state consistently: creating or auto-switching sessions no longer leaves stale agents/files/checkpoints behind, and deep-link-driven lazy imports now fail through explicit logging instead of silent unhandled promise chains.
- Benchmark infrastructure is cleaner and deeper: shared support helpers are now split into dedicated workspace and validation modules, benchmark runs track hidden subagent usage/cost, the CLI now supports `--task-filter` for one-off benchmark slices, benchmark-mode question prompts get a deterministic fallback answer, the tables print delegation details prominently, and new scenarios cover file-scoped rule following plus delegation-heavy multi-file debugging.

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
