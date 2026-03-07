# Frontend Store/Hook Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Split 6 oversized frontend files (stores + hooks) to comply with the 300-line limit, fix the circular import between settings store and component tabs, and reduce inter-store coupling.

**Architecture:** Extract shared types/defaults to `src/config/defaults/`, split stores into state + operations files, split hooks into core logic + helpers. Module-level signals MUST remain in a single file per store to preserve SolidJS reactivity.

**Tech Stack:** SolidJS (createSignal), TypeScript strict mode, Biome/Oxlint linting, Vitest tests

---

## Verification Checklist (Run After Every Phase)

```bash
npx tsc --noEmit                  # Type check
npm run lint                      # Oxlint + ESLint
npm run format:check              # Biome format
npm run test                      # Vitest tests
```

If any check fails, fix before proceeding to next phase.

---

## Phase 1: Break Circular Import — Extract Types & Defaults

**Risk addressed:** RISK-1 (CRITICAL) — `stores/settings.ts` imports from component tabs

**Why first:** This circular dependency blocks clean file splitting. Every other phase depends on clean imports.

### Task 1.1: Create `src/config/defaults/provider-defaults.ts`

**Files:**
- Create: `src/config/defaults/provider-defaults.ts`

**Step 1: Create the file with types + defaults extracted from ProvidersTab.tsx**

Move `LLMProviderConfig` interface (currently at ProvidersTab.tsx L50-68) and `ProviderModel` type and `defaultProviders` array (ProvidersTab.tsx L589+) into this new file. The icon references use lucide-solid — these MUST come along.

```typescript
/**
 * Provider type definitions and default configurations.
 * Extracted from ProvidersTab to break circular import with settings store.
 */
import type { Component } from 'solid-js'
import {
  Bot, Braces, Cloud, Cpu, Flame, Globe, Monitor, Sparkles, Zap,
} from 'lucide-solid'

type IconComponent = Component<{ class?: string }>

export interface ProviderModel {
  id: string
  name: string
  contextWindow: number
  isDefault?: boolean
}

export interface LLMProviderConfig {
  id: string
  name: string
  icon: IconComponent
  description: string
  enabled: boolean
  status: 'connected' | 'disconnected' | 'error'
  apiKey?: string
  baseUrl?: string
  models: ProviderModel[]
  defaultModel?: string
  error?: string
}

export const defaultProviders: LLMProviderConfig[] = [
  // ... copy the full array from ProvidersTab.tsx L589-780
]
```

**Step 2: Run `npx tsc --noEmit` to verify the new file compiles**

Expected: PASS (no consumers yet)

### Task 1.2: Create `src/config/defaults/agent-defaults.ts`

**Files:**
- Create: `src/config/defaults/agent-defaults.ts`

**Step 1: Create the file with types + defaults extracted from AgentsTab.tsx**

Move `AgentPreset` interface (AgentsTab.tsx L16-27) and `defaultAgentPresets` array (AgentsTab.tsx L214+) into this new file.

```typescript
/**
 * Agent preset type definitions and defaults.
 * Extracted from AgentsTab to break circular import with settings store.
 */
import type { Component } from 'solid-js'
import { Code, FileText, GitBranch, Terminal, Zap } from 'lucide-solid'

type IconComponent = Component<{ class?: string }>

export interface AgentPreset {
  id: string
  name: string
  description: string
  icon: IconComponent
  enabled: boolean
  systemPrompt?: string
  capabilities: string[]
  model?: string
  isCustom?: boolean
  type?: 'coding' | 'git' | 'terminal' | 'docs' | 'fast' | 'custom'
}

export const defaultAgentPresets: AgentPreset[] = [
  // ... copy the full array from AgentsTab.tsx L214-264
]
```

**Step 2: Run `npx tsc --noEmit`**

Expected: PASS

### Task 1.3: Create barrel export `src/config/defaults/index.ts`

**Files:**
- Create: `src/config/defaults/index.ts`

