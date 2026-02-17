# PI Parity Scope (Sprint 1.6 Ticket 09)

Updated: 2026-02-13

## Objective

Define a concrete, prioritized parity scope versus PI Coding Agent so implementation can be scheduled into Sprint 2.x without ambiguity.

## Priority Buckets

### P0 (MVP-adjacent parity)

1. Mid-session provider switching
   - Why: avoid session restarts when provider/token constraints change.
   - Scope: switch provider/model while preserving session history.
   - Estimate: 2-3 days.

2. Minimal tool mode
   - Why: improve safety/perf for low-risk Q&A workflows.
   - Scope: chat-only mode with restricted tool execution policy.
   - Estimate: 1-2 days.

### P1 (workflow parity)

3. Session branching tree UI
   - Why: improve history navigation and what-if workflows.
   - Scope: visualize forks and allow branch switch/merge-like resume.
   - Estimate: 4-6 days.

4. Runtime skill creation UX
   - Why: parity on interactive skill authoring and rapid workflow codification.
   - Scope: create/edit/activate skills from app runtime with validation.
   - Estimate: 3-5 days.

## Proposed Milestones

| Milestone | Scope | Target Sprint |
|---|---|---|
| M1 | Provider switching + minimal tool mode | Sprint 2.3 or 2.4 |
| M2 | Session branching tree | Sprint 2.4 |
| M3 | Runtime skill creation UX | Sprint 2.5 |

## Ownership Proposal

- Core/runtime changes: `packages/core` maintainers
- Frontend UX: `src/components` owners
- Validation/testing: Sprint 1.6 hardening owner

## Acceptance Criteria

1. Parity checklist is prioritized and estimated.
2. Milestones are mapped to future sprints.
3. Each item has clear scope boundary (in/out).

## Out of Scope

- Full PI feature parity in this sprint.
- Large new protocol surfaces (Phase 4 work).
