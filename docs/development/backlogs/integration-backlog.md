# Integration Backlog

> Cross-cutting frontend-backend work only. Updated 2026-02-26.

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

- Provider OAuth matrix completion is still required for full sprint closeout confidence.

## Done

- Streaming jitter stabilization for chat start/end transitions.
- Plugin SDK + test utilities (Sprint 10): `createMockExtensionAPI()`, provider test harness, 5 example plugins, `PLUGIN_SDK.md`.
- Remote plugin catalog with fetch + cache + fallback (Sprint 10): `PluginCatalogItem` extended with `repo`, `downloadUrl`, `readme` fields.
- CLI scaffold updated (Sprint 10): generates `ExtensionAPI`-based source, `ava-extension.json` manifest, and test file.
