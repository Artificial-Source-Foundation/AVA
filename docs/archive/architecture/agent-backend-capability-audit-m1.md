---
title: "Agent Backend Capability Audit (Milestone 1)"
description: "Concise inventory of AVA's current coding-agent backend capability surface."
order: 5
updated: "2026-04-14"
---

# Agent Backend Capability Audit (Milestone 1)

> Historical artifact note: this is a Milestone 1 snapshot (as-audited state), preserved for traceability. It is not the current canonical contract or active gap tracker.

Scope at audit time: in-repo backend state only (no external comparison yet).

For external comparison and correction planning, see `docs/architecture/agent-backend-capability-comparison-m2.md`.

## 1) Skills

- Runtime skill discovery and prompt injection live in `crates/ava-agent/src/instructions.rs`.
  - Skill roots: `.claude/skills`, `.agents/skills`, `.ava/skills` (`SKILL_DIRS`).
  - Discovery APIs: `discover_runtime_skills()` / `discover_runtime_skills_from_root(...)`.
  - Trust gating: project-local skills load only when the project is trusted.
- Slash visibility path for runtime skills is wired in:
  - TUI slash command handling: `crates/ava-tui/src/app/commands.rs` (`/skills`)
  - Headless slash handling: `crates/ava-tui/src/headless/single.rs`

## 2) Commands / slash commands

- Built-in slash command routing is centralized in `crates/ava-tui/src/app/commands.rs`.
- Hook-specific and custom-command support is in `crates/ava-tui/src/app/command_support.rs`.
- Repo/global custom slash command registry is in `crates/ava-tui/src/state/custom_commands.rs`.
  - Sources: `~/.ava/commands/*.toml` and `.ava/commands/*.toml` (project-local trust-gated).
- CLI command/subcommand registry is defined in `crates/ava-tui/src/config/cli.rs`.

## 3) Subagents

- Subagent tool surface is implemented as `subagent` in `crates/ava-tools/src/core/task.rs`.
- Runtime spawning is in `crates/ava-agent/src/stack/stack_run.rs` (`AgentTaskSpawner`).
  - Includes depth limits, spawn budgets, foreground/background execution, and session linkage.
- Agent-type config and defaults are in `crates/ava-config/src/agents.rs` (`AgentsConfig`, `default_agents`).
- Run-time registration of the tool happens in `crates/ava-agent/src/stack/stack_run.rs` (via `register_task_tool(...)` when delegation is enabled).

## 4) Tool execution pipeline

- Core registry, middleware, tiers, and execution lifecycle: `crates/ava-tools/src/registry.rs`.
  - `ToolRegistry::execute(...)` does argument backfill, middleware checks, and retry handling for transient failures.
- Base registry construction with permission middleware + plugin wiring:
  `crates/ava-agent/src/stack/stack_tools.rs` (`build_tool_registry_with_plugins(...)`).
- Agent-loop tool orchestration and validation:
  `crates/ava-agent/src/agent_loop/tool_execution.rs`.
  - JSON-schema arg validation, tool-call repair, truncation, concurrency limits for read-only tools.
- Interactive desktop tool-surface introspection path is wired to the same runtime filtering logic used by `run()` (desktop parity):
  - Core runtime surface resolver: `crates/ava-agent/src/stack/mod.rs` (`effective_tools_for_interactive_run`)
  - Desktop command exposure: `src-tauri/src/commands/tool_commands.rs` (`list_agent_tools`)
  - Frontend bridge call site: `src/services/rust-bridge.ts` (`rustBackend.listAgentTools`)
  - Desktop Tool List dialog context source: `src/components/dialogs/ToolListDialog.tsx`

## 5) Write/edit capabilities

- Write tool implementation: `crates/ava-tools/src/core/write.rs`.
  - Workspace path enforcement, parent-dir creation, backup-before-write, and result diff summary.
