# AVA HQ -- Director Mode

> Crate: `ava-hq`
> Desktop bridge: `src-tauri/src/commands/hq_commands.rs`
> Frontend shell: `src/components/hq/`
> Status: active implementation; simplified 4-view shell is live, deeper orchestration/memory behavior still evolving

## What HQ is

HQ is AVA's orchestration mode.

Instead of chatting with one coding agent directly, the user talks to a single persistent
**Director of Engineering**. The Director understands the project, decides how much process a
task needs, delegates work to specialists, tracks execution, and reports back in one calm,
continuous conversation.

The important product idea is:

- the user talks to **one Director chat**
- the Director manages the team behind the scenes
- the team is visible, but the Director remains the main control surface

This should feel closer to talking to a trusted chief of staff than operating a kanban board.

## Product direction

The current codebase already ships real HQ runtime state (plans, epics, issues, agents, activity,
director chat, settings). The target product direction discussed for HQ goes further:

- one persistent Director conversation per project
- first-run HQ onboarding that sets up the Director and HQ memory
- private repo-local HQ memory in `.ava/HQ/`
- plan-first execution for medium/complex work
- richer Board-of-Directors consultation on high-complexity work
- a simplified 4-view UI instead of many separate management screens

When updating HQ, prefer this direction over the older "many management screens" concept.

## Core UX model

### One persistent Director chat

This is the main HQ experience.

- The Director chat is the primary surface.
- The user should not need to manage separate HQ sessions/epics as the main mental model.
- The Director remembers the project through structured memory, not by keeping an infinitely long
  raw chat context.
- Other HQ views are supporting visibility surfaces, not the primary place where control happens.

### Complexity levels

The Director should automatically decide how much machinery to use:

| Level | When | Behavior |
|---|---|---|
| 1 | Simple, local, obvious work | One worker + lightweight QA path |
| 2 | Multi-file but understandable work | Scouts -> plan -> user review -> leads/workers |
| 3 | Architecture / risky / broad work | Scouts -> Board -> plan review -> full execution |

### User control philosophy

- Users steer the **Director**, not individual workers by default.
- Users should still be able to pause/cancel runs and inspect worker state.
- HQ should expose what it is doing, what it believes, what changed, and when it needs
  confirmation.

## Team model

Current naming direction:

```text
Director (strongest available model)
|- CTO / engineering leads
|  |- backend workers
|  |- frontend workers
|- QA lead
|  |- QA workers
|- scouts (cheap, ephemeral)
`- Board of Directors (opt-in for complex tasks)
```

Representative worker names in the current product direction include:

- Backend: Pedro, Sofia
- Frontend: Luna
- QA: Kai, Mira

The exact runtime wiring may evolve, but the UI and planning model should preserve this feeling of
an understandable org chart with named specialists.

## Simplified HQ UI

HQ should be organized around **4 views**, not the earlier 9-11 screen concept.

### 1. Director Chat

This is the default and most important view.

- Reuse the normal AVA chat surface as much as possible.
- HQ-specific UI appears as inline cards inside the Director conversation.
- Expected inline cards include:
  - scout reports
  - worker status cards
  - worker completion cards
  - memory update cards
  - handoff cards
  - board vote summaries

### 2. Overview

Operational dashboard for the current HQ run and overall HQ state.

- metric cards
- current mission / phase context
- activity feed
- agent roster and live status

### 3. Team

This replaces the old org-chart-only mental model.

- a useful "team office" / office-floor view
- director office + engineering wing + QA wing
- should be delightful, but not pure novelty
- should allow useful inspection of worker state/transcript/handoff context

### 4. Plan Review

This reuses the existing Plannotator-style plan experience.

- phase/task structure
- annotations and revision loop
- assigned agent + model info
- parallel/sequential markers
- QA checkpoints
- Board-of-Directors panel when used

## Shared shell expectations

All HQ views share one sidebar shell.

The sidebar should include:

- HQ identity / logo
- Chat / Overview / Team navigation
- live worker status list
- metrics footer (cost, files, success, etc.)
- Back to Chat action

Plan Review should still feel like HQ while preserving the same shared shell language.

## HQ memory model

The persistent Director chat should **not** rely on raw chat history forever.

The intended model is a repo-local HQ memory system under:

```text
.ava/HQ/
```

This folder is the Director's private notebook / office memory.

### Memory philosophy

Think of HQ memory like a real office:

- **Front page** -- what the Director should always know
- **Desk** -- active working state that changes often
- **Filing cabinet** -- older durable memory and summaries

The live chat is the conversation.
The files in `.ava/HQ/` are the Director's brain.

### Recommended structure

```text
.ava/HQ/
  FRONT_PAGE.md
  MANIFEST.md
  DESK/
    current-status.md
    backlog.md
    handoff.md
    proposed-updates.md
  CABINET/
    index.md
    decisions/
    summaries/
    archive/
