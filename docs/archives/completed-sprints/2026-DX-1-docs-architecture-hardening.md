# Sprint DX-1 Docs Architecture Hardening

## Goal

Finalize two-lane docs adoption and establish durable freshness checks across roadmap, backlogs, and sprint execution docs.

## Ticket board

- `DX-1-01` Adopt two-lane structure in all active docs - in_progress
  - Owner backlog: `docs/ROADMAP.md`
  - Owner checklist:
    - [ ] Ensure roadmap links to current-focus, S1.6, S2.3, and DX-1 execution docs.
    - [ ] Ensure docs index links to execution-lane docs.
    - [ ] Ensure frontend/backend backlogs include ownership rules and integration linkage.
- `DX-1-02` Add advisory drift check script - done
  - Owner backlog: `docs/backend/backlog.md`
  - Owner checklist:
    - [x] Implement warning-only drift script.
    - [x] Add `docs:drift` command in `package.json`.
    - [x] Include canonical and execution docs in drift targets.
- `DX-1-03` Add CI wiring for `docs:drift` in workflow - todo
  - Owner backlog: `docs/backend/backlog.md`
  - Owner checklist:
    - [ ] Add `npm run docs:drift` to CI workflow as advisory step.
    - [ ] Document warning-only behavior in workflow comments.
- `DX-1-04` Add PR docs freshness checklist - done
  - Owner backlog: `docs/ROADMAP.md`
  - Owner checklist:
    - [x] Add docs freshness checkbox to PR template.
- `DX-1-05` Validate source-of-truth ownership rules - in_progress
  - Owner backlog: `docs/frontend/backlog.md` and `docs/backend/backlog.md`
  - Owner checklist:
    - [ ] Confirm cross-cutting tickets point to `docs/development/backlogs/integration-backlog.md`.
    - [ ] Confirm `INT-001`/`INT-002`/`INT-003` references are aligned in backlog docs.

## Dependencies

- Active sprint references present in roadmap and docs index.
- `docs:drift` script exists in `package.json`.

## Evidence

- `npm run docs:drift`
- `docs/ROADMAP.md`
- `docs/README.md`
- `docs/development/backlogs/integration-backlog.md`

## Exit criteria

- No contradictions for active tracked items in docs drift report.
- Roadmap, backlog, and sprint docs link consistently.
- CI includes advisory docs drift command.

## Close checklist

- [ ] CI command wired and documented
- [ ] Ownership rules verified in frontend/backend backlogs
- [ ] Current focus pulse updated after closeout
