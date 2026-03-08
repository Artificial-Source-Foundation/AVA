# Sprint 16 Interactive TUI Design

## Goal

Implement an interactive CLI TUI for AVA using React + Ink, replacing the current non-interactive default CLI flow with a chat-first terminal UX that supports streaming, approvals, keybindings, sessions, and agent integration.

## Scope and Constraints

- TUI runtime is React + Ink only (no SolidJS patterns in `cli/src/tui`).
- Follow AVA repository conventions (`CLAUDE.md`, `AGENTS.md`): strict TypeScript, no `any`, file length under 300 lines.
- For each feature, read listed competitor references before implementation.
- Verify each feature with `npm run test:run && npx tsc --noEmit`.
- Deliver as one commit per feature (8 commits total).

## Architecture

The TUI is composed from a provider stack and a feature-sliced directory structure rooted in `cli/src/tui`.

Provider order:

1. `ThemeProvider`
2. `SessionProvider`
3. `StreamingProvider`
4. `KeybindProvider`
5. `PermissionProvider`
6. `AppLayout`

The stack centralizes state and keeps rendering components focused on display and interaction.

## Major Components

### Entry and App Shell

- `cli/src/tui/index.tsx`: Ink render bootstrap and process lifecycle.
- `cli/src/tui/app.tsx`: provider composition and top-level behavior.
- `cli/src/tui/components/layout/app-layout.tsx`: primary frame with status, messages, input, and optional sidebar.

### Chat and Streaming

- `contexts/streaming.tsx` batches agent events at 16ms windows to avoid excessive re-renders.
- `components/chat/*` renders user/assistant/tool/system/error/thinking message variants.
- Markdown rendering uses `marked` + `marked-terminal`; syntax highlighting uses `cli-highlight`.

### Composer and Commanding

- `components/input/composer.tsx` supports multi-line input, history, submit/cancel/quit behavior.
- `components/input/autocomplete.tsx` handles slash-command and mention completions with `fuzzysort`.
- `components/input/command-palette.tsx` enables fuzzy command execution via keybind.

### Tool Approval

- `contexts/permission.tsx` manages approval queue and session/YOLO policy.
- `components/approval/tool-approval.tsx` implements 3-stage modal flow.
- `components/approval/diff-preview.tsx` renders colored unified diffs using `diff` + `chalk`.

### Sessions and Sidebar

- `contexts/session.tsx` adapts `createSessionManager()` operations for UI consumption.
- `components/layout/session-list.tsx` supports session search/switch/create/archive.
- `components/layout/sidebar.tsx` shows session/model/files/status/error metadata responsively.

### Agent Bridge

- `hooks/use-agent.ts` encapsulates agent start/abort/lifecycle + token/turn tracking.
- Reuses CLI integration patterns from `cli/src/commands/run.ts` for platform init, extension loading, prompt/instructions handling, and event bridging.

## Data Flow

1. User submits goal in composer.
2. `useAgent().start(goal)` initializes executor and session context.
3. Agent events stream into `StreamingContext`, queued at 16ms intervals.
4. Message list re-renders from flushed batches.
5. Tool permission requests route into `PermissionContext` queue.
6. Approval decision returns to paused agent via permission reply bridge.
7. Session context and sidebar update metadata (tokens, files changed, status).

## UX Rules

- Terminal width drives responsive sidebar default behavior (`>120` shows by default).
- Auto-scroll stays pinned to bottom unless user manually scrolls up.
- “New messages below” cue appears while user is scrolled away from bottom.
- Keybinds are configurable from `~/.ava/keybindings.json` and merged with defaults.

## Risk Management

- **Streaming performance risk:** mitigated with strict 16ms event batching and minimal per-flush transforms.
- **Approval race risk:** single-consumer queue with deterministic next-item progression.
- **Keybinding conflict risk:** normalized key descriptor matching and central dispatch.
- **Integration drift risk:** keep bootstrap and extension activation behavior aligned to `run.ts`.

## Verification Strategy

- Per-feature verification: `npm run test:run && npx tsc --noEmit`.
- Final verification: `npm run test:run`, `npx tsc --noEmit`, `npm run lint`, `npm run format:check`, manual `npx tsx cli/src/index.ts`.
- Add focused tests for each new context/hook/component listed in Sprint 16 scope.

## Deliverables

- New `cli/src/tui/` subtree with contexts, components, hooks, themes, and tests.
- `cli/src/index.ts` updated to add `tui` command and launch TUI by default when no args.
- `cli/package.json` updated with Ink/React/rendering/search/diff dependencies.
