# Sprint 16 Interactive TUI Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build and ship Sprint 16 Interactive TUI for AVA using React + Ink with streaming chat, approvals, keybindings, sessions, sidebar, and agent bridge.

**Architecture:** Create a new `cli/src/tui` React+Ink application composed by provider contexts (theme/session/streaming/keybind/permission), feature-scoped components, and hooks. Reuse AVA CLI runtime integration patterns from `cli/src/commands/run.ts` for platform, extensions, executor lifecycle, and event flow.

**Tech Stack:** TypeScript, React 19, Ink 5, @inkjs/ui, marked, marked-terminal, cli-highlight, fuzzysort, diff, chalk, vitest, ink-testing-library.

---

### Task 0: Dependency and scaffolding baseline

**Files:**
- Modify: `cli/package.json`
- Create: `cli/src/tui/index.tsx`
- Create: `cli/src/tui/app.tsx`
- Modify: `cli/src/index.ts`

**Step 1: Add required dependencies/devDependencies in CLI package**

Add exact packages from sprint prompt unless workspace already provides same version.

**Step 2: Write failing smoke test for TUI entry render**

Create `cli/src/tui/app.test.tsx` with an initial render assertion.

```tsx
import { render } from 'ink-testing-library'
import React from 'react'
import { App } from './app.js'

it('renders app shell', () => {
  const { lastFrame } = render(<App />)
  expect(lastFrame()).toContain('AVA')
})
```

**Step 3: Implement minimal `index.tsx` and `app.tsx` to satisfy smoke test**

`index.tsx` exports `startTUI(options)` using Ink `render`. `app.tsx` returns minimal placeholder shell.

**Step 4: Wire `cli/src/index.ts` default no-arg behavior to launch TUI**

Add explicit `tui` command path and make no-command default launch `startTUI(...)`.

**Step 5: Verify baseline**

Run: `npm run test:run && npx tsc --noEmit`
Expected: tests and typecheck pass with new dependencies and entry wiring.

**Step 6: Commit**

```bash
git add cli/package.json cli/src/index.ts cli/src/tui/index.tsx cli/src/tui/app.tsx cli/src/tui/app.test.tsx
git commit -m "feat(sprint-16): bootstrap React+Ink TUI shell"
```

### Task 1: Feature 1 Core shell, theme, and layout

**Files:**
- Create: `cli/src/tui/components/layout/app-layout.tsx`
- Create: `cli/src/tui/components/layout/status-bar.tsx`
- Create: `cli/src/tui/contexts/theme.tsx`
- Create: `cli/src/tui/themes/index.ts`
- Create: `cli/src/tui/themes/default.ts`
- Create: `cli/src/tui/themes/dracula.ts`
- Create: `cli/src/tui/themes/nord.ts`
- Create: `cli/src/tui/hooks/use-terminal-size.ts`
- Create: `cli/src/tui/contexts/theme.test.ts`
- Create: `cli/src/tui/hooks/use-terminal-size.test.ts`

**Step 1: Read competitor references for Feature 1**

Read:
- `docs/reference-code/gemini-cli/packages/cli/src/gemini.tsx`
- `docs/reference-code/gemini-cli/packages/cli/src/ui/App.tsx`
- `docs/reference-code/gemini-cli/packages/cli/src/ui/layouts/DefaultAppLayout.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/app.tsx`

Capture provider stack and layout spacing choices.

**Step 2: Write failing tests for theme resolution and terminal size hook**

Add tests for:
- default theme load
- explicit theme selection
- fallback behavior
- terminal dimensions return values

**Step 3: Implement theme model and provider**

Define `Theme` type including semantic colors (primary, secondary, accent, error, warning, text, textMuted, border, background, added, removed, context).

**Step 4: Implement `use-terminal-size` and shell layout components**

Create top/bottom status bars and central layout slots for messages/input.

**Step 5: Verify feature 1**

Run: `npm run test:run && npx tsc --noEmit`

**Step 6: Commit**

```bash
git add cli/src/tui/components/layout/app-layout.tsx cli/src/tui/components/layout/status-bar.tsx cli/src/tui/contexts/theme.tsx cli/src/tui/themes/index.ts cli/src/tui/themes/default.ts cli/src/tui/themes/dracula.ts cli/src/tui/themes/nord.ts cli/src/tui/hooks/use-terminal-size.ts cli/src/tui/contexts/theme.test.ts cli/src/tui/hooks/use-terminal-size.test.ts
git commit -m "feat(sprint-16): add TUI layout and theme system"
```

