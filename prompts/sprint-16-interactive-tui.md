# Sprint 16: Interactive TUI — Implementation Prompt

> For AI coding agent. Estimated: 8 features, mix M/L effort.
> Run `npm run test:run && npx tsc --noEmit` after each feature.

---

## Role

You are implementing Sprint 16 (Interactive TUI) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, architecture, dispatchCompute pattern)
- `AGENTS.md` (code standards, common workflows)
- `docs/research/tui-comparison-matrix.md` (competitor analysis — read the FULL document)

**IMPORTANT**: The TUI uses **React + Ink** (NOT SolidJS). The desktop app uses SolidJS, but the CLI TUI is a separate React app using Ink for terminal rendering. Do NOT mix SolidJS and React patterns.

---

## Pre-Implementation: Competitor Research Phase

**CRITICAL**: Before implementing each feature, you MUST read the relevant competitor reference code and extract best patterns.

For EACH feature:
1. **Read** the listed competitor reference files
2. **Extract** key patterns (architecture, state management, rendering tricks)
3. **Adapt** to AVA's Ink + TypeScript architecture
4. **Implement** (<300 lines/file, no `any`)
5. **Test** + verify

---

## Setup: Dependencies & Project Structure

### Step 0: Install Dependencies

Add to `cli/package.json`:
```json
{
  "dependencies": {
    "ink": "^5.1.0",
    "ink-text-input": "^6.0.0",
    "ink-spinner": "^5.0.0",
    "ink-select-input": "^6.0.0",
    "react": "^19.0.0",
    "react-dom": "^19.0.0",
    "marked": "^15.0.0",
    "cli-highlight": "^2.1.11",
    "fuzzysort": "^3.0.0",
    "diff": "^7.0.0"
  },
  "devDependencies": {
    "@types/react": "^19.0.0",
    "ink-testing-library": "^4.0.0"
  }
}
```

Check which dependencies already exist in the workspace before adding duplicates. Use workspace versions where available.

### Step 0b: Create TUI Directory Structure

```
cli/src/tui/
  index.tsx                  # Entry point — render(<App />)
  app.tsx                    # Root component + provider stack
  contexts/
    streaming.tsx            # Event batching + streaming state
    session.tsx              # Session management
    keybind.tsx              # Keyboard shortcuts
    theme.tsx                # Terminal themes
    permission.tsx           # Tool approval state
  components/
    chat/
      message-list.tsx       # Scrollable message history
      message.tsx            # Single message renderer
      streaming-text.tsx     # Real-time markdown streaming
    input/
      composer.tsx           # Multi-line text input
      autocomplete.tsx       # @mentions + /commands
      command-palette.tsx    # Fuzzy command search
    approval/
      tool-approval.tsx      # 3-stage approval modal
      diff-preview.tsx       # Inline diff display
    layout/
      app-layout.tsx         # Main layout: header + messages + input + footer
      sidebar.tsx            # Optional info panel
      status-bar.tsx         # Model, tokens, session info
    shared/
      markdown.tsx           # Terminal markdown renderer
      code-block.tsx         # Syntax-highlighted code
      spinner.tsx            # Loading indicators
      dialog.tsx             # Modal dialog system
  hooks/
    use-streaming.ts         # Streaming event handler
    use-keypress.ts          # Global key events
    use-terminal-size.ts     # Responsive layout
  themes/
    index.ts                 # Theme loader + dark/light detection
    dracula.ts
    nord.ts
    default.ts
```

---

## Feature 1: Core TUI Shell & Layout

### Competitor Research
Read these files:
- `docs/reference-code/gemini-cli/packages/cli/src/gemini.tsx` — Ink render entry point
- `docs/reference-code/gemini-cli/packages/cli/src/ui/App.tsx` — Root component
- `docs/reference-code/gemini-cli/packages/cli/src/ui/layouts/DefaultAppLayout.tsx` — Layout structure
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/app.tsx` — Provider stack pattern

### What to Build
The foundational Ink app with layout, theme system, and provider stack.

**Files:**
- `cli/src/tui/index.tsx` — Entry point
- `cli/src/tui/app.tsx` — Root component with provider nesting
- `cli/src/tui/components/layout/app-layout.tsx` — Main layout
- `cli/src/tui/components/layout/status-bar.tsx` — Bottom status bar
- `cli/src/tui/contexts/theme.tsx` — Theme context + dark/light detection
- `cli/src/tui/themes/index.ts` — Theme definitions
- `cli/src/tui/hooks/use-terminal-size.ts` — Terminal dimensions hook

**Implementation:**

Entry point (`index.tsx`):
```typescript
import { render } from 'ink'
import React from 'react'
import { App } from './app.js'

