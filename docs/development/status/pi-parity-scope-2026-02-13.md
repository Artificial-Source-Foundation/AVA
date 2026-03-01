# PI Parity Scope (Sprint 1.6 Ticket 09)

Updated: 2026-02-28

## Objective

Define a concrete, prioritized parity scope versus PI Coding Agent so implementation can be scheduled into Sprint 2.x without ambiguity.

## Status: ALL ITEMS COMPLETE

All P0 and P1 parity items have been implemented.

## Priority Buckets

### P0 (MVP-adjacent parity) — DONE

1. Mid-session provider switching
   - **DONE** (Sprint B9: `requestProviderSwitch()` on AgentExecutor)

2. Minimal tool mode
   - **DONE** (Sprint B8: 9-tool subset, per-session state, plan mode pattern)

### P1 (workflow parity) — DONE

3. Session branching tree UI
   - **DONE** (Gap Analysis Batch 6: `SessionBranchTree.tsx`, `parentSessionId` tracking, tree/list toggle in sidebar, `getSessionTree()` computed)

4. Runtime skill creation UX
   - **DONE** (Gap Analysis Batch 5: Custom skill CRUD in `MicroagentsTab.tsx` — create form with name/description/file globs/instructions, edit/delete on custom skill cards, `customMicroagents` in settings store)

## Milestones — All Complete

| Milestone | Scope | Status |
|---|---|---|
| M1 | Provider switching + minimal tool mode | **Done** (Sprint B8/B9) |
| M2 | Session branching tree | **Done** (Gap Analysis Batch 6) |
| M3 | Runtime skill creation UX | **Done** (Gap Analysis Batch 5) |
