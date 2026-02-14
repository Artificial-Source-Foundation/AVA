# Epic: Plugin Ecosystem UX and Integration

Status update date: 2026-02-14

## Objective

Deliver the first usable plugin management experience in the desktop app by connecting existing backend extension foundations to stable frontend plugin UX.

## Current Status

- Status: in progress
- Backend foundation: mostly done (manifest, lifecycle manager, persistence)
- Frontend UX: partial (settings plugin manager MVP exists, wiring and catalog depth still pending)

## Done

- Manifest parsing/validation implemented in `packages/core/src/extensions/manifest.ts`
- Lifecycle core implemented in `packages/core/src/extensions/manager.ts`
- Extension storage + tests implemented in `packages/core/src/extensions/storage.ts`
- Settings plugin manager MVP shell and smoke test present:
  - `src/components/settings/tabs/PluginsTab.smoke.test.tsx`

## In Progress

- Frontend plugin management flow refinement in settings surface
- Prioritization of plugin UX sprint execution sequencing

## Next

1. Wire real backend lifecycle actions to install/enable/disable/uninstall controls.
2. Add featured/search/catalog UX polish and metadata surfaces.
3. Define SDK packaging/docs pass for external plugin creators.
4. Document sandbox policy hardening requirements before broader distribution.

## Dependencies

- Stable extension lifecycle API contract from `packages/core/src/extensions/manager.ts`
- Frontend settings/sidebar integration capacity in current sprint plan
- Plugin metadata model and source of truth for featured/search listing

## Exit Criteria

- End-to-end plugin lifecycle actions work from desktop UI.
- Plugin management UX is discoverable and stable for MVP users.
- SDK and sandboxing requirements are documented and tracked.

## Evidence Sources

- `docs/ROADMAP.md`
- `docs/frontend/backlog.md`
- `docs/development/mvp-readiness-report-2026-02-13.md`
