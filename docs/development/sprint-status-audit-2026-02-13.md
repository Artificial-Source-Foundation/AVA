# Sprint Status Audit (2026-02-13)

## Goal
Align roadmap/backlog documentation with current code reality so execution can continue without ambiguity.

## Evidence Summary

### Roadmap + Backlog Signals
- `docs/ROADMAP.md` says Phase 2 (Plugin Ecosystem) is next and Sprint 1.6 is planned.
- `docs/frontend/backlog.md` still marks many capabilities as not started, but also contains notes saying some are already complete.
- `docs/backend/backlog.md` positions testing/parity work as still open (especially OAuth/message flow/logging hardening).

### Codebase Signals
- Backend plugin foundations already exist in `packages/core/src/extensions/`:
  - Manifest/types parsing and validation
  - Install/uninstall/enable/disable/reload lifecycle
  - Extension events and tests (`manager.test.ts`, `manifest.test.ts`, `storage.test.ts`)
- Frontend plugin UX is still placeholder (`src/components/settings/SettingsModal.tsx` plugins tab with "coming in Phase 2").
- Sprint-1.6-related work appears partially present:
  - OAuth coverage exists in `packages/core/src/llm/client.test.ts`
  - Message bus tests exist in `packages/core/src/bus/message-bus.test.ts`
  - Sprint doc `docs/development/sprint-1.6-testing.md` remains a plan/spec with ticket list.

## Mismatches Found

1. **Phase marker mismatch**
   - Docs imply Plugin Ecosystem is fully "next".
   - Code shows backend extension foundation is already implemented.

2. **Backlog consistency mismatch**
   - Frontend backlog marks features as pending while changelog indicates recent completion for several items.

3. **Sprint ambiguity mismatch**
   - Sprint 1.6 is documented as planned, but partial implementation exists across tests and fixes.

## Recommended Current Sprint Position

Use this as the working truth until roadmap/backlog are normalized:

- **Active Sprint: 1.6 (Hardening/Testing) - in progress**
- **Parallel Track: 2.1 backend foundation - substantially implemented**
- **Next Execution Target: 2.2+ frontend plugin UX and integration wiring**

Rationale:
- Current delivery risk is not backend extension primitives; it is verification hardening and frontend integration completion.

## Reorganization Actions Applied

- Created `.opencode/context.md` with a normalized project environment summary.
- Replaced `.opencode/todo.md` with a hierarchical mission checklist and completion states.
- Created `.opencode/work-log.md` for session/state continuity.

## Recommended Immediate Follow-up

1. Update `docs/ROADMAP.md` phase wording to reflect "2.1 backend foundation done/mostly done".
2. Normalize `docs/frontend/backlog.md` by converting already-shipped items to done and isolating true TODOs.
3. Convert `docs/development/sprint-1.6-testing.md` tickets into explicit status rows (todo/in-progress/done, owner, evidence link).
4. Start a focused sprint card for frontend plugin UX (replacement for removed `SidebarPlugins.tsx`).
