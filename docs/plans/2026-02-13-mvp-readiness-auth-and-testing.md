# MVP Readiness: Auth + Autonomous Testing Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make MVP confidence high by verifying auth end-to-end and adding automated tests so core flows can be validated without manual help.

**Architecture:** Use a testing-first hardening pass. Lock a small set of MVP-critical flows (auth, chat, session continuity, plugin basics), then implement unit/integration tests around existing services and stores before any UI feature expansion. Keep manual Tauri checks as a final gate only.

**Tech Stack:** Vitest, TypeScript, SolidJS testing setup, existing Tauri frontend services/stores, core package tests.

---

### Task 1: Define MVP quality gates and test matrix

**Files:**
- Create: `docs/development/mvp-test-matrix.md`
- Modify: `.opencode/todo.md`

**Step 1: Write the MVP gate checklist doc**
- Define pass/fail gates for: auth login/reconnect, send message, tool approval, session restore, settings persistence.

**Step 2: Add required CI/local commands to matrix**
- Include: `npm run lint`, `npx tsc --noEmit`, `npm run test:run`.

**Step 3: Update mission TODO to track each gate**
- Add leaf items for each gate with evidence links.

### Task 2: Harden OAuth/Auth automated coverage

**Files:**
- Modify: `packages/core/src/llm/client.test.ts`
- Create: `src/services/auth/oauth-flow.test.ts`
- Modify: `docs/development/sprint-1.6-testing.md`

**Step 1: Add failing auth edge-case tests in core**
- Cases: expired token fallback, missing account id, provider mismatch, clear-and-reconnect flow.

**Step 2: Run focused core auth tests**
- Run: `npx vitest run packages/core/src/llm/client.test.ts`

**Step 3: Implement/adjust minimal auth behavior only if failures reveal gaps**
- Keep changes localized to current auth routing/storage paths.

**Step 4: Add frontend auth flow test stubs and critical path assertions**
- Validate connect/disconnect state transitions and safe error surfacing.

**Step 5: Re-run auth-focused tests and mark ticket evidence**
- Run: `npx vitest run packages/core/src/llm/client.test.ts src/services/auth/oauth-flow.test.ts`

### Task 3: Add chat reliability integration tests (no user intervention)

**Files:**
- Create: `src/hooks/useChat.integration.test.ts`
- Create: `src/components/chat/ChatView.integration.test.tsx`
- Modify: `docs/development/sprint-1.6-testing.md`

**Step 1: Add failing integration scenarios**
- Cases: send while streaming, steering/cancel behavior, queued follow-up, watcher-originated message metadata.

**Step 2: Run focused chat integration tests**
- Run: `npx vitest run src/hooks/useChat.integration.test.ts src/components/chat/ChatView.integration.test.tsx`

**Step 3: Fix minimal logic where needed**
- Prioritize deterministic queue/cancel state and UI state transitions.

**Step 4: Re-run tests until green**
- Same command as Step 2.

### Task 4: Add plugin baseline smoke tests for MVP safety

**Files:**
- Modify: `packages/core/src/extensions/manager.test.ts`
- Create: `src/components/settings/tabs/PluginsTab.smoke.test.tsx`
- Modify: `docs/frontend/backlog.md`

**Step 1: Add core extension lifecycle regression tests**
- Validate install -> enable -> disable -> uninstall path with persisted enablement state.

**Step 2: Add frontend plugin tab smoke test**
- Validate placeholder/actions don’t crash and show deterministic state.

**Step 3: Run plugin-focused suite**
- Run: `npx vitest run packages/core/src/extensions/manager.test.ts src/components/settings/tabs/PluginsTab.smoke.test.tsx`

### Task 5: Establish autonomous validation workflow

**Files:**
- Create: `scripts/verify-mvp.sh`
- Modify: `package.json`
- Modify: `docs/development/mvp-test-matrix.md`

**Step 1: Add non-interactive verification script**
- Script runs lint + typecheck + test run in sequence with clear failure exits.

**Step 2: Add package script alias**
- Add `"verify:mvp": "bash scripts/verify-mvp.sh"`.

**Step 3: Run and capture baseline output**
- Run: `npm run verify:mvp`

**Step 4: Document expected outputs and failure triage**
- Add quick troubleshooting section in matrix doc.

### Task 6: Final MVP verification and release-readiness report

**Files:**
- Create: `docs/development/mvp-readiness-report-2026-02-13.md`
- Modify: `docs/ROADMAP.md`
- Modify: `.opencode/work-log.md`

**Step 1: Run full verification gates**
- `npm run verify:mvp`
- `npm run test:run`

**Step 2: Perform short manual Tauri auth sanity pass**
- Run: `npm run tauri dev` and validate provider connect/disconnect once.

**Step 3: Write readiness report with evidence links**
- Include pass/fail per gate and known limitations.

**Step 4: Update roadmap sprint note**
- Reflect hardening progress and remaining blockers.
