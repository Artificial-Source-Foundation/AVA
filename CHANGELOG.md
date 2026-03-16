# Changelog

All notable changes to AVA are documented in this file.

## [Unreleased]

### Removed
- **Deleted `packages/` directory entirely.** The TypeScript desktop layer (`packages/core-v2/`, `packages/extensions/`, `packages/core/`, `packages/platform-node/`, `packages/platform-tauri/`) has been removed. The desktop app now calls Rust crates directly via Tauri IPC commands (`src-tauri/src/commands/`), eliminating the `dispatchCompute` bridge pattern and all Node.js runtime dependencies from the desktop path.

### Changed
- Desktop frontend (SolidJS) retained in `src/` but all backend logic is now pure Rust via `src-tauri/`.
- The `dispatchCompute` pattern is no longer present anywhere in the codebase (previously deprecated for new work, now fully removed).

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
