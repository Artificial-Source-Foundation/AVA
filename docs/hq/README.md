# AVA HQ -- Multi-Agent Orchestration

> **Crate**: `ava-hq` (formerly `ava-praxis`)
> **Platform**: Desktop only (Tauri IPC)
> **Constraint**: One HQ instance per project

## What is HQ?

HQ is AVA's multi-agent orchestration system. It lets a single user delegate complex, multi-file engineering work to a team of AI agents organized under a **Director of Engineering**. The Director analyzes goals, scouts the codebase, builds structured plans, decomposes work into issues, assigns agents, and monitors execution -- all through natural language.

HQ runs as a background process alongside AVA's normal single-chat mode. While HQ agents work on an epic, the user can continue chatting with AVA in the main conversation. HQ surfaces progress, questions, and review requests through the Director Chat interface.

The design is inspired by Paperclip AI's team-based orchestration but adapted for a single-user coding tool rather than a multi-user collaboration platform.

## Hierarchy

```
Director of Engineering (Opus tier -- strongest available model)
|
+-- CTO (strong model, e.g. Sonnet/Opus)
|   +-- Engineer: Pedro (Jr. Backend)
|   +-- Engineer: Sofia (Jr. Backend)
|   +-- Engineer: Luna (Jr. Frontend)
|   ...
|
+-- QA Lead (strong model)
|   +-- Engineer: Kai (Jr. QA)
|   +-- Engineer: Mira (Jr. QA)
|   ...
```

### Roles

| Role | Model Tier | Examples | Icon / Color |
|------|-----------|----------|--------------|
| **Director** | Strongest available | Opus, GPT-5.4 | Crown, amber |
| **CTO / Leads** | Strong | Sonnet, Opus (complex) | Professional titles -- "Backend Lead", "QA Lead" |
| **Engineers / Workers** | Mid-tier | Sonnet, GPT-5.3 | Fun first names -- Pedro, Sofia, Luna, Kai, Mira, Rio, Ash, Nico, Ivy, Juno, Zara, Leo |
| **Scouts** | Cheapest | Haiku, Flash, Mercury | Ephemeral, unnamed |
| **Board** | Top per provider | Best from each configured provider | Named by model -- "Opus (Board)", "Gemini (Board)" |

The Director is LLM-powered, not a code-driven router. It analyzes task complexity and adapts its approach through three intelligence levels:

| Level | Complexity | Behavior |
|-------|-----------|----------|
| **1** | Simple (one-file fix) | Spawns one worker + one QA worker. No leads needed. |
| **2** | Medium (multi-file, clear scope) | Sends scouts, creates plan, user reviews. Spawns 2-3 leads with workers. |
| **3** | Complex (major refactor, architecture) | Scouts + Board of Directors (3 SOTA models vote on approach). User approves plan. |

## Key Concepts

### Epics

An epic is a large unit of work -- a feature, refactor, or multi-step goal. The user describes an epic to the Director, who breaks it down into a structured plan. Each project can have multiple epics, tracked on a kanban board.

### Issues

Epics decompose into issues -- discrete, assignable tasks. Each issue has a title, description, acceptance criteria, assigned agent, and status. Issues flow through a kanban: Backlog, In Progress, In Review, Done.

### Plans

Before execution begins, the Director produces a plan: a structured breakdown of the epic into phases with dependencies. Plans are Plannotator-style -- shown as interactive messages in chat with clickable, reorderable, commentable steps. Plans are saved to `.ava/plans/`.

### Phases (Parallel / Sequential)

A plan consists of phases. Phases execute **sequentially** when there are dependencies between them, or **in parallel** when tasks touch independent files/modules. The Director determines the execution order. Each phase may have one or more issues assigned to different agents.

### Review Steps

QA is built into every level of execution:

- **Worker self-check**: each worker verifies its changes compile and pass tests.
- **Lead QA workers**: each lead has dedicated QA workers that review changes within their domain.
- **QA Lead**: cross-lead merge verification -- reviews the integrated result after all leads complete.
- **Board review** (Level 3): for complex decisions, the Board of Directors provides multi-model consensus before execution.

## Screens

HQ has 11 screens designed in `design/ava-ui.pen`:

| Screen | Purpose |
|--------|---------|
| **Dashboard** | Overview of all active epics, agent utilization, progress metrics |
| **Org Chart** | Visual hierarchy of all active agents, their roles, status, and model assignments |
| **Director Chat** | The command center -- primary interface for talking to the Director. All other screens are live views of state managed here. |
| **Plan Review** | Interactive plan viewer where the user reviews, comments on, and approves the Director's proposed plan before execution begins |
| **Epics** | List of all epics with status, progress bars, and quick actions |
| **Epic Detail** | Deep view of a single epic: its plan, issues, timeline, and agent assignments |
| **Issues / Kanban** | Kanban board showing all issues across epics, filterable by status, assignee, and domain |
| **Issue Detail** | Single issue view: description, acceptance criteria, assigned agent, activity log, file changes |
| **Agent Detail** | View of a single agent: current task, token usage, tool calls, conversation history |
| **Onboarding** | First-time HQ setup: model selection for each role tier, budget configuration |
| **New Epic Modal** | Modal dialog for creating a new epic with goal description and optional constraints |

### Director Chat is Primary

The Director Chat is the main interface. The user talks to the Director in natural language. The Director responds with status updates, questions, plan proposals, and completion reports. All other screens (Dashboard, Kanban, Agent Detail, etc.) are live views into state that the Director manages -- they provide visibility but the Director Chat is where decisions happen.

## The Flow

1. **User describes goal** -- types a message to the Director in Director Chat (e.g., "Refactor the authentication system to support OAuth2 PKCE").
2. **Director assesses complexity** -- determines intelligence level (1/2/3) based on scope.
3. **Scouts explore codebase** -- lightweight agents (cheap models) read relevant files and produce summaries for the Director.
4. **Director creates epic + plan** -- structured breakdown with phases, dependencies, and estimated effort.
5. **User reviews plan** -- interactive Plan Review screen. User can comment, reorder steps, adjust scope, or approve.
6. **Director decomposes into issues** -- each plan step becomes one or more issues on the kanban.
7. **Director assigns agents** -- spawns leads and workers, assigns issues based on domain and complexity.
8. **Agents execute** -- workers edit files, run tests, use tools. Leads coordinate their domain. Director monitors everything.
9. **QA reviews** -- QA workers check individual changes. QA Lead reviews the merged result.
10. **Director reports completion** -- summarizes what was done, files changed, tests passing, and any remaining items.

Throughout this process, HQ runs in the background. The user can switch to AVA's single-chat mode at any time and continue normal work. HQ surfaces important events (questions, review requests, completion) through notifications.

## Background Execution

HQ is designed to run alongside normal AVA usage:

- HQ agents work in **separate git worktrees** -- each lead gets its own worktree, and workers share their lead's worktree. This prevents conflicts with the user's working directory.
- A **Merge Worker** integrates lead worktrees when phases complete, resolving conflicts.
- The user can pause, cancel, or steer HQ at any time through the Director Chat.
- Artifacts (intermediate outputs, reports, plans) are saved to `.ava/hq/{session-id}/{lead-name}/`.

## Crate: ava-hq

The `ava-hq` crate (under `crates/ava-hq/`) contains all orchestration logic. Types are prefixed with `Hq*` to avoid collisions with other crates.

**Key modules**: `director`, `lead`, `worker`, `routing`, `plan`, `prompts`, `scout`, `board`, `events`, `workflow`, `acp`, `acp_handler`, `acp_transport`, `artifact`, `artifact_store`, `conflict`, `decomposition`, `mailbox`, `review`, `spec`, `spec_workflow`, `synthesis`.

**Key types**: `Director`, `HqEvent`, `Lead`, `Worker`, `AcpServer`, `AcpClient`, `ArtifactStore`, `Mailbox`, `SpecWorkflow`, `ConflictResolver`.

**Dependencies**: ava-types, ava-agent, ava-llm, ava-tools, ava-context, ava-platform; optional ava-cli-providers.

## Desktop Commands

HQ is wired to the desktop frontend via Tauri IPC commands in `src-tauri/src/commands/hq_commands.rs`:

| Command | Purpose |
|---------|---------|
| `start_hq` | Begin HQ orchestration for the current project |
| `get_hq_status` | Retrieve current HQ state (agents, epics, issues, progress) |
| `cancel_hq` | Stop all HQ agents and clean up worktrees |
| `steer_lead` | Send a message to a specific lead to redirect their work |

The desktop HQ bridge also exposes CRUD-style commands for epics, issues, plans, comments, agents, dashboard metrics, activity feed, director chat, and HQ settings so the SolidJS HQ screens can render live SQLite-backed state instead of mock fixtures.

Events flow from the Rust backend to the frontend via `emit_hq_event`, which forwards all `HqEvent` variants through the `agent-event` IPC channel. Frontend event handlers in `rust-agent-events.ts` and `useAgent.ts` map HQ events to the team store.

## Error Handling (Tiered)

1. **Tool error** -- Worker retries automatically (up to 2x).
2. **LLM error** -- Lead switches to fallback model.
3. **Logic error** -- Lead reviews output, spawns fix worker.
4. **Worker budget exhausted** -- Lead asks Director, Director asks user.
5. **Catastrophic** -- Director asks user for guidance.