```typescript
export type { AgentPreset } from './agent-defaults'
export { defaultAgentPresets } from './agent-defaults'
export type { LLMProviderConfig, ProviderModel } from './provider-defaults'
export { defaultProviders } from './provider-defaults'
```

### Task 1.4: Rewire `src/stores/settings.ts` imports

**Files:**
- Modify: `src/stores/settings.ts` lines 10-13

**Step 1: Replace the 4 circular imports**

Change:
```typescript
import type { AgentPreset } from '../components/settings/tabs/AgentsTab'
import { defaultAgentPresets } from '../components/settings/tabs/AgentsTab'
import type { LLMProviderConfig } from '../components/settings/tabs/ProvidersTab'
import { defaultProviders } from '../components/settings/tabs/ProvidersTab'
```

To:
```typescript
import type { AgentPreset, LLMProviderConfig } from '../config/defaults'
import { defaultAgentPresets, defaultProviders } from '../config/defaults'
```

**Step 2: Run `npx tsc --noEmit`**

Expected: PASS

### Task 1.5: Rewire `ProvidersTab.tsx` to import from defaults

**Files:**
- Modify: `src/components/settings/tabs/ProvidersTab.tsx`

**Step 1: Remove the `LLMProviderConfig` interface and `defaultProviders` array from ProvidersTab.tsx. Add import from defaults.**

At the top, add:
```typescript
import type { LLMProviderConfig, ProviderModel } from '../../../config/defaults'
```

Remove:
- The `LLMProviderConfig` interface (L50-68)
- The `ProviderModel` interface (if separate)
- The `defaultProviders` array (L589-780ish)
- The `IconComponent` type (L48) — if not needed locally

Keep: The `ProvidersTabProps` interface and component.

**Step 2: Run `npx tsc --noEmit`**

### Task 1.6: Rewire `AgentsTab.tsx` to import from defaults

**Files:**
- Modify: `src/components/settings/tabs/AgentsTab.tsx`

**Step 1: Remove `AgentPreset` interface and `defaultAgentPresets` from AgentsTab.tsx. Import from defaults.**

Add:
```typescript
import type { AgentPreset } from '../../../config/defaults'
```

Remove:
- The `AgentPreset` interface (L16-27)
- The `defaultAgentPresets` array (L214-264)
- The `IconComponent` type if no longer needed
- The lucide-solid imports for defaults (Code, FileText, GitBranch, Terminal, Zap) — only if not used by the component itself

**Step 2: Run `npx tsc --noEmit`**

### Task 1.7: Update barrel export `src/components/settings/tabs/index.ts`

**Files:**
- Modify: `src/components/settings/tabs/index.ts`

The barrel currently re-exports types from ProvidersTab and AgentsTab. Update to re-export from the new defaults location:

```typescript
export type { AgentPreset, AgentsTabProps } from './AgentsTab'
export { AgentsTab } from './AgentsTab'
// defaultAgentPresets now comes from config/defaults
export { defaultAgentPresets } from '../../../config/defaults'
export type { LLMProviderConfig, ProviderModel, ProvidersTabProps } from './ProvidersTab'
// But LLMProviderConfig type should come from config/defaults
```

Actually — check consumers. `SettingsModal.tsx` imports `AgentPreset` from `./tabs/AgentsTab`. Verify all consumers and update as needed.

### Task 1.8: Update `SettingsModal.tsx` imports

**Files:**
- Modify: `src/components/settings/SettingsModal.tsx` (L37)

Change:
```typescript
import { type AgentPreset, AgentsTab } from './tabs/AgentsTab'
```
To:
```typescript
import type { AgentPreset } from '../../config/defaults'
import { AgentsTab } from './tabs/AgentsTab'
```

### Task 1.9: Run full verification checklist

```bash
npx tsc --noEmit && npm run lint && npm run format:check && npm run test
```

### Task 1.10: Commit