export function startTUI(options: TUIOptions): void {
  const { waitUntilExit } = render(<App {...options} />)
  waitUntilExit().then(() => process.exit(0))
}
```

App layout structure:
```
+-----------------------------------------------+
| AVA v{version}  |  model: claude-sonnet  | ... |  <- StatusBar (top)
+-----------------------------------------------+
|                                               |
|  [messages scroll area]                       |  <- MessageList
|                                               |
+-----------------------------------------------+
| > user input here...                          |  <- Composer
+-----------------------------------------------+
| session: abc123 | tokens: 1.2k | Ctrl+/ help |  <- StatusBar (bottom)
+-----------------------------------------------+
```

Provider stack (nested in App):
```typescript
<ThemeProvider>
  <SessionProvider options={options}>
    <StreamingProvider>
      <KeybindProvider>
        <PermissionProvider>
          <AppLayout />
        </PermissionProvider>
      </KeybindProvider>
    </StreamingProvider>
  </SessionProvider>
</ThemeProvider>
```

Theme system:
- Auto-detect dark/light via OSC 11 query (like OpenCode)
- 3 built-in themes: default (auto), dracula (dark), nord (dark)
- Colors: primary, secondary, accent, error, warning, text, textMuted, border, background
- Diff colors: added, removed, context

**Integration:** Add `tui` command to `cli/src/index.ts`. Running `ava` with no args launches TUI instead of showing help.

### Tests
- `cli/src/tui/app.test.tsx` — Renders without crash using ink-testing-library
- `cli/src/tui/contexts/theme.test.ts` — Theme loading + color resolution
- `cli/src/tui/hooks/use-terminal-size.test.ts` — Returns dimensions

---

## Feature 2: Streaming Chat & Message Rendering

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/sdk.tsx` — Event batching (16ms)
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/index.tsx` — Message rendering
- `docs/reference-code/codex-cli/codex-rs/tui/src/streaming/chunking.rs` — Adaptive streaming
- `docs/reference-code/aider/aider/mdstream.py` — Sliding window markdown streaming

### What to Build
Real-time streaming message display with event batching and markdown rendering.

**Files:**
- `cli/src/tui/contexts/streaming.tsx` — StreamingContext with event batching
- `cli/src/tui/hooks/use-streaming.ts` — Streaming hook for components
- `cli/src/tui/components/chat/message-list.tsx` — Scrollable message list
- `cli/src/tui/components/chat/message.tsx` — Individual message renderer
- `cli/src/tui/components/chat/streaming-text.tsx` — Live streaming text with markdown
- `cli/src/tui/components/shared/markdown.tsx` — Terminal markdown renderer
- `cli/src/tui/components/shared/code-block.tsx` — Syntax-highlighted code blocks

**Implementation:**

Event batching (from OpenCode pattern):
```typescript
// Queue events for 16ms windows, flush as batch
const BATCH_INTERVAL = 16 // ~60fps
const eventQueue: AgentEvent[] = []
let flushTimer: NodeJS.Timeout | null = null