## Board of Directors

For Level 3 (complex) tasks, the Director convenes a Board of Directors:

- Three different SOTA models (e.g., Opus, Gemini, GPT-5.4), each with a distinct analytical personality.
- One round of opinions based on scout reports.
- Models vote on the approach.
- Director synthesizes the consensus into the final plan.

The Board is opt-in and only activated for complex architectural decisions.

## Configuration

HQ model assignments and behavior are configured in `agents.toml` (per-project) or through the HQ Onboarding screen:

- Director model (strongest available)
- Lead models (strong tier)
- Worker models (mid tier)
- Scout models (cheapest tier)
- Board models (top per provider, opt-in)
- Budget limits per lead, per worker, and total

## Solo Hidden Delegation

Outside HQ team mode, the main agent can still delegate quietly through the `task` tool:

- Small single-file work stays on the main thread only.
- Broader tasks can unlock a bounded helper budget (typically 1-2 hidden subagents, 3 only on explicit delegation requests).
- `scout`, `explore`, `plan`, and `review` helpers run in enforced read-only specialist mode; `worker`, `build`, and `task` keep full editing access.

This is distinct from HQ -- solo delegation is lightweight and invisible, while HQ is a full orchestration system with its own UI and lifecycle.

## Frontend Implementation

The SolidJS frontend lives at `src/components/hq/` and is now backed by real desktop/web runtime state instead of mock fixtures:

```
src/types/hq.ts                          — HQ DTOs for epics, issues, agents, plans, activity, settings
src/stores/hq.ts                         — Navigation + live HQ state store, Director streaming state, event ingestion
src/components/hq/
  HqShell.tsx                            — Shell container (sidebar + content + overlays)
  HqSidebar.tsx                          — HQ nav (6 items) with Back to Chat escape hatch and agent status footer
  HqContent.tsx                          — Lazy-loaded page router (9 screens)
  HqOnboarding.tsx                       — 3-step setup wizard (Director model, team, review)
  HqNewEpicModal.tsx                     — Create epic dialog with non-blurred overlay surface
  index.ts                               — Barrel export
  screens/
    HqDashboard.tsx                      — Metric cards + active agents + activity feed
    HqDirectorChat.tsx                   — Director chat using the shared AVA chat renderer/composer, including streaming/thinking/tool surfaces
    HqOrgChart.tsx                       — SVG tree (Director → Leads → Workers)
    HqPlanReview.tsx                     — Plannotator-style with annotation toolbar
    HqEpics.tsx                          — Hierarchical epic tree with progress bars
    HqEpicDetail.tsx                     — Epic progress cards + issue list
    HqIssues.tsx                         — 4-column Kanban with drag-and-drop
    HqIssueDetail.tsx                    — Comments thread + properties panel
    HqAgentDetail.tsx                    — Live transcript + steering input
src/components/settings/tabs/HqTab.tsx   — HQ-only settings (Director model, tone, auto-review, lead/worker routing)
```

Integration points:
- `src/components/layout/AppShell.tsx` — collapses the normal chat-session sidebar while `hqMode()` is true
- `src/components/layout/MainArea.tsx` — renders `HqShell` when `hqMode()` is true
- `src/components/layout/SidebarPanel.tsx` — Building2 icon toggles HQ mode
- `src/components/settings/settings-modal-config.ts` — HQ settings registered as their own dedicated settings category (separate from general Agents)

Runtime/data flow:
- Desktop HQ uses Tauri commands in `src-tauri/src/commands/hq_commands.rs` for epics, issues, plans, agents, activity, Director chat, and HQ settings.
- Browser HQ exposes matching `/api/hq/*` endpoints from `crates/ava-tui/src/web/api_hq.rs` so web mode and Playwright can exercise the same shell.
- HQ Director chat now runs through the real assistant path; stale fake kickoff/preview messages are purged on read, the Director agent is auto-seeded so Org Chart is never blank on first open, and Director chat tasks emit streaming token/thinking/tool events so the HQ chat can reuse normal AVA chat rendering.

## Paperclip Reference

The design is inspired by [Paperclip AI](https://github.com/paperclipai/paperclip). Reference documentation:
- `docs/reference-code/paperclip/` — full repo clone
- `docs/reference-code/paperclip-frontend-reference.md` — UI architecture (39 pages, 80 components)
- `docs/reference-code/paperclip-backend-reference.md` — server architecture (59 tables, 180 endpoints)
- `docs/reference-code/paperclip-adapters-reference.md` — agent adapters (10 types)
- `docs/reference-code/paperclip-skills-plugins-reference.md` — skills, plugins, CLI, shared types
