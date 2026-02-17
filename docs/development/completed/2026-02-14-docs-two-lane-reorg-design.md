# Two-Lane Docs Reorganization Design

**Date:** 2026-02-14
**Status:** Approved
**Owner:** AVA docs maintenance

## Goal

Adopt a two-lane documentation model that keeps canonical status docs concise and accurate while moving daily planning/execution detail into active sprint and integration backlog docs.

## Why This Change

Current docs contain status drift across roadmap, backlog, and sprint files (for example, streaming jitter status differs between docs). The team also needs clearer visibility for frontend-backend wiring work as frontend nears completion.

## Scope

### In Scope (Normalization Now)

- Resolve status drift in canonical docs.
- Mark streaming jitter stabilization as complete where stale.
- Keep frontend-backend lifecycle wiring explicitly in progress until end-to-end UI-to-backend wiring is complete.
- Introduce integration backlog and current focus pulse docs.

### In Scope (Structural Redesign Next Sprint)

- Add sprint execution docs under `docs/development/sprints/`.
- Add consistent sprint template and update cadence.
- Keep `docs/ROADMAP.md` concise and link-driven.

### Out of Scope

- Full docs site redesign.
- Rewriting every historic epic doc.
- Automatic strict CI failure on docs drift.

## Architecture: Two-Lane Docs System

### Lane 1: Canonical Truth (Stable)

- `docs/ROADMAP.md`
- `docs/frontend/backlog.md`
- `docs/backend/backlog.md`
- `docs/development/epics/*.md`

Rules:

- Keep high signal only (status, priorities, dependencies, exits).
- Do not store daily execution logs here.
- Each item has one owner source (frontend, backend, or integration).

### Lane 2: Active Execution (Frequently Updated)

- `docs/development/sprints/YYYY-S<id>-<topic>.md`
- `docs/development/backlogs/integration-backlog.md`
- `docs/development/status/current-focus.md`

Rules:

- Store live work state, blockers, sequencing, and evidence links.
- Link each execution item back to canonical owner doc.
- Keep current-focus doc as weekly pulse for active priorities.

## Freshness Protocol

1. **Single owner per item:** every task appears as source-of-truth in one backlog.
2. **Sprint close checklist:** cannot mark sprint done until evidence and status sync are complete.
3. **PR docs checkbox:** every PR must update docs or declare docs N/A with reason.
4. **Weekly pulse update:** update `current-focus.md` each week.
5. **Advisory drift guard:** CI warns on contradictory statuses across key docs.

## Rollout Plan

### Phase A: Normalization Sprint

- Align roadmap/backlog/epic statuses.
- Correct streaming jitter status to done where stale.
- Keep plugin lifecycle wiring as in progress with explicit ownership and next actions.
- Create integration backlog and current-focus docs.

### Phase B: Structural Redesign Sprint

- Create sprint docs folder and template.
- Create new sprint docs for plugin lifecycle wiring and docs architecture hardening.
- Update roadmap to link to active sprint docs.

## Risks and Mitigations

- **Risk:** duplicate statuses across docs.
  - **Mitigation:** single owner source, link-only references elsewhere.
- **Risk:** roadmap bloat.
  - **Mitigation:** keep roadmap concise; move detail to sprint docs.
- **Risk:** docs lag behind code.
  - **Mitigation:** weekly pulse + PR checkbox + advisory drift checks.

## Verification Strategy

- Manual review of statuses across roadmap, frontend backlog, backend backlog, and active sprint docs.
- Validate all "in progress" items include owner and next action.
- Validate every active sprint has exit criteria and evidence links.
- Run advisory drift check script and address warnings.

## Success Criteria

- No status contradictions for top active items (streaming jitter, OAuth manual matrix, plugin lifecycle wiring).
- Canonical docs remain under control and reference active execution docs instead of duplicating detail.
- Integration backlog exists and tracks frontend-backend wiring dependencies clearly.
- Current focus doc reflects active sprint and blockers with last-updated date.
- Team can determine current priorities and blockers within two document hops.