function queueEvent(event: AgentEvent): void {
  eventQueue.push(event)
  if (!flushTimer) {
    flushTimer = setTimeout(flush, BATCH_INTERVAL)
  }
}
```

Message types to render:
- **User message** — plain text with username prefix
- **Assistant message** — markdown with streaming indicator
- **Tool call** — tool name + args (collapsible) + result + duration
- **Thinking** — collapsible thinking block (dimmed)
- **Error** — red error card
- **System** — info messages (turn markers, compression notices)

Markdown rendering:
- Use `marked` for parsing markdown to tokens
- Use `cli-highlight` for syntax highlighting in code blocks
- Render to Ink `<Text>` components with appropriate colors
- Handle: headings, bold, italic, code, code blocks, lists, links, blockquotes

Scrolling:
- Track scroll position with `useState`
- Page up/down moves by half terminal height
- Auto-scroll to bottom on new messages (unless user scrolled up)
- Show "New messages below" indicator when scrolled up

### Tests
- `cli/src/tui/components/chat/message-list.test.tsx` — Renders messages
- `cli/src/tui/components/chat/message.test.tsx` — Renders each message type
- `cli/src/tui/components/shared/markdown.test.tsx` — Markdown to terminal text
- `cli/src/tui/contexts/streaming.test.tsx` — Event batching flushes correctly

---

## Feature 3: Composer Input & History

### Competitor Research
Read these files:
- `docs/reference-code/gemini-cli/packages/cli/src/ui/components/Composer.tsx` — Ink input component
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/prompt/index.tsx` — Multi-part prompt
- `docs/reference-code/pi-mono/packages/tui/src/components/input.ts` — Input with kill ring + word nav

### What to Build
Multi-line text input with history navigation, slash commands, and @mentions.

**Files:**
- `cli/src/tui/components/input/composer.tsx` — Main input component
- `cli/src/tui/components/input/autocomplete.tsx` — Autocomplete popup

**Implementation:**

Composer features:
- Multi-line input (Shift+Enter or Ctrl+J for newline, Enter to submit)
- History navigation (Up/Down when cursor at start/end)
- Persistent history saved to `~/.ava/cli-history.jsonl`
- Slash command detection: typing `/` shows autocomplete popup
- @mention detection: typing `@` shows file/agent autocomplete
- Paste detection: handle bracketed paste mode for multi-line content
- Ctrl+C: cancel current input (or abort running agent)
- Ctrl+D: exit TUI
- Word navigation: Ctrl+Left/Right, Ctrl+Backspace (delete word)

Autocomplete popup:
- Shows below input when typing `/` or `@`
- Fuzzy search with `fuzzysort`
- Arrow keys to navigate, Tab/Enter to select, Escape to dismiss
- Slash commands: `/help`, `/model`, `/session`, `/clear`, `/recipe`, `/compact`
- @mentions: `@file.ts` (reads file into context), `@agent` (invoke agent)

### Tests
- `cli/src/tui/components/input/composer.test.tsx` — Input, submit, history
- `cli/src/tui/components/input/autocomplete.test.tsx` — Fuzzy search + selection

---

