# Integration Backlog

> Cross-cutting frontend-backend work only. Updated 2026-02-14.

## Active

- `INT-001` Plugin lifecycle wiring (settings UI controls -> extension manager actions)
  - Status: in progress
  - Owners: frontend + backend
  - Frontend links: `docs/frontend/backlog.md`
  - Backend links: `docs/backend/backlog.md`
  - Exit evidence: install/enable/disable/uninstall flows succeed in desktop runtime.

- `INT-002` Plugin state synchronization and optimistic UX behavior
  - Status: in progress
  - Owners: frontend + backend
  - Frontend links: `docs/frontend/backlog.md`
  - Backend links: `docs/backend/backlog.md`
  - Exit evidence: UI state remains consistent under fast toggles, retries, and action failures.

- `INT-003` Plugin lifecycle failure handling and recovery UX
  - Status: in progress
  - Owners: frontend + backend
  - Frontend links: `docs/frontend/backlog.md`
  - Backend links: `docs/backend/backlog.md`
  - Exit evidence: actionable error states and retry/recover flows validated.

## Blockers

- Provider OAuth matrix completion is still required for full sprint closeout confidence.

## Done

- Streaming jitter stabilization for chat start/end transitions.
