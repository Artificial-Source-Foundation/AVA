# Two-Lane Docs Reorganization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Normalize docs status, add execution-lane docs, and keep roadmap/backlog/sprint states aligned.

**Architecture:** Canonical docs are concise source-of-truth; active sprint/backlog docs hold execution detail and evidence links.

**Tech Stack:** Markdown, git, ripgrep, Node/npm scripts

---

### Task 1: Normalize roadmap status drift

**Files**
- Modify: `docs/ROADMAP.md`
- Reference: `docs/development/epics/sprint-1.6-testing-hardening.md`
- Reference: `docs/development/epics/plugin-ecosystem-ux-integration.md`

**Steps**
1. Confirm drift exists:
   - `rg -n "stream(ing)? (micro-)?jitter|streaming polish" docs/ROADMAP.md docs/frontend/backlog.md docs/development/epics/*.md`
2. Update roadmap statuses:
   - streaming jitter => done
   - manual OAuth matrix => in progress
   - plugin lifecycle wiring => in progress
3. Add links from roadmap to execution docs.
4. Re-run the grep and confirm no jitter contradictions.

**Commit**
```bash
git add docs/ROADMAP.md
git commit -m "docs: align roadmap status with sprint reality"
```

### Task 2: Normalize frontend and backend backlogs

**Files**
- Modify: `docs/frontend/backlog.md`
- Modify: `docs/backend/backlog.md`

**Steps**
1. Add `Ownership Rules` to both files.
2. Ensure cross-cutting lifecycle items point to `docs/development/backlogs/integration-backlog.md`.
3. Ensure lifecycle wiring references include `INT-001`/`INT-002`/`INT-003` where relevant.
4. Verify:
   - `rg -n "Ownership Rules|integration-backlog|INT-001|INT-002|INT-003" docs/frontend/backlog.md docs/backend/backlog.md`

**Commit**
```bash
git add docs/frontend/backlog.md docs/backend/backlog.md
git commit -m "docs: define backlog ownership and integration handoff"
```

### Task 3: Create integration backlog

**Files**
- Create: `docs/development/backlogs/integration-backlog.md`

**Steps**
1. Create sections: `Active`, `Blockers`, `Done`.
2. Add active tickets `INT-001`, `INT-002`, `INT-003` with owners and exit evidence.
3. Verify:
   - `rg -n "INT-001|INT-002|INT-003|## Active|## Blockers|## Done" docs/development/backlogs/integration-backlog.md`

**Commit**
```bash
git add docs/development/backlogs/integration-backlog.md
git commit -m "docs: add integration backlog for frontend-backend wiring"
```

### Task 4: Add current focus pulse doc

**Files**
- Create: `docs/development/status/current-focus.md`

**Steps**
1. Add sections:
   - `Last Updated`
   - `Active Sprint`
   - `Top Priorities`
   - `Blockers`
   - `Evidence Refresh Needed`
2. Link active sprint execution docs from this file.
3. Verify:
   - `rg -n "Last Updated|Active Sprint|Top Priorities|Blockers|Evidence Refresh Needed|development/sprints" docs/development/status/current-focus.md`

**Commit**
```bash
git add docs/development/status/current-focus.md
git commit -m "docs: add weekly current focus status pulse"
```

### Task 5: Add sprint execution docs and template

**Files**
- Create: `docs/development/sprints/README.md`
- Create: `docs/development/sprints/2026-S1.6-testing-hardening-closeout.md`
- Create: `docs/development/sprints/2026-S2.3-plugin-ux-wiring.md`
- Create: `docs/development/sprints/2026-DX-1-docs-architecture-hardening.md`

**Steps**
1. Define required sprint sections in `README.md`.
2. Add active sprint index in `README.md`.
3. Add ticket boards, dependencies, evidence, exit criteria, and close checklist in each sprint doc.
4. Verify:
   - `rg -n "Goal|Ticket board|Dependencies|Evidence|Exit criteria|Close checklist" docs/development/sprints/*.md`

**Commit**
```bash
git add docs/development/sprints
git commit -m "docs: add sprint execution docs and templates"
```

### Task 6: Add advisory drift guard and PR checklist

**Files**
- Create: `scripts/docs/check-doc-drift.mjs`
- Modify: `package.json`
- Create/Modify: `.github/pull_request_template.md`

**Steps**
1. Implement warning-only drift checker (exit code 0).
2. Check targets include canonical + execution docs.
3. Add `docs:drift` script to `package.json`.
4. Add PR checkbox for docs update or N/A rationale.
5. Verify:
   - `npm run docs:drift`

**Commit**
```bash
git add scripts/docs/check-doc-drift.mjs package.json .github/pull_request_template.md
git commit -m "chore(docs): add advisory drift check and PR checklist"
```

### Task 7: Final docs index wiring and verification

**Files**
- Modify: `docs/README.md`

**Steps**
1. Link `Current Focus`, `Integration Backlog`, and `Sprint Execution Docs` from docs index.
2. Verify discoverability:
   - `rg -n "integration-backlog|current-focus|development/sprints" docs/README.md docs/ROADMAP.md docs/frontend/backlog.md docs/backend/backlog.md`
3. Verify contradictions:
   - `npm run docs:drift`

**Commit**
```bash
git add docs/README.md
git commit -m "docs: link new execution lane and finalize sync"
```