```bash
git add src/config/defaults/ src/stores/settings.ts src/components/settings/tabs/ProvidersTab.tsx src/components/settings/tabs/AgentsTab.tsx src/components/settings/tabs/index.ts src/components/settings/SettingsModal.tsx
git commit -m "refactor: extract provider/agent defaults to break circular import

Move LLMProviderConfig, AgentPreset types and defaultProviders,
defaultAgentPresets arrays from component tabs into src/config/defaults/
to eliminate circular dependency between settings store and UI components."
```

---

## Phase 2: Split `settings.ts` (1010 → 4 files, each <300 lines)

**Risk addressed:** RISK-8 (appearance DOM logic in store), 300-line limit

### Task 2.1: Extract `src/stores/settings/appearance.ts` (~230 lines)

**Files:**
- Create: `src/stores/settings/appearance.ts`

Move from current `settings.ts`:
- Font maps: MONO_FONTS, SANS_FONTS (L661-730)
- Radius/density scales: RADIUS_SCALES, DENSITY_SCALES (L667-723)
- Color helpers: hexToRgb, adjustBrightness, hexToAccentVars, ACCENT_VAR_NAMES (L732-777)
- resolveMode() (L779-789)
- applyAppearance() (L791-893)
- setupSystemThemeListener() (L895-905)

This file needs access to `settings()` signal — pass as parameter or import from the state file.

```typescript
/**
 * Appearance DOM application logic.
 * Reads settings signal and applies CSS custom properties to document.
 */
import type { AppSettings, AppearanceSettings, ... } from './types'
// ... all the appearance logic
export function applyAppearance(getSettings: () => AppSettings): void { ... }
export function setupSystemThemeListener(getSettings: () => AppSettings): () => void { ... }
export function resolveMode(s: AppSettings): string { ... }
export function isDarkMode(getSettings: () => AppSettings): boolean { ... }
```

### Task 2.2: Extract `src/stores/settings/types.ts` (~100 lines)

**Files:**
- Create: `src/stores/settings/types.ts`

Move all type definitions from `settings.ts` L163-263:
- PermissionMode, UISettings, AppearanceSettings, GenerationSettings, AgentLimitSettings, BehaviorSettings, NotificationSettings, GitSettings, AppSettings, AccentColor, MonoFont, SansFont, BorderRadius, UIDensity, CodeTheme, DarkStyle, SendKey

### Task 2.3: Extract `src/stores/settings/defaults.ts` (~80 lines)

**Files:**
- Create: `src/stores/settings/defaults.ts`

Move all DEFAULT_* constants (L265-343):
- DEFAULT_UI, DEFAULT_APPEARANCE, DEFAULT_GENERATION, DEFAULT_AGENT_LIMITS, DEFAULT_BEHAVIOR, DEFAULT_NOTIFICATIONS, DEFAULT_GIT, DEFAULT_SETTINGS

### Task 2.4: Extract `src/stores/settings/credentials.ts` (~80 lines)

**Files:**
- Create: `src/stores/settings/credentials.ts`

Move L23-114:
- CREDENTIAL_PREFIX, PROVIDER_KEY_MAP, syncProviderCredentials(), syncAllApiKeys()
- ENV_VAR_MAP, detectEnvApiKeys()

### Task 2.5: Extract `src/stores/settings/persistence.ts` (~100 lines)

**Files:**
- Create: `src/stores/settings/persistence.ts`

Move L349-436 + L907-948:
- loadSettings(), serializeSettings(), saveSettings()
- hydrateProviders(), hydrateAgents()
- hydrateSettingsFromFS()

### Task 2.6: Extract `src/stores/settings/core-sync.ts` (~50 lines)

**Files:**
- Create: `src/stores/settings/core-sync.ts`

Move L116-161:
- pushSettingsToCore()

### Task 2.7: Slim down `src/stores/settings/index.ts` to <300 lines

**Files:**
- Modify: rename `src/stores/settings.ts` → `src/stores/settings/index.ts`

