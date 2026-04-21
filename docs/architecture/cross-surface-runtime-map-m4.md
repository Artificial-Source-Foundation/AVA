---
title: "Cross-Surface Runtime Map (Milestone 4)"
description: "Concrete map of how CLI/TUI, desktop/Tauri, and web route into AVA's shared backend runtime seam."
order: 7
updated: "2026-04-14"
---

# Cross-Surface Runtime Map (Milestone 4)

Milestone 4 is mapping-only. This artifact traces each surface from entrypoint to transport/bridge to shared runtime, then summarizes per-surface behavior for events, approvals, commands, session loading, and tool introspection. No cross-surface behavior-audit conclusions are included.

## Shared backend seam (explicit)

Shared backend is two connected seams:

1. **Runtime execution seam** in `AgentStack` (`crates/ava-agent/src/stack/`)
   - `AgentStack::new(...)` (`crates/ava-agent/src/stack/mod.rs`)
   - `AgentStack::run(...)` (`crates/ava-agent/src/stack/stack_run.rs`)
   - `AgentStack::effective_tools_for_interactive_run(...)` (`crates/ava-agent/src/stack/mod.rs`)
2. **Interactive control-plane seam** adjacent to the stack
   - approval/question/plan channels returned from `AgentStack::new(...)` (`crates/ava-agent/src/stack/mod.rs`)
   - cancellation, message queue, pending interactive replies, and session handoff owned today in surface adapters (`crates/ava-tui/src/state/agent.rs`, `src-tauri/src/bridge.rs`, `crates/ava-tui/src/web/state.rs`)

All surfaces should treat these as the backend seams and keep adapters thin.

## 1) Surface traces: entrypoint -> bridge/transport -> shared runtime

| Surface | Entrypoint path | Frontend invocation adapter | Bridge / transport path | Shared runtime landing |
|---|---|---|---|
| Interactive TUI | `crates/ava-tui/src/main.rs` → `App::new` / `App::run` (`crates/ava-tui/src/app/mod.rs`) | Terminal UI command/event handling in `crates/ava-tui/src/app/` | Internal Tokio channels/events (`crates/ava-tui/src/event.rs`, `crates/ava-tui/src/app/mod.rs`) | `AgentState::new` / `start` → `AgentStack::new` / `run` (`crates/ava-tui/src/state/agent.rs`) |
| Headless CLI | `crates/ava-tui/src/main.rs` → `run_headless` / `run_single_agent` (`crates/ava-tui/src/headless/mod.rs`, `crates/ava-tui/src/headless/single.rs`) | CLI args and headless request assembly in `crates/ava-tui/src/headless/` | In-process async tasks plus headless-specific approval handling | `AgentStack::new` / `run` called directly from headless path (`crates/ava-tui/src/headless/single.rs`) |
| Desktop/Tauri | `src-tauri/src/lib.rs` (`run`, `invoke_handler`) | Solid frontend invoke/listener layer (`src/services/rust-bridge.ts`, `src/hooks/rust-agent-ipc.ts`) | Tauri IPC + command routing + event conversion (`src-tauri/src/commands/*.rs`, `src-tauri/src/bridge.rs`, `src-tauri/src/events.rs`) | `DesktopBridge` owns `Arc<AgentStack>` (`src-tauri/src/bridge.rs`), handlers invoke `stack.run(...)` (`src-tauri/src/commands/agent_commands.rs`) |
| Web | `crates/ava-tui/src/main.rs` → `Command::Serve` → `crates/ava-tui/src/web/mod.rs::run_server` | Frontend API adapter in `src/lib/api-client.ts` | Axum REST `/api/*` and WebSocket `/ws` (`crates/ava-tui/src/web/mod.rs`, `crates/ava-tui/src/web/ws.rs`) | `WebState::init` owns `Arc<AgentStack>` (`crates/ava-tui/src/web/state.rs`), handlers invoke `stack.run(...)` (`crates/ava-tui/src/web/api_agent.rs`) |

## 2) Per-surface flow summary (events, approvals, commands, sessions, tool introspection)

### Interactive TUI