## Feature 4: Tool Approval System

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/permission.tsx` — 3-stage approval
- `docs/reference-code/codex-cli/codex-rs/tui/src/bottom_pane/approval_overlay.rs` — Modal approval overlay
- `docs/reference-code/gemini-cli/packages/cli/src/ui/components/messages/ToolConfirmationMessage.tsx` — Tool confirmation

### What to Build
Interactive 3-stage tool approval flow in the terminal.

**Files:**
- `cli/src/tui/contexts/permission.tsx` — Permission state + queue
- `cli/src/tui/components/approval/tool-approval.tsx` — Approval modal
- `cli/src/tui/components/approval/diff-preview.tsx` — Inline diff display

**Implementation:**

3-stage approval flow (adapted from OpenCode):

**Stage 1 — Preview:**
- Shows tool name + icon (bash -> `$`, edit -> pencil, read -> eye, etc.)
- Shows arguments formatted as key-value pairs
- For file edits: shows unified diff with +/- coloring
- For bash commands: shows command + working directory

**Stage 2 — Action Selection:**
- `[a] Allow once` — run this tool call
- `[s] Allow for session` — auto-approve this tool for rest of session
- `[r] Reject` — deny this call
- `[y] YOLO mode` — auto-approve everything (like --yolo flag)
- Navigate with arrow keys or press shortcut key

**Stage 3 — Rejection reason (optional):**
- If rejected, optional text input for rejection message sent back to agent
- Enter to confirm, Escape to skip

Permission queue:
- Multiple tool calls queue up (agent may request several)
- Process one at a time
- Show queue count: "Approval 1 of 3"
- Modal blocks input to chat while approval pending

Diff preview:
- Use `diff` npm package to generate unified diff
- Color: green for additions, red for deletions, gray for context
- Line numbers on both sides
- File path header

### Tests
- `cli/src/tui/components/approval/tool-approval.test.tsx` — 3 stages render
- `cli/src/tui/components/approval/diff-preview.test.tsx` — Diff coloring
- `cli/src/tui/contexts/permission.test.tsx` — Queue management

---

## Feature 5: Keyboard Shortcuts & Command Palette

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/keybind.tsx` — Leader key system
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/component/dialog-command.tsx` — Command palette
- `docs/reference-code/pi-mono/packages/tui/src/components/input.ts` — Keybinding config

### What to Build
Configurable keyboard shortcuts with leader key support and a fuzzy command palette.

**Files:**
- `cli/src/tui/contexts/keybind.tsx` — Keybind context + matching
- `cli/src/tui/hooks/use-keypress.ts` — Global key event hook
- `cli/src/tui/components/input/command-palette.tsx` — Fuzzy command search dialog

**Implementation:**

Default keybindings:
```typescript
const DEFAULT_KEYBINDS = {
  'command_palette': ['ctrl+/'],
  'new_session': ['ctrl+n'],
  'session_list': ['ctrl+k'],
  'model_switch': ['ctrl+m'],
  'scroll_up': ['pageup'],
  'scroll_down': ['pagedown'],
  'scroll_top': ['home'],
  'scroll_bottom': ['end'],
  'toggle_sidebar': ['ctrl+s'],
  'toggle_thinking': ['ctrl+t'],
  'cancel': ['ctrl+c'],
  'quit': ['ctrl+d'],
  'yolo_toggle': ['ctrl+y'],
}
```

Keybind config file: `~/.ava/keybindings.json` (override defaults)

Command palette:
- Opens with Ctrl+/ (like VS Code)
- Fuzzy search all available commands
- Shows command name + keybinding + category
- Categories: Session, Model, Navigation, View, Agent
- Enter to execute, Escape to close
- Recent commands shown first (frecency)

Commands include:
- New Session, Switch Session, Rename Session, Delete Session
- Switch Model, Switch Provider
- Toggle Sidebar, Toggle Thinking Blocks, Toggle Tool Details
- Clear Chat, Export Session, Fork Session
- YOLO Mode Toggle
- Run Recipe (lists available recipes)

### Tests
- `cli/src/tui/contexts/keybind.test.ts` — Key matching, config override
- `cli/src/tui/components/input/command-palette.test.tsx` — Fuzzy search + selection

---

## Feature 6: Session Management UI

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/index.tsx` — Session view
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/home.tsx` — Home/session list
- `docs/reference-code/pi-mono/packages/coding-agent/src/core/session-manager.ts` — Session DAG

### What to Build
Session list, switching, resume, and fork UI.

**Files:**
- `cli/src/tui/contexts/session.tsx` — Session state + operations
- `cli/src/tui/components/layout/session-list.tsx` — Session picker dialog

**Implementation:**

Session context integrates with existing `createSessionManager()` from core-v2.

Session list dialog (Ctrl+K):
- Shows all sessions sorted by last updated
- Each row: session name, date, message count, model used
- Fuzzy search to filter
- Enter to switch, Delete to archive
- `+` to create new session

Session operations:
- **New** (Ctrl+N): creates fresh session, clears chat
- **Switch** (Ctrl+K): open session list
- **Resume** (on startup): `ava --continue` resumes last session
- **Fork**: create branch from current point in conversation
- **Rename**: edit session name inline
- **Export**: download as JSON

Startup behavior:
- `ava` — new session (home screen with prompt)
- `ava --continue` or `ava -c` — resume last session
- `ava --session <id>` — resume specific session
- `ava "goal"` — new session with pre-filled goal (submits immediately)

### Tests
- `cli/src/tui/contexts/session.test.ts` — Create, switch, resume, fork
- `cli/src/tui/components/layout/session-list.test.tsx` — List rendering + search

---

## Feature 7: Sidebar & Info Panels

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/routes/session/sidebar.tsx` — Sidebar content
- `docs/reference-code/codex-cli/codex-rs/tui/src/chatwidget.rs` — Search for sidebar/panel sections

