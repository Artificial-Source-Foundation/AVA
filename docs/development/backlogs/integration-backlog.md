# Integration Backlog

> Cross-cutting frontend-backend work only. Updated 2026-02-27.

## Active

- `INT-001` Plugin lifecycle wiring (settings UI controls -> extension manager actions)
  - Status: in progress (frontend wiring landed; runtime validation pending)
  - Owners: frontend + backend
  - Frontend links: `docs/frontend/backlog.md`
  - Backend links: `docs/backend/backlog.md`
  - Exit evidence: install/enable/disable/uninstall flows succeed in desktop runtime.

- `INT-002` Plugin state synchronization and optimistic UX behavior
  - Status: in progress (optimistic/reconcile baseline landed; stress validation pending)
  - Owners: frontend + backend
  - Frontend links: `docs/frontend/backlog.md`
  - Backend links: `docs/backend/backlog.md`
  - Exit evidence: UI state remains consistent under fast toggles, retries, and action failures.

- `INT-003` Plugin lifecycle failure handling and recovery UX
  - Status: in progress (retry/recovery baseline landed; runtime failure evidence pending)
  - Owners: frontend + backend
  - Frontend links: `docs/frontend/backlog.md`
  - Backend links: `docs/backend/backlog.md`
  - Exit evidence: actionable error states and retry/recover flows validated.

## Blockers

- Provider OAuth matrix completion is still required for full sprint closeout confidence (OpenAI + Anthropic remaining; Copilot backend now wired).

## Done

- All 14 stub extensions wired with real logic (Sprint 11): slash-commands (8 commands), models (registry), focus-chain (tracker), scheduler (task runner), diff (middleware), instructions (loader), skills (matcher), git (snapshots), custom-commands (parser), mcp (manager wired), codebase (indexer), lsp (availability check), integrations (search providers), sandbox (Docker runner). +107 tests, 12 new helper modules.
- Build fix (Sprint 11): excluded test utils from production builds in core-v2 and extensions tsconfigs.
- Dead code cleanup (Sprint 11): deleted `anthropic-oauth.ts`, `SidebarPlugins.tsx`, `ProvidersTab.tsx`.
- GitHub Copilot provider extension wired end-to-end: backend `CopilotClient` (OAuth + custom header), frontend defaults (Github icon, real model IDs), dynamic model fetcher with fallback, test harness (3 tests).
- Streaming jitter stabilization for chat start/end transitions.
- Plugin SDK + test utilities (Sprint 10): `createMockExtensionAPI()`, provider test harness, 5 example plugins, `PLUGIN_SDK.md`.
- Remote plugin catalog with fetch + cache + fallback (Sprint 10): `PluginCatalogItem` extended with `repo`, `downloadUrl`, `readme` fields.
- CLI scaffold updated (Sprint 10): generates `ExtensionAPI`-based source, `ava-extension.json` manifest, and test file.