- **Event flow**
  - `AgentState::start` spawns run tasks and relays to UI events (`crates/ava-tui/src/state/agent.rs`).
  - `App::run` owns the main loop and dispatches `AppEvent::{AgentRunEvent,AgentRunDone,...}` (`crates/ava-tui/src/app/mod.rs`, `crates/ava-tui/src/app/event_dispatch.rs`).
- **Approvals**
  - Interactive mode consumes `question_rx`, `approval_rx`, `plan_rx` and maps to UI events (`crates/ava-tui/src/event.rs`, `crates/ava-tui/src/app/mod.rs`).
  - UI handoff occurs in event dispatch (`crates/ava-tui/src/app/event_dispatch.rs`).
- **Commands**
  - CLI command dispatch in `crates/ava-tui/src/main.rs` and schema in `crates/ava-tui/src/config/cli.rs`.
  - Slash/custom command handling: `crates/ava-tui/src/app/commands.rs`, `crates/ava-tui/src/state/custom_commands.rs`.
- **Session loading**
  - Startup/session bootstrap in `App::new` (`crates/ava-tui/src/app/mod.rs`).
  - Session list/load workers in `crates/ava-tui/src/app/spawners.rs` → `AppEvent::SessionLoaded` (`crates/ava-tui/src/app/event_dispatch.rs`).
- **Tool introspection**
  - `AgentState::list_tools_with_source(...)` then calls `effective_tools_for_interactive_run(...)` (`crates/ava-tui/src/state/agent.rs`, `crates/ava-agent/src/stack/mod.rs`).

### Headless CLI

- **Event flow**
  - Headless path assembles request/session context and runs the backend directly from `run_single_agent` (`crates/ava-tui/src/headless/single.rs`).
- **Approvals**
  - Headless mode uses `spawn_auto_approve_requests` (auto-approve path) (`crates/ava-tui/src/headless/mod.rs`).
- **Commands**
  - CLI command dispatch is still rooted in `crates/ava-tui/src/main.rs` and `crates/ava-tui/src/config/cli.rs`.
- **Session loading**
  - Session continuity is resolved inside the headless request path before `stack.run(...)` (`crates/ava-tui/src/headless/single.rs`).
- **Tool introspection**
  - Headless uses the same backend tool/runtime surface indirectly through the stack, but it does not expose the same user-facing tool-introspection flow as desktop/web.

Headless note: include headless in the next audit only as a scoped non-interactive backend path. It is not a peer target for interactive approval/question/plan UX parity.

### Desktop / Tauri

- **Event flow**
  - Backend run loop events are forwarded through `run_agent_inner` and converted via `from_backend_event` (`src-tauri/src/commands/agent_commands.rs`, `src-tauri/src/events.rs`).
  - Frontend receives through Tauri `agent-event` listeners in `src/hooks/rust-agent-ipc.ts`.
- **Approvals**
  - `approval_request`, `question_request`, `plan_created` payloads are emitted from command handlers (`src-tauri/src/commands/agent_commands.rs`).
  - Front-end responses resolve through `resolve_approval`, `resolve_question`, `resolve_plan` (`src-tauri/src/commands/agent_commands.rs`).
  - Approval/question timeout fallbacks are currently defined here (`src-tauri/src/commands/agent_commands.rs`).
- **Commands**
  - IPC command surface is registered in `src-tauri/src/lib.rs` (`tauri::generate_handler!`).
  - Command callers in desktop UI are in `src/services/rust-bridge.ts`.
- **Session loading**
  - Session fetch/load command in `src-tauri/src/commands/session_commands.rs`.
  - `run_agent_inner` can rehydrate using `requested_session_id` / `last_session_id` (`src-tauri/src/commands/agent_commands.rs`).
- **Tool introspection**
  - `list_agent_tools` in `src-tauri/src/commands/tool_commands.rs` → `effective_tools_for_interactive_run(...)` and consumes context from explicit session/tool payloads.

### Web

- **Event flow**
  - `submit_goal` starts async backend work and pushes event updates into a broadcast sender (`crates/ava-tui/src/web/api_agent.rs`, `crates/ava-tui/src/web/state.rs`).
  - `/ws` consumers receive adapter-mapped payloads from `crates/ava-tui/src/web/api.rs` and `crates/ava-tui/src/web/ws.rs`.