### What to Build
Optional sidebar showing session metadata, diffs, and agent status.

**Files:**
- `cli/src/tui/components/layout/sidebar.tsx` — Sidebar component
- `cli/src/tui/components/layout/app-layout.tsx` — Update to include sidebar

**Implementation:**

Sidebar (shown when terminal width > 120 chars, toggled with Ctrl+S):

```
+------------------+
| Session          |
|   abc123 (5 min) |
+------------------+
| Model            |
|   claude-sonnet  |
|   tokens: 12.4k  |
+------------------+
| Files Changed    |
|   M src/app.ts   |
|   + src/util.ts  |
|   M package.json |
+------------------+
| Agent Status     |
|   Turn 3/20      |
|   Running...     |
+------------------+
```

Sections:
- **Session**: name, duration, ID
- **Model**: current model, token usage (input/output), cost estimate
- **Files Changed**: list of files modified in this session (from tool events)
- **Agent Status**: current turn, max turns, running/idle/waiting for approval
- **Errors**: count of errors in session (if any)

Responsive behavior:
- Width <= 120: sidebar hidden (toggle with Ctrl+S overrides)
- Width > 120: sidebar shown by default (42 char wide, like OpenCode)
- Sidebar content scrollable independently

### Tests
- `cli/src/tui/components/layout/sidebar.test.tsx` — Renders sections
- `cli/src/tui/components/layout/app-layout.test.tsx` — Sidebar show/hide responsive

---

## Feature 8: Agent Integration & Event Bridge