### Task 2: Feature 2 streaming chat and markdown rendering

**Files:**
- Create: `cli/src/tui/contexts/streaming.tsx`
- Create: `cli/src/tui/hooks/use-streaming.ts`
- Create: `cli/src/tui/components/chat/message-list.tsx`
- Create: `cli/src/tui/components/chat/message.tsx`
- Create: `cli/src/tui/components/chat/streaming-text.tsx`
- Create: `cli/src/tui/components/shared/markdown.tsx`
- Create: `cli/src/tui/components/shared/code-block.tsx`
- Create: `cli/src/tui/components/shared/markdown.test.tsx`
- Create: `cli/src/tui/components/chat/message-list.test.tsx`
- Create: `cli/src/tui/components/chat/message.test.tsx`
- Create: `cli/src/tui/contexts/streaming.test.tsx`

**Step 1: Read competitor references for Feature 2**

Read:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/sdk.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/index.tsx`
- `docs/reference-code/codex-cli/codex-rs/tui/src/streaming/chunking.rs`
- `docs/reference-code/aider/aider/mdstream.py`

Document batching cadence and streaming chunk behavior.

**Step 2: Write failing tests for event batch flush and message rendering variants**

Test cases:
- queue events flush at 16ms
- user/assistant/tool/error/system message render
- markdown output includes expected transformed content

**Step 3: Implement `StreamingProvider` batching queue**

Use `setTimeout` flush with 16ms interval and deterministic queue drain.

**Step 4: Implement markdown and code rendering path**

Use `marked` + `marked-terminal`, route code blocks through `cli-highlight`, apply theme colors.

**Step 5: Implement scroll logic in `message-list`**

Auto-scroll when at bottom; hold position when user scrolled up; show new-message indicator.

**Step 6: Verify feature 2**

Run: `npm run test:run && npx tsc --noEmit`

**Step 7: Commit**

```bash
git add cli/src/tui/contexts/streaming.tsx cli/src/tui/hooks/use-streaming.ts cli/src/tui/components/chat/message-list.tsx cli/src/tui/components/chat/message.tsx cli/src/tui/components/chat/streaming-text.tsx cli/src/tui/components/shared/markdown.tsx cli/src/tui/components/shared/code-block.tsx cli/src/tui/components/shared/markdown.test.tsx cli/src/tui/components/chat/message-list.test.tsx cli/src/tui/components/chat/message.test.tsx cli/src/tui/contexts/streaming.test.tsx
git commit -m "feat(sprint-16): implement streaming chat and markdown rendering"
```

### Task 3: Feature 3 composer input and autocomplete

**Files:**
- Create: `cli/src/tui/components/input/composer.tsx`
- Create: `cli/src/tui/components/input/autocomplete.tsx`
- Create: `cli/src/tui/components/input/composer.test.tsx`
- Create: `cli/src/tui/components/input/autocomplete.test.tsx`

**Step 1: Read competitor references for Feature 3**

Read:
- `docs/reference-code/gemini-cli/packages/cli/src/ui/components/Composer.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/prompt/index.tsx`
- `docs/reference-code/pi-mono/packages/tui/src/components/input.ts`

Capture cursor/history navigation and multiline handling.

**Step 2: Write failing tests for submit/history/autocomplete flows**

Include:
- enter submit
- ctrl+j newline
- up/down history
- slash command popup filtering
- mention popup filtering

**Step 3: Implement composer state and history persistence**

Persist JSONL history at `~/.ava/cli-history.jsonl` and support ctrl+c/ctrl+d behaviors.

**Step 4: Implement autocomplete popup with `fuzzysort`**

Arrow navigation, tab/enter select, escape dismiss.

**Step 5: Verify feature 3**

Run: `npm run test:run && npx tsc --noEmit`

**Step 6: Commit**

```bash
git add cli/src/tui/components/input/composer.tsx cli/src/tui/components/input/autocomplete.tsx cli/src/tui/components/input/composer.test.tsx cli/src/tui/components/input/autocomplete.test.tsx
git commit -m "feat(sprint-16): add composer input history and autocomplete"
```

### Task 4: Feature 4 tool approval system

**Files:**
- Create: `cli/src/tui/contexts/permission.tsx`
- Create: `cli/src/tui/components/approval/tool-approval.tsx`
- Create: `cli/src/tui/components/approval/diff-preview.tsx`
- Create: `cli/src/tui/components/approval/tool-approval.test.tsx`
- Create: `cli/src/tui/components/approval/diff-preview.test.tsx`
- Create: `cli/src/tui/contexts/permission.test.tsx`

**Step 1: Read competitor references for Feature 4**

Read:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/permission.tsx`
- `docs/reference-code/codex-cli/codex-rs/tui/src/bottom_pane/approval_overlay.rs`
- `docs/reference-code/gemini-cli/packages/cli/src/ui/components/messages/ToolConfirmationMessage.tsx`

