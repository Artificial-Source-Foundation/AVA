---
title: "Entrypoints"
description: "Map of AVA's CLI, desktop, web, and shared runtime entrypoints."
order: 3
updated: "2026-04-21"
---

# Entrypoints

This page maps the main runtime entrypoints so contributors and AI agents can quickly find where to wire new behavior.

## Main Runtime Paths

### CLI Binary

`crates/ava-tui/src/main.rs` is the top-level runtime router.

It decides between:

1. TUI mode
2. Headless mode
3. ACP server mode
4. Web server mode via `ava serve`
5. Auth, review, plugin, and update subcommands
6. Benchmark and harness modes behind feature flags

### Shared Agent Runtime

`crates/ava-agent-orchestration/src/stack/mod.rs` now owns `AgentStack` composition used across product surfaces.

It owns:

1. Model router setup
2. Tool registry assembly
3. MCP runtime wiring
4. Session and memory systems
5. Plugin manager loading
6. Trust-gated project-local config loading
7. Codebase indexing startup behavior

The stack keeps MCP lifecycle/state behind the adjacent `crates/ava-agent-orchestration/src/stack/stack_mcp.rs` service module, while `AgentStack` remains the shared surface-level composition root used by CLI/TUI, desktop, and web.

Boundary note: internal workspace callers that need stack/subagent orchestration now depend on `ava-agent-orchestration` directly (`ava_agent_orchestration::stack::*`), while `ava-agent` remains runtime-core ownership.

### Shared Control-Plane Contract Seam

`crates/ava-control-plane/src/` is the canonical cross-surface command/event/lifecycle contract seam for shared/pure backend contracts.

`crates/ava-agent/src/control_plane/` remains the backend integration shim for runtime-owned helpers only (no pure contract re-export modules).

Ownership boundaries:

1. `ava-control-plane::commands` -- canonical command families, completion semantics, terminal signals, and correlation requirements.
2. `ava-control-plane::events` -- canonical event kinds, required fields, and required backend event-kind inventory.
3. `ava-control-plane::interactive` -- approval/question/plan request lifecycle, request IDs, and timeout policy.
4. `ava-control-plane::sessions` -- session selection precedence and retry/edit/regenerate replay payload rules.
5. `ava-control-plane::queue` and `ava-control-plane::orchestration` -- queue-clear semantics plus deferred follow-up/post-complete orchestration helpers.
6. `ava-agent::control_plane::events` + `ava-agent::control_plane::sessions` -- backend-only projection/reconstruction helpers.

Primary adopters:

1. `src-tauri/` desktop bridge + command handlers
   - pure command/event/interactive/session/queue/orchestration contract imports should come from `ava-control-plane`; keep `ava-agent::control_plane::*` only for backend-only helpers (for example `run_context_from_session`, `backend_event_requires_interactive_projection`)
2. `crates/ava-web/src/` web routes and websocket projections (wired from `ava-tui` `Command::Serve`) — pure control-plane contracts should import from `ava-control-plane` directly; backend-only helpers still come from `ava-agent::control_plane::*`.
3. `crates/ava-tui/src/app/` interactive TUI handlers — prefer direct `ava-control-plane` imports for pure command/event/interactive/session/queue/orchestration contracts.
4. `crates/ava-tui/src/headless/` headless command and event flows — same direct `ava-control-plane` guidance for pure contracts.

### Desktop App

The desktop app starts in `src-tauri/src/lib.rs`.

Key pieces:

1. Tauri plugin registration
2. `AppState` initialization for legacy command paths
3. `DesktopBridge::init()` for the real agent stack
4. IPC command registration through `tauri::generate_handler![]`

Desktop command modules are grouped in `src-tauri/src/commands/mod.rs`.

### Web Server

The browser-facing server lives in `crates/ava-web/src/lib.rs`.

It exposes:

1. REST endpoints under `/api/`
2. Agent event streaming via `/ws`
3. Session CRUD, MCP, plugins, models, permissions, and plan endpoints
4. Lazy MCP initialization after the server is already listening

## Trust-Gated Local Config Surfaces

The shared runtime only loads repo-local config when the project is trusted.

This applies to:

1. `.ava/mcp.json`
2. `.ava/tools/`
3. `.ava/commands/`
4. `.ava/hooks/`
5. `.ava/subagents.toml`
6. `.ava/skills/`
7. `.ava/rules/`

Use `ava --trust` to approve the current project.

## Where To Implement Common Changes

1. New core tool: `crates/ava-tools/src/core/`
2. New provider: `crates/ava-llm/src/providers/`
3. New slash command: `crates/ava-tui/src/app/commands.rs`
4. New custom command behavior: `crates/ava-tui/src/state/custom_commands.rs`
5. New hook event or behavior: `crates/ava-tui/src/hooks/`
6. New web endpoint: `crates/ava-web/src/`
7. New desktop IPC command: `src-tauri/src/commands/`
8. New plugin capability: `crates/ava-plugin/` and `plugins/sdk/`
9. Shared control-plane command/event/session contract changes: `crates/ava-control-plane/src/` (plus backend-only wiring in `crates/ava-agent/src/control_plane/`)

## Related Docs

1. [Crate map](crate-map.md)
2. [Plugin boundary checklist](plugin-boundary.md)
3. [Commands reference](../reference/commands.md)
4. [Extend AVA](../extend/README.md)
5. [Canonical shared-backend contract (Milestone 6)](shared-backend-contract-m6.md)