### Competitor Research
Read these files:
- `docs/reference-code/opencode/packages/opencode/src/cli/cmd/tui/context/sync.tsx` — Real-time sync
- `docs/reference-code/gemini-cli/packages/cli/src/ui/hooks/useGeminiStream.ts` — Stream consumption
- `cli/src/commands/run.ts` — Existing CLI agent integration (AVA's current approach)

### What to Build
Bridge between the TUI and AVA's agent executor, connecting all the UI to the actual agent loop.

**Files:**
- `cli/src/tui/hooks/use-agent.ts` — Agent lifecycle hook
- `cli/src/tui/contexts/streaming.tsx` — Update to wire real agent events
- `cli/src/tui/index.tsx` — Update to initialize platform + extensions

**Implementation:**

Agent hook (`useAgent`):
```typescript
function useAgent(): {
  start: (goal: string) => void
  abort: () => void
  isRunning: boolean
  currentTurn: number
  maxTurns: number
  tokensUsed: { input: number; output: number }
}
```

Integration flow:
1. TUI initializes platform (`createNodePlatform`) + extensions (reuse from `run.ts`)
2. User types goal in Composer, presses Enter
3. `useAgent().start(goal)` creates AgentExecutor and runs
4. Agent events flow through StreamingContext → MessageList renders them
5. Tool calls that need approval flow through PermissionContext → ToolApproval renders
6. User approves/rejects → result flows back to agent
7. Agent completes → summary shown in chat

Permission bridge:
- Agent's permission middleware emits `permission:request` events
- TUI catches these, pauses agent, shows approval UI
- User decision flows back via `permission:reply` event
- Agent resumes with approved/rejected result

Extension loading:
- Reuse `loadAllBuiltInExtensions()` from existing CLI
- Skip LSP, MCP, server extensions (same as current CLI)
- Load prompts extension for system prompt building
- Load instructions extension for project-specific instructions

Slash command integration:
- `/model <name>` — switch model mid-session
- `/compact` — trigger context compaction
- `/clear` — clear display (not session)
- `/help` — show available commands
- `/recipe <name>` — run a recipe (Sprint 15's recipe system)

### Tests
- `cli/src/tui/hooks/use-agent.test.ts` — Start, abort, event flow
- `cli/src/tui/index.test.tsx` — Full integration renders + accepts input

---

## Post-Implementation Verification

After ALL 8 features:

1. `npm run test:run`
2. `npx tsc --noEmit`
3. `npm run lint`
4. `npm run format:check`
5. Verify no files exceed 300 lines
6. Manual test: `npx tsx cli/src/index.ts` — should show TUI
7. Commit: `git commit -m "feat(sprint-16): interactive TUI with Ink"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `cli/src/tui/index.tsx` |
| CREATE | `cli/src/tui/app.tsx` |
| CREATE | `cli/src/tui/app.test.tsx` |
| CREATE | `cli/src/tui/contexts/streaming.tsx` |
| CREATE | `cli/src/tui/contexts/streaming.test.tsx` |
| CREATE | `cli/src/tui/contexts/session.tsx` |
| CREATE | `cli/src/tui/contexts/session.test.ts` |
| CREATE | `cli/src/tui/contexts/keybind.tsx` |
| CREATE | `cli/src/tui/contexts/keybind.test.ts` |
| CREATE | `cli/src/tui/contexts/theme.tsx` |
| CREATE | `cli/src/tui/contexts/theme.test.ts` |
| CREATE | `cli/src/tui/contexts/permission.tsx` |
| CREATE | `cli/src/tui/contexts/permission.test.tsx` |
| CREATE | `cli/src/tui/components/layout/app-layout.tsx` |
| CREATE | `cli/src/tui/components/layout/app-layout.test.tsx` |
| CREATE | `cli/src/tui/components/layout/status-bar.tsx` |
| CREATE | `cli/src/tui/components/layout/sidebar.tsx` |
| CREATE | `cli/src/tui/components/layout/sidebar.test.tsx` |
| CREATE | `cli/src/tui/components/layout/session-list.tsx` |
| CREATE | `cli/src/tui/components/layout/session-list.test.tsx` |
| CREATE | `cli/src/tui/components/chat/message-list.tsx` |
| CREATE | `cli/src/tui/components/chat/message-list.test.tsx` |
| CREATE | `cli/src/tui/components/chat/message.tsx` |
| CREATE | `cli/src/tui/components/chat/message.test.tsx` |
| CREATE | `cli/src/tui/components/chat/streaming-text.tsx` |
| CREATE | `cli/src/tui/components/shared/markdown.tsx` |
| CREATE | `cli/src/tui/components/shared/markdown.test.tsx` |
| CREATE | `cli/src/tui/components/shared/code-block.tsx` |
| CREATE | `cli/src/tui/components/shared/spinner.tsx` |
| CREATE | `cli/src/tui/components/shared/dialog.tsx` |
| CREATE | `cli/src/tui/components/input/composer.tsx` |
| CREATE | `cli/src/tui/components/input/composer.test.tsx` |
| CREATE | `cli/src/tui/components/input/autocomplete.tsx` |
| CREATE | `cli/src/tui/components/input/autocomplete.test.tsx` |
| CREATE | `cli/src/tui/components/input/command-palette.tsx` |
| CREATE | `cli/src/tui/components/input/command-palette.test.tsx` |
| CREATE | `cli/src/tui/components/approval/tool-approval.tsx` |
| CREATE | `cli/src/tui/components/approval/tool-approval.test.tsx` |
| CREATE | `cli/src/tui/components/approval/diff-preview.tsx` |
| CREATE | `cli/src/tui/components/approval/diff-preview.test.tsx` |
| CREATE | `cli/src/tui/hooks/use-streaming.ts` |
| CREATE | `cli/src/tui/hooks/use-keypress.ts` |
| CREATE | `cli/src/tui/hooks/use-terminal-size.ts` |
| CREATE | `cli/src/tui/hooks/use-terminal-size.test.ts` |
| CREATE | `cli/src/tui/hooks/use-agent.ts` |
| CREATE | `cli/src/tui/hooks/use-agent.test.ts` |
| CREATE | `cli/src/tui/themes/index.ts` |
| CREATE | `cli/src/tui/themes/default.ts` |
| CREATE | `cli/src/tui/themes/dracula.ts` |
| CREATE | `cli/src/tui/themes/nord.ts` |
| MODIFY | `cli/src/index.ts` (add `tui` command, change default to launch TUI) |
| MODIFY | `cli/package.json` (add ink + react dependencies) |