Extract three-stage flow and queueing details.

**Step 2: Write failing tests for queue and modal stages**

Test:
- queue order
- allow once/session/reject/yolo transitions
- rejection optional reason input

**Step 3: Implement permission context queue API**

Expose enqueue/dequeue/current/policy helpers with deterministic state transitions.

**Step 4: Implement diff preview using `diff` and `chalk`**

Render unified diff with color mapping and bounded output handling.

**Step 5: Implement modal keyboard handling and stage transitions**

Block chat input while pending approval.

**Step 6: Verify feature 4**

Run: `npm run test:run && npx tsc --noEmit`

**Step 7: Commit**

```bash
git add cli/src/tui/contexts/permission.tsx cli/src/tui/components/approval/tool-approval.tsx cli/src/tui/components/approval/diff-preview.tsx cli/src/tui/components/approval/tool-approval.test.tsx cli/src/tui/components/approval/diff-preview.test.tsx cli/src/tui/contexts/permission.test.tsx
git commit -m "feat(sprint-16): add interactive tool approval flow"
```

### Task 5: Feature 5 keybindings and command palette

**Files:**
- Create: `cli/src/tui/contexts/keybind.tsx`
- Create: `cli/src/tui/hooks/use-keypress.ts`
- Create: `cli/src/tui/components/input/command-palette.tsx`
- Create: `cli/src/tui/contexts/keybind.test.ts`
- Create: `cli/src/tui/components/input/command-palette.test.tsx`

**Step 1: Read competitor references for Feature 5**

Read:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/keybind.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/dialog-command.tsx`
- `docs/reference-code/pi-mono/packages/tui/src/components/input.ts`

**Step 2: Write failing tests for key match + command palette search**

Test:
- default key map
- override from `~/.ava/keybindings.json`
- fuzzy filtering and command selection behavior

**Step 3: Implement keybind context and parser**

Normalize key descriptors (`ctrl+/`, `pageup`, etc.) and expose command trigger helpers.

**Step 4: Implement global keypress hook and command palette UI**

Add frecency ordering for recent commands and command execution callbacks.

**Step 5: Verify feature 5**

Run: `npm run test:run && npx tsc --noEmit`

**Step 6: Commit**

```bash
git add cli/src/tui/contexts/keybind.tsx cli/src/tui/hooks/use-keypress.ts cli/src/tui/components/input/command-palette.tsx cli/src/tui/contexts/keybind.test.ts cli/src/tui/components/input/command-palette.test.tsx
git commit -m "feat(sprint-16): add keybinding system and command palette"
```

### Task 6: Feature 6 session management UI

**Files:**
- Create: `cli/src/tui/contexts/session.tsx`
- Create: `cli/src/tui/components/layout/session-list.tsx`
- Create: `cli/src/tui/contexts/session.test.ts`
- Create: `cli/src/tui/components/layout/session-list.test.tsx`

**Step 1: Read competitor references for Feature 6**

Read:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/index.tsx`
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/home.tsx`
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/session-manager.ts`

**Step 2: Write failing tests for create/switch/resume/fork operations**

Include startup route variants: default, `--continue`, `--session`, goal shortcut.

**Step 3: Implement `SessionProvider` wrappers around `createSessionManager()`**

Expose list/search/current/create/switch/fork/rename/archive/export.

**Step 4: Implement session list dialog**

Fuzzy filter, keyboard navigation, create (`+`), archive (`Delete`), switch (`Enter`).

**Step 5: Verify feature 6**

Run: `npm run test:run && npx tsc --noEmit`

**Step 6: Commit**