- Edit tool implementation: `crates/ava-tools/src/core/edit.rs`.
  - Matching/edit engine integration, hashline-aware targeting, stale-read warnings, ghost snapshots, backup-before-edit.
- Plan-mode write restrictions enforced in agent runtime:
  `crates/ava-agent/src/agent_loop/tool_execution.rs` (`check_plan_mode_tool`, `is_plan_path`).

## 6) Planning / todo workflow

- Todo tools: `crates/ava-tools/src/core/todo.rs` (`todo_write`, `todo_read` with full-replace semantics).
- Plan tool and plan approval bridge: `crates/ava-tools/src/core/plan.rs` (`PlanBridge`, `PlanTool`).
- Agent stack wires todo/question/plan tools in:
  - `crates/ava-agent/src/stack/mod.rs`
  - `crates/ava-agent/src/stack/stack_run.rs`
- TUI plan approval state/UI handoff:
  - `crates/ava-tui/src/state/plan_approval.rs`
  - `crates/ava-tui/src/app/event_dispatch.rs` (`AppEvent::PlanProposal`)

## 7) Permissions / approval

- Permission decision logic and risk model:
  - `crates/ava-permissions/src/inspector.rs` (`DefaultInspector`)
  - `crates/ava-permissions/src/policy.rs` (`PermissionPolicy::{permissive,standard,strict}`)
- Tool-call approval middleware and bridge:
  `crates/ava-tools/src/permission_middleware.rs`.
- Live context setup and merged rules assembly:
  `crates/ava-agent/src/stack/mod.rs` (persistent rules + glob rules + config path rules).
- TUI approval queue/state and modal activation:
  - `crates/ava-tui/src/state/permission.rs`
  - `crates/ava-tui/src/app/event_dispatch.rs` (`AppEvent::ToolApproval`)
- Headless default behavior currently auto-approves received approval requests:
  `crates/ava-tui/src/headless/mod.rs` (`spawn_auto_approve_requests`).

## 8) Review flow

- Review engine/API: `crates/ava-review/src/lib.rs`.
  - Diff collection, prompt builder, review-runner integration, parsing/formatting.
- TUI slash review trigger: `crates/ava-tui/src/app/commands.rs` (`/review`).
- TUI async review execution: `crates/ava-tui/src/app/spawners.rs` (`spawn_review_pass`).
- CLI review command contract: `crates/ava-tui/src/config/cli.rs` (`Command::Review`, `ReviewArgs`).
- CLI review entrypoint logic: `crates/ava-tui/src/review.rs`.

## 9) Key backend/config registries

- Agent runtime composition root: `crates/ava-agent/src/stack/mod.rs`.
- Tool registration surfaces:
  - Default/additional tool registration: `crates/ava-tools/src/core/mod.rs`
  - Run-scoped registry build + middleware: `crates/ava-agent/src/stack/stack_tools.rs`
- Global app config schema/manager: `crates/ava-config/src/lib.rs` (`Config`, `PermissionsConfig`, feature toggles).
- Subagent config registry: `crates/ava-config/src/agents.rs` (`AgentsConfig`, `AgentOverride`, predefined defaults).
- Instruction/skill loading registry: `crates/ava-agent/src/instructions.rs`.

## Observed inconsistencies / gaps (historical Milestone 1 findings)

1. **Subagent naming mismatch in some TUI paths**  
   - Tool name is `subagent` (`crates/ava-tools/src/core/task.rs`), but some UI rendering paths still check for `task`-only behavior in `crates/ava-tui/src/widgets/message.rs`.
2. **Subagent tool-surface docs/comments appear stale**  
   - `task.rs` comments/descriptions mention `apply_patch` availability for subagents, while subagent runtime registry is built from `register_core_tools(...)` in `stack_run.rs` and does not explicitly register `apply_patch` there.
3. **Plan persistence wording drift**  
   - `PlanTool` comments mention JSON persistence in one location, while implementation persists Markdown files under `.ava/plans/` (`crates/ava-tools/src/core/plan.rs`).