- **Approvals**
  - Interactive prompts are emitted from API handlers (`crates/ava-tui/src/web/api_agent.rs`).
  - User responses are resolved in `crates/ava-tui/src/web/api_interactive.rs`.
- **Commands**
  - API route registration and auth/command routing in `crates/ava-tui/src/web/mod.rs`; frontend API mapping lives in `src/lib/api-client.ts`.
- **Session loading**
  - CRUD + session fetch APIs in `crates/ava-tui/src/web/api_sessions.rs`.
  - `submit_goal` accepts and uses `session_id` (`crates/ava-tui/src/web/api_agent.rs`).
- **Tool introspection**
  - Endpoint `POST /api/tools/agent` in `crates/ava-tui/src/web/api_tools.rs` funnels into `effective_tools_for_interactive_run(...)`.

## 3) Main divergence points that matter for parity work

1. **Transport completion behavior differs**
   - Tauri `submit_goal` invocation is completion-bound on the command call, while web `submit_goal` returns immediately and completion events stream on `/ws` (`src/services/rust-bridge.ts`, `src/hooks/rust-agent-ipc.ts`, `crates/ava-tui/src/web/api_agent.rs`).
2. **Unattended approval mode differs**
   - Headless CLI auto-resolves approvals in some paths (`crates/ava-tui/src/headless/mod.rs`); interactive TUI, desktop, and web run explicit interactive resolution paths.
3. **Watchdog policy diverges by adapter**
   - Desktop command handlers include time-based auto-resolution for approvals/questions; web currently forwards request events without equivalent timeout handling in the same API path (`src-tauri/src/commands/agent_commands.rs`, `crates/ava-tui/src/web/api_agent.rs`).
4. **Session lifecycle and persistence semantics differ by surface**
   - Desktop and web use different session-ID precedence, checkpoint, and continuity logic (`requested_session_id` / `last_session_id` in desktop, request `session_id`/new IDs in web, local app state in interactive TUI, headless request-local continuity) (`src-tauri/src/commands/agent_commands.rs`, `crates/ava-tui/src/web/api_agent.rs`, `crates/ava-tui/src/app/mod.rs`, `crates/ava-tui/src/app/spawners.rs`, `crates/ava-tui/src/headless/single.rs`).
5. **Tool-introspection availability shape differs**
   - Desktop/web have explicit user-facing tool-introspection command routes (`src-tauri/src/commands/tool_commands.rs`, `crates/ava-tui/src/web/api_tools.rs`); TUI exposes this through runtime helper flow (`crates/ava-tui/src/state/agent.rs`).
6. **Control-plane contract drift exists across adapters**
   - Request DTOs, event schemas, and session/runtime contract shapes are not fully unified across TUI, Tauri, web, and frontend adapters (`src-tauri/src/commands/agent_commands.rs`, `src-tauri/src/events.rs`, `crates/ava-tui/src/web/api_agent.rs`, `crates/ava-tui/src/web/api.rs`, `src/lib/api-client.ts`, `src/types/rust-ipc.ts`).
7. **Event payload shaping and delivery semantics remain adapter-local**
   - `src-tauri/src/events.rs` and `crates/ava-tui/src/web/api.rs` map backend event data differently, and web delivery uses broadcast/WebSocket semantics that can diverge from desktop listener behavior.
8. **Queue and cancel semantics remain adapter-local**
   - Mid-stream message queues, cancel handling, and follow-up/steer/post-complete control flow are coordinated differently in desktop and web adapters (`src-tauri/src/commands/agent_commands.rs`, `crates/ava-tui/src/web/api_agent.rs`).

## 4) How to use this artifact

- Treat the two backend seams above as the baseline for the next shared-vs-divergent audit.
- Use the divergence list to classify each difference as one of: shared invariant, intentional adapter-only difference, or drift/bug (historical Milestone 5 classification terms).
- Treat headless as a scoped non-interactive audit input, not a peer target for full approval/question/plan UX parity.
- Include queue/cancel behavior in the next audit under the interactive control-plane seam, not just event-schema comparison.
- Do not read Milestone 4 as a parity conclusion; it is only the wiring map that [Milestone 5 should audit](cross-surface-behavior-audit-m5.md).

---

Milestone 4 output: architecture map artifact only. No behavior-audit findings are included yet.