```bash
git add cli/src/tui/contexts/session.tsx cli/src/tui/components/layout/session-list.tsx cli/src/tui/contexts/session.test.ts cli/src/tui/components/layout/session-list.test.tsx
git commit -m "feat(sprint-16): add session management UI and controls"
```

### Task 7: Feature 7 sidebar and info panels

**Files:**
- Create: `cli/src/tui/components/layout/sidebar.tsx`
- Modify: `cli/src/tui/components/layout/app-layout.tsx`
- Create: `cli/src/tui/components/layout/sidebar.test.tsx`
- Create: `cli/src/tui/components/layout/app-layout.test.tsx`

**Step 1: Read competitor references for Feature 7**

Read:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/sidebar.tsx`
- `docs/reference-code/codex-cli/codex-rs/tui/src/chatwidget.rs`

**Step 2: Write failing tests for responsive show/hide and section rendering**

Test terminal width thresholds and toggle behavior.

**Step 3: Implement sidebar component sections**

Session/model/files changed/agent status/errors with independent scroll.

**Step 4: Integrate sidebar in `app-layout` with responsive logic**

Default visible when width `>120`, togglable with keybind override.

**Step 5: Verify feature 7**

Run: `npm run test:run && npx tsc --noEmit`

**Step 6: Commit**

```bash
git add cli/src/tui/components/layout/sidebar.tsx cli/src/tui/components/layout/app-layout.tsx cli/src/tui/components/layout/sidebar.test.tsx cli/src/tui/components/layout/app-layout.test.tsx
git commit -m "feat(sprint-16): add responsive sidebar and info panels"
```

### Task 8: Feature 8 agent integration and event bridge

**Files:**
- Create: `cli/src/tui/hooks/use-agent.ts`
- Modify: `cli/src/tui/contexts/streaming.tsx`
- Modify: `cli/src/tui/index.tsx`
- Create: `cli/src/tui/hooks/use-agent.test.ts`
- Create: `cli/src/tui/index.test.tsx`
- Create: `cli/src/tui/components/shared/spinner.tsx`
- Create: `cli/src/tui/components/shared/dialog.tsx`

**Step 1: Read competitor references for Feature 8**

Read:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/sync.tsx`
- `docs/reference-code/gemini-cli/packages/cli/src/ui/hooks/useGeminiStream.ts`
- `cli/src/commands/run.ts`

**Step 2: Write failing tests for start/abort/permission bridge and TUI integration render**

Test:
- start sets running state
- abort cancels active run
- permission request pauses and resumes with reply

**Step 3: Implement `use-agent` lifecycle hook**

Return shape:

```ts
function useAgent(): {
  start: (goal: string) => void
  abort: () => void
  isRunning: boolean
  currentTurn: number
  maxTurns: number
  tokensUsed: { input: number; output: number }
}
```

**Step 4: Wire real agent events into streaming/permission/session contexts**

Bridge permission middleware request/reply, update tokens and turn counters.

**Step 5: Add slash command bridge handlers**

Implement `/model`, `/compact`, `/clear`, `/help`, `/recipe` actions.

**Step 6: Verify feature 8**

Run: `npm run test:run && npx tsc --noEmit`

**Step 7: Commit**

```bash
git add cli/src/tui/hooks/use-agent.ts cli/src/tui/contexts/streaming.tsx cli/src/tui/index.tsx cli/src/tui/hooks/use-agent.test.ts cli/src/tui/index.test.tsx cli/src/tui/components/shared/spinner.tsx cli/src/tui/components/shared/dialog.tsx
git commit -m "feat(sprint-16): integrate TUI with agent executor and event bridge"
```

### Task 9: Final verification and sprint completion

**Files:**
- Modify as needed from fixups identified by verification

**Step 1: Run final repo checks**

Run:
- `npm run test:run`
- `npx tsc --noEmit`
- `npm run lint`
- `npm run format:check`

Expected: all checks pass.

**Step 2: Validate file size constraints**

Confirm no newly created TUI file exceeds 300 lines.

**Step 3: Manual smoke run**

Run: `npx tsx cli/src/index.ts`
Expected: TUI shell launches and accepts input.

**Step 4: Final sprint commit (only if explicitly requested)**

If user requests a final aggregate commit:

```bash
git add cli/src cli/package.json
git commit -m "feat(sprint-16): interactive TUI with Ink"
```

Otherwise keep 8 feature commits as delivered.