This file keeps:
- The module-level signal: `const [settings, setSettingsRaw] = createSignal<AppSettings>(initial)`
- All mutator functions (updateSettings, updateProvider, etc.) — these are ~200 lines
- The `useSettings()` export hook
- MCP CRUD functions (~20 lines)
- Import/export helpers (exportSettings, importSettings, resetSettings)

Re-export types and key functions from sub-modules.

### Task 2.8: Update all 21 import sites

**Files to update** (these import from `../stores/settings` or `../../stores/settings`):

Since we're using `index.ts` in a directory, import paths stay the same (`../stores/settings` resolves to `../stores/settings/index.ts`). No consumer changes needed IF we re-export everything from the barrel.

Verify: `npx tsc --noEmit`

**BUT** — some files import specific named exports like `isDarkMode`, `syncProviderCredentials`, `resolveMode`. These MUST be re-exported from the barrel `index.ts`.

### Task 2.9: Run full verification + commit

```bash
npx tsc --noEmit && npm run lint && npm run format:check && npm run test
git add src/stores/settings/ src/stores/settings.ts
git commit -m "refactor: split settings store into 6 focused modules

Extract appearance, types, defaults, credentials, persistence, and
core-sync into separate files under src/stores/settings/. Main index.ts
keeps the signal + mutators + hook export. All files under 300 lines."
```

---

## Phase 3: Split `session.ts` (874 → 3 files, each <300 lines)

**Risk addressed:** RISK-2 (project coupling), RISK-3 (module-level signals), 300-line limit

### Task 3.1: Extract `src/stores/session/types.ts`

Types are already in `src/types/index.ts` — verify no session-specific types need extracting.

### Task 3.2: Extract `src/stores/session/operations.ts` (~400 lines)

**Files:**
- Create: `src/stores/session/operations.ts`

Move all the method bodies from the `useSession()` return object that are longer than 10 lines. These are currently defined as inline arrow functions. Extract them as named functions that accept the signal setters as parameters:

```typescript
export async function loadAllSessions(
  projectId: string | undefined,
  setSessions: ...,
  setIsLoadingSessions: ...,
): Promise<void> { ... }
```

**Critical:** The `useProject()` coupling (RISK-2) gets fixed here — instead of calling `useProject()` inside the function body, the extracted function accepts `projectId` as a parameter. The caller (in `index.ts`) reads it from `useProject()`.

### Task 3.3: Slim `src/stores/session/index.ts` to <300 lines

**Files:**
- Rename: `src/stores/session.ts` → `src/stores/session/index.ts`

This file keeps:
- All 13 module-level signals (MUST stay here — RISK-3)
- The 3 computed memos (sessionTokenStats, contextUsage, agentStats)
- The `useSession()` hook that returns signals + delegates to operations

### Task 3.4: Fix project coupling (RISK-2)

In the new `index.ts`, change the 4 call sites from:
```typescript
const { currentProject } = useProject()
const projectId = currentProject()?.id
```
To: call `useProject()` once at the hook level and pass `projectId` down.

### Task 3.5: Run verification + commit

---

## Phase 4: Split `useChat.ts` (925 → 3 files, each <300 lines)

**Risk addressed:** RISK-4, 300-line limit

### Task 4.1: Extract `src/hooks/chat/stream.ts` (~250 lines)

**Files:**
- Create: `src/hooks/chat/stream.ts`

Move the `streamResponse()` function (L191-423) plus its helpers:
- getModifiedFilePath() (L159-165)
- checkLintErrors() (L168-185)

The function needs access to: `currentProject`, `settings`, `setCurrentProvider`, `setActiveToolCalls`, logging. Pass these as a context parameter object.

**CRITICAL:** The `streamResponse` function (232 lines) MUST NOT be split further — it's a stateful async loop with tool execution that needs to remain cohesive.

### Task 4.2: Extract `src/hooks/chat/messages.ts` (~150 lines)

**Files:**
- Create: `src/hooks/chat/messages.ts`

