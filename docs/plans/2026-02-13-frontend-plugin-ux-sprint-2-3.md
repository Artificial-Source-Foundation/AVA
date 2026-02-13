# Frontend Plugin UX (Sprint 2.3) Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Ship the first usable frontend plugin experience: browse installed/available plugins, install/uninstall, enable/disable, and per-plugin settings entry points.

**Architecture:** Keep backend extension lifecycle as source of truth (`packages/core/src/extensions/*`) and add a thin frontend orchestration layer via existing bridge/services. Start with settings-surface MVP (since sidebar placeholder was removed), then optionally promote to dedicated sidebar page after UX validation.

**Tech Stack:** SolidJS + TypeScript + Tauri bridge + existing settings/store patterns.

---

### Task 1: Define frontend plugin data contract

**Files:**
- Create: `src/types/plugin.ts`
- Modify: `src/services/core-bridge.ts`
- Test: `src/services/core-bridge.test.ts`

**Step 1: Write failing test**
- Add test asserting plugin list/install/enable/disable bridge calls return typed responses.

**Step 2: Run test to verify fail**
- Run: `npx vitest run src/services/core-bridge.test.ts`

**Step 3: Implement minimal types + bridge methods**
- Add `PluginSummary`, `PluginDetails`, `PluginActionResult`.
- Add bridge methods: `listPlugins`, `installPlugin`, `uninstallPlugin`, `enablePlugin`, `disablePlugin`, `reloadPlugin`.

**Step 4: Run test to verify pass**
- Run: `npx vitest run src/services/core-bridge.test.ts`

**Step 5: Commit**
- `git add src/types/plugin.ts src/services/core-bridge.ts src/services/core-bridge.test.ts && git commit -m "feat(frontend): add typed plugin bridge contract"`

### Task 2: Add plugin store and state machine

**Files:**
- Create: `src/stores/plugins.ts`
- Modify: `src/stores/index.ts`
- Test: `src/stores/plugins.test.ts`

**Step 1: Write failing store tests**
- Cover load, install, uninstall, enable/disable toggles, and error state.

**Step 2: Run tests and confirm fail**
- Run: `npx vitest run src/stores/plugins.test.ts`

**Step 3: Implement minimal store**
- Create signal-backed store with `plugins`, `loading`, `error`, and action methods.

**Step 4: Run tests and confirm pass**
- Run: `npx vitest run src/stores/plugins.test.ts`

**Step 5: Commit**
- `git add src/stores/plugins.ts src/stores/index.ts src/stores/plugins.test.ts && git commit -m "feat(frontend): add plugin state store"`

### Task 3: Build plugin settings MVP UI

**Files:**
- Create: `src/components/settings/tabs/PluginsTab.tsx`
- Modify: `src/components/settings/SettingsModal.tsx`
- Test: `src/components/settings/tabs/PluginsTab.test.tsx`

**Step 1: Write failing component tests**
- Validate list rendering, install/uninstall buttons, enable toggle, and loading/error states.

**Step 2: Run tests to verify fail**
- Run: `npx vitest run src/components/settings/tabs/PluginsTab.test.tsx`

**Step 3: Implement minimal UI**
- Render plugin cards, status badges, action buttons, and refresh control.

**Step 4: Run tests to verify pass**
- Run: `npx vitest run src/components/settings/tabs/PluginsTab.test.tsx`

**Step 5: Commit**
- `git add src/components/settings/tabs/PluginsTab.tsx src/components/settings/SettingsModal.tsx src/components/settings/tabs/PluginsTab.test.tsx && git commit -m "feat(frontend): add plugin settings tab MVP"`

### Task 4: Add plugin detail/settings entry point

**Files:**
- Create: `src/components/plugins/PluginDetailPanel.tsx`
- Modify: `src/components/settings/tabs/PluginsTab.tsx`
- Test: `src/components/plugins/PluginDetailPanel.test.tsx`

**Step 1: Write failing detail tests**
- Cover metadata rendering, context files display, and settings/open action callback.

**Step 2: Run tests to verify fail**
- Run: `npx vitest run src/components/plugins/PluginDetailPanel.test.tsx`

**Step 3: Implement detail panel**
- Show name/version/description/state and action surface for plugin-level settings.

**Step 4: Run tests to verify pass**
- Run: `npx vitest run src/components/plugins/PluginDetailPanel.test.tsx`

**Step 5: Commit**
- `git add src/components/plugins/PluginDetailPanel.tsx src/components/settings/tabs/PluginsTab.tsx src/components/plugins/PluginDetailPanel.test.tsx && git commit -m "feat(frontend): add plugin detail panel"`

### Task 5: Validate integration and docs

**Files:**
- Modify: `docs/frontend/backlog.md`
- Modify: `docs/ROADMAP.md`
- Modify: `docs/frontend/changelog.md`

**Step 1: Run focused tests**
- `npx vitest run src/services/core-bridge.test.ts src/stores/plugins.test.ts src/components/settings/tabs/PluginsTab.test.tsx src/components/plugins/PluginDetailPanel.test.tsx`

**Step 2: Run quality gates**
- `npm run lint`
- `npx tsc --noEmit`

**Step 3: Update docs**
- Mark sprint 2.3 as in progress with delivered MVP scope.

**Step 4: Run final verification**
- `npm run test:run`

**Step 5: Commit**
- `git add docs/frontend/backlog.md docs/ROADMAP.md docs/frontend/changelog.md && git commit -m "docs: record sprint 2.3 plugin UX MVP plan and status"`