```

### File roles

- `FRONT_PAGE.md`
  - tiny always-read orientation file
  - what the project is, stack, current HQ purpose, non-negotiables

- `MANIFEST.md`
  - tells the Director what each HQ file is for
  - effectively the routing guide for HQ memory

- `DESK/current-status.md`
  - where work stands right now

- `DESK/backlog.md`
  - near-term priorities, not a giant dump

- `DESK/handoff.md`
  - the note the Director leaves for its future self

- `DESK/proposed-updates.md`
  - important changes HQ believes may be true, but wants confirmation on

- `CABINET/decisions/`
  - durable important decisions (ADRs / major project truths)

- `CABINET/summaries/`
  - compacted session and phase summaries

- `CABINET/archive/`
  - older memory that should exist but is not top-of-mind

### Memory governance

Default behavior should be:

- **auto-update**
  - active notes
  - handoff
  - compacted summaries
  - current status

- **confirm with user**
  - important project truths
  - coding conventions
  - major assumptions
  - architecture-level updates

- **hard approval**
  - HQ operating rules
  - budget/policy changes
  - anything that changes permanent system behavior

Important: HQ should not silently rewrite durable truth. It should propose important updates and
let the user confirm them.

### Git policy for HQ memory

Default expectation:

- `.ava/HQ/` is private working memory
- it should be ignored by git by default
- HQ should never silently commit it
- users may choose to promote selected knowledge into normal project docs under `docs/`

This keeps the Director's notebook private while allowing confirmed decisions to graduate into
official project documentation.

## First-run onboarding

On the first HQ run for a project, HQ should perform a lightweight onboarding flow.

Desired behavior:

1. user enables HQ
2. user chooses Director model / team defaults as needed
3. Director starts with a setup prompt
4. scouts inspect the repo/codebase
5. HQ creates `.ava/HQ/`
6. HQ writes the initial front-page/desk/cabinet files
7. user can confirm any important project truths HQ inferred

This onboarding is not just configuration; it is the initial creation of the Director's memory.

Headless support matters too. HQ memory bootstrap should be runnable without the desktop UI so the
backend can be tested directly in CLI/headless flows.

Current CLI path:

```bash
ava hq init
ava hq init --force
ava hq init --director-model openrouter:anthropic/claude-opus-4.1
```

## Current runtime and persistence

Today HQ already persists real runtime state through SQLite-backed Tauri commands.

Persisted surfaces include:

- epics
- issues
- comments
- plans
- agents
- activity feed
- director chat
- HQ settings

Current caveat: replanning currently replaces the generated issue set for an epic. Preserve this
behavior intentionally, but HQ now remaps issue comments onto replacement issues by matching task
titles so plan revision feedback is not immediately lost during replans.

Important implementation files:

- `src-tauri/src/commands/hq_commands.rs`
- `crates/ava-db/src/models/hq.rs`
- `crates/ava-db/src/migrations/003_hq.sql`
- `crates/ava-hq/src/memory.rs`
- `src/stores/hq.ts`
- `src/types/hq.ts`

## Current frontend integration

Key frontend paths:

```text
src/stores/hq.ts
src/types/hq.ts
src/services/rust-bridge.ts
src/components/hq/
src/components/settings/tabs/HqTab.tsx
```

The most important frontend rule is:

- Director Chat should reuse the normal AVA chat presentation wherever possible
- prefer shared chat primitives over HQ-only clones so future chat-view polish lands in HQ too
- Plan Review should also prefer existing plan-view primitives with HQ-specific extensions instead of a second standalone plan-review system
- shared chat shells should be extracted when needed; HQ should not keep a parallel input/composer path if the main chat can be refactored into a reusable shell

Current implementation status: HQ Director Chat now mounts the same `MessageInput` and `MessageList`
components as normal chat via adapter overrides, rather than only sharing lower-level shells. The
remaining differences should live in adapter data and HQ-specific card content, not in separate chat
component trees.

Do not fork a separate chat design language for HQ unless there is a real behavioral need.

The onboarding path should call the same reusable memory bootstrap used by CLI/headless mode,
instead of inventing a separate desktop-only setup path.

Browser/web mode now also has first-class HQ routes for creating epics and loading/approving/
rejecting plans, so Playwright and headless browser testing can exercise real HQ flows.

Current browser-mode status: the web path now reliably supports create-initiative -> plan creation
-> Plan Review -> approve/revise loops. The store prefers the newest relevant epic/plan when HQ
enters review mode so browser users do not land in a false "No plan is ready" state after a plan
was already created.

## Board of Directors

The Board is an optional escalation path for complex work.

- use it for architecture-heavy or risky tasks
- multiple top models give opinions
- the Director synthesizes the result
- the user still approves the plan boundary

Current implementation status:

- HQ now persists board review data onto plans when the generated plan is broad/complex enough to
  trigger a board consultation
- Plan Review surfaces the real board consensus, vote summary, and member opinions when present
- if no board review exists, Plan Review stays on the simple Director-only path

Disagreements should not become open-ended chaos. HQ should surface a concise synthesis,
highlight disagreement areas, and move forward with a clear recommended path.

## Implementation notes

When working on HQ, prefer these principles:

- preserve one clear primary control surface: the Director
- keep memory structured and inspectable
- avoid raw-chat-only persistence
- keep plan review as an explicit trust boundary
- keep the UI simpler, calmer, and more product-native than the old many-screen concept
- prefer durable data models over mock/demo-only UI assumptions
- reject invalid plans before execution when a supposedly parallel phase contains tasks that depend
  on each other
- keep HQ testable outside the desktop shell: CLI/headless bootstrap, browser routes, and Playwright
  flows should remain supported

## Open questions still in play

These remain active design/implementation questions:

- exact first-run onboarding flow for HQ memory creation
- how much worker-level pause/cancel control to expose in UI
- how Board disagreements should be rendered and resolved
- how mid-flight replanning should work without breaking trust
- how HQ and normal AVA chat should share context safely
- which parts of `.ava/HQ/` should be exportable/promotable into `docs/`

## Reference note

Paperclip remains a useful reference for governance/orchestration ideas, but HQ should stay
software-engineering-native: persistent Director conversation, plan review, git-aware execution,
QA checkpoints, and memory-backed project understanding.