Move:
- createAssistantMessage() (L429-437)
- recallMemoryContext() (L443-486)
- buildApiMessages() (L488-531)
- syncTrackerStats() (L90-100)
- maybeCompact() (L106-152)

### Task 4.3: Slim `src/hooks/chat/index.ts` to <300 lines

**Files:**
- Rename: `src/hooks/useChat.ts` → `src/hooks/chat/index.ts`

Keeps:
- Singleton pattern
- Signal declarations (8 signals)
- Store access (session, project, settings, approval)
- Public API functions: sendMessage, regenerate, cancel, processQueue, steer, clearQueue, clearError, retryMessage, editAndResend, undoLastEdit, regenerateResponse
- The return object

### Task 4.4: Update `src/hooks/index.ts` barrel

If it currently exports `useChat` from `./useChat`, update to `./chat`.

### Task 4.5: Run verification + commit

---

## Phase 5: Split `useAgent.ts` (598 → 2 files, each <300 lines)

### Task 5.1: Extract `src/hooks/agent/types.ts` (~50 lines)

Move ToolActivity, AgentState interfaces.

### Task 5.2: Extract `src/hooks/agent/execution.ts` (~250 lines)

Move the core agent execution logic (runAgentLoop or similar main function).

### Task 5.3: Slim `src/hooks/agent/index.ts` to <300 lines

Keeps signals, store access, hook return.

### Task 5.4: Update barrel + verification + commit

---

## Phase 6: Fix Service Layer Violation

**Risk addressed:** RISK-6 — `services/notifications.ts` imports store

### Task 6.1: Refactor `src/services/notifications.ts`

Currently (L9, L55):
```typescript
import { useSettings } from '../stores/settings'
const { settings } = useSettings()
```

Change to accept settings as a parameter:
```typescript
export function notifyCompletion(title: string, body: string, settings: { notifyOnCompletion: boolean; soundOnCompletion: boolean; soundVolume: number }): void
```

### Task 6.2: Update all callers of `notifyCompletion`

Found in: `useChat.ts`, `useAgent.ts` — pass `settings().notifications` as parameter.

### Task 6.3: Verification + commit

---

## Phase 7: Split `team.ts` and `project.ts` (Both ~330-345 lines)

These are only slightly over the 300-line limit.

### Task 7.1: Extract types or constants from `team.ts` if needed

May only need to move ~50 lines of types to get under 300.

### Task 7.2: Extract types or constants from `project.ts` if needed

Same approach.

### Task 7.3: Verification + commit

---

## Risk Summary & Phase Dependencies

```
Phase 1 (circular import) ──→ Phase 2 (settings split) ──→ Phase 6 (notifications)
                            └─→ Phase 3 (session split)
                            └─→ Phase 4 (useChat split)  ──→ Phase 6 (notifications)
                            └─→ Phase 5 (useAgent split) ──→ Phase 6 (notifications)
                            └─→ Phase 7 (team/project trim)
```

Phase 1 MUST be done first. Phases 2-5 can be done in any order after Phase 1. Phase 6 should be done after Phases 4+5. Phase 7 is independent after Phase 1.

---

## File Count Summary

| Before | After |
|--------|-------|
| `stores/settings.ts` (1010) | `stores/settings/{index,types,defaults,appearance,credentials,persistence,core-sync}.ts` (7 files, all <300) |
| `hooks/useChat.ts` (925) | `hooks/chat/{index,stream,messages}.ts` (3 files, all <300) |
| `stores/session.ts` (874) | `stores/session/{index,operations}.ts` (2 files, all <300) |
| `hooks/useAgent.ts` (598) | `hooks/agent/{index,types,execution}.ts` (3 files, all <300) |
| `stores/team.ts` (345) | `stores/team.ts` or `stores/team/{index,types}.ts` (<300) |
| `stores/project.ts` (330) | `stores/project.ts` or `stores/project/{index,types}.ts` (<300) |
| **6 files, 4080 lines** | **~18-20 files, same total lines, all <300** |
| + `config/defaults/` (3 new files) | |
