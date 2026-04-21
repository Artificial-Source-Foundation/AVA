---
title: "Agent Backend Capability Comparison (Milestone 2)"
description: "Actionable backend comparison matrix for AVA vs OpenCode, PI Code, Claude Code, and Codex."
order: 6
updated: "2026-04-14"
---

# Agent Backend Capability Comparison (Milestone 2)

Scope: backend mechanics and runtime shape only. This is not a model-quality benchmark or a UX ranking.

Baseline for AVA: `docs/architecture/agent-backend-capability-audit-m1.md`.

External evidence basis for this snapshot:

- OpenCode: `https://opencode.ai/docs/` plus tools, agents, commands, permissions, server, and GitHub docs, checked 2026-04-14.
- PI Code: `https://pi.dev`, `@mariozechner/pi-coding-agent` docs and examples, checked 2026-04-14.
- Claude Code: Anthropic Claude Code docs for commands, skills, subagents, permissions, and review, checked 2026-04-14.
- Codex: OpenAI Codex CLI docs for slash commands, skills, subagents, and approvals/security, checked 2026-04-14.

Interpretation rule: external columns summarize the documented behavior most relevant to AVA backend corrections, not an exhaustive product inventory.

Owner rule: canonical owner = backend subsystem that should own the invariant long-term. Dependent adapters / surfaces = callers, bridges, UI layers, or delivery surfaces that should consume that invariant.

## Workflow Capabilities

| Capability area | AVA current state | Primary external reference | Documented reference behavior | Canonical owner | Dependent adapters / surfaces | AVA follow-up / gap note |
|---|---|---|---|---|---|---|
| Tools and execution pipeline | Mature registry/middleware pipeline, run-scoped filtering, schema validation, retries, and permission middleware | OpenCode | Broad coding tool loop, explicit tool/config surfaces, and stronger migration compatibility around tool discovery and extension loading | `crates/ava-tools/`, `crates/ava-agent/src/agent_loop/` | `crates/ava-agent/src/stack/`, TUI/desktop/web callers | AVA has the basics. Main follow-up is not more core tools first; it is keeping runtime introspection, policy, and cross-surface behavior aligned as new tools/extensions land. |
| Commands / slash commands | Built-ins are hardcoded in TUI; custom commands come from a separate TOML loader | Codex | Clear command control plane and stronger operator-visible runtime controls | Intended shared command/control-plane registry under `crates/ava-agent/` or adjacent shared runtime layer | `crates/ava-tui/src/app/commands.rs`, `crates/ava-tui/src/state/custom_commands.rs`, desktop/web command consumers | AVA gap is command ownership and registry shape: built-in and custom commands are still split instead of sharing one backend-first model. |
| Skills / instruction layering | Good `SKILL.md` discovery, trust gating, prompt injection, and compatibility paths | Claude Code | Explicit invocation semantics, tool restrictions, and tighter skill contract behavior | `crates/ava-agent/src/instructions.rs` | TUI `/skills`, desktop prompt assembly, project/global skill roots | AVA gap is not basic skill loading. It is richer skill contract semantics such as allowed-tool policy, invocation metadata, and clearer backend ownership. |
| Subagents / delegation | Explicit `subagent` tool, config defaults, budgets/depth, and run-scoped enablement | Claude Code | Delegation behavior is treated as a first-class control-plane workflow with clearer policy semantics | `crates/ava-agent/src/routing.rs`, `crates/ava-agent/src/stack/mod.rs`, `crates/ava-agent/src/stack/stack_run.rs` | `crates/ava-tools/src/core/task.rs`, `crates/ava-config/src/agents.rs`, TUI/desktop introspection surfaces | AVA gap is reliability and transparency of delegation behavior across surfaces, not the existence of subagents. Milestone 1 already exposed parity drift in desktop introspection. |
| Write / edit primitives | Strong write/edit tools with backups, stale-read warnings, and targeted edit behavior | Codex | Mutation is paired with disciplined verify/review loops and strong operator feedback | `crates/ava-tools/src/core/write.rs`, `crates/ava-tools/src/core/edit.rs` | Agent loop execution and review flows | AVA has solid primitives. Follow-up should focus on edit/verify discipline and grounding reliability rather than adding another mutation primitive. |
| Planning / todo workflow | First-class `todo_*` and `plan` tools plus approval bridge | OpenCode | Explicit plan/task workflow with stronger operator-visible task tracking | `crates/ava-tools/src/core/todo.rs`, `crates/ava-tools/src/core/plan.rs` | `crates/ava-tui/src/state/plan_approval.rs`, desktop bridge surfaces | AVA gap is cross-surface consistency and policy around planning modes. Core planning exists already. |
| Permissions / approvals | Strong policy tiers, inspector risk model, approval queue/state, and middleware bridge | Claude Code | Strong per-agent/per-tool approval semantics with clearer policy composition | `crates/ava-permissions/`, `crates/ava-tools/src/permission_middleware.rs` | `crates/ava-agent/src/stack/mod.rs`, TUI/desktop/headless policy consumers | AVA gap is making policy semantics clearer and more uniform across desktop, TUI, headless, and future plugin surfaces. |
| Review flow | Dedicated review crate and entrypoints exist | Claude Code | Review is a real workflow, not just a prompt pattern | `crates/ava-review/` | `crates/ava-tui/src/review.rs`, slash/CLI entrypoints | AVA has the subsystem. Gap is making review fit more naturally into the broader coding loop and keeping it consistent across surfaces and automation paths. |

## Architecture Constraints

| Constraint area | AVA current state | Primary external reference | Documented reference behavior | Canonical owner | Dependent adapters / surfaces | AVA follow-up / gap note |
|---|---|---|---|---|---|---|
| Ownership model / extension boundary | Mixed: core tools and flows are strong, but active plugin-boundary work is still in progress | PI Code | Advanced workflows stay extension-owned rather than automatically becoming core surface area | `crates/ava-plugin/`, `docs/architecture/plugin-boundary.md` | `crates/ava-agent/src/stack/`, tool/command delivery surfaces | This is a major AVA-specific dimension. Backend corrections should prefer clearer core-vs-plugin boundaries over simply expanding built-ins. |
| Surface parity / shared runtime reuse | Shared `AgentStack` exists across CLI/TUI/Desktop/Web, but behavior can still drift per surface | Codex | Runtime policy is more consistently discoverable and controllable across interfaces | `crates/ava-agent/` | `crates/ava-tui/`, `src-tauri/src/`, `src/services/rust-bridge.ts` | This is a major AVA gap. Milestone 1 found real desktop-vs-runtime drift; future correction work should treat parity bugs as first-class backend issues. |
| Contract / state coherence | Shared runtime exists, but some DTOs, session views, and surface-specific contract paths still drift | Codex | Clearer control-plane contract for agents, approvals, and runtime status across interfaces | `crates/ava-agent/` | `src-tauri/src/commands/`, `src/types/rust-ipc.ts`, `src/services/rust-bridge.ts` | AVA gap is reducing duplicated state/contract logic between TUI, desktop bridge, and UI adapters. |
| Headless / unattended semantics | Headless flow exists, but headless currently auto-approves approval requests instead of matching the interactive approval model | OpenCode | Unattended execution modes and approval behavior are more explicit as part of the runtime policy model | `crates/ava-permissions/`, `crates/ava-tools/src/permission_middleware.rs`, shared runtime policy in `crates/ava-agent/` | `crates/ava-tui/src/headless/`, desktop/TUI approval consumers | AVA gap is making non-interactive behavior easier to reason about and closer to the same backend policy model as interactive runs. The concrete correction target from M1 is the current headless auto-approve behavior. |

## Strongest Reference Systems

For targeted backend correction work, the strongest practical references are:

1. **Claude Code**
   Best overall reference for how skills, subagents, permissions, review, and planning semantics compose into one coherent coding-agent backend.

2. **Codex**
   Best reference for explicit runtime control-plane design: approvals, sandboxing, config layering, agent definitions, and operator-visible state.

3. **OpenCode**
   Best reference for broad command/tool/server extensibility and for migration-friendly compatibility with existing agent conventions.

`PI Code` is still useful as a secondary architectural reference because it is explicit about keeping advanced workflows extension-owned instead of core-owned.

## Source Notes

- OpenCode source pages used for this snapshot: tools, agents, commands, permissions, server, and GitHub docs under `https://opencode.ai/docs/`.
- PI Code source pages used for this snapshot: `pi.dev`, package README, and example extensions for subagents, plan mode, and permission gates.
- Claude Code source pages used for this snapshot: commands, skills, subagents, permissions, and code review docs under `https://docs.anthropic.com/en/docs/claude-code/`.
- Codex source pages used for this snapshot: CLI slash commands, skills, subagents, and approvals/security docs under `https://developers.openai.com/codex/`.

## Correction Intake Map

Current planning intake:

- `docs/project/backlog.md`

Workstream mapping from this matrix:

| Workstream | Matrix rows | Canonical owner seam | Adapter surfaces | Execution queue |
|---|---|---|---|---|
| Runtime parity | subagents/delegation, planning/todo, surface parity, headless semantics | `crates/ava-agent/` shared runtime APIs plus shared approval/policy layers | TUI, desktop bridge, web/headless adapters | `docs/project/backlog.md` |
| Control-plane contracts | commands, permissions, contract/state coherence | intended shared command/control-plane registry plus shared contract modules | TUI slash handling, Tauri commands, TS bridge, web consumers | `docs/project/backlog.md` |
| Ownership and extension boundary | skills, ownership model / extension boundary | plugin/runtime boundary docs and plugin delivery seams | project-local config, plugin hooks, command/skill delivery surfaces | `docs/project/backlog.md` for general pending work; `docs/architecture/plugin-boundary.md` only for HQ-specific migration scope |
| Coding-loop reliability | tools, write/edit, review | `crates/ava-tools/`, `crates/ava-agent/`, `crates/ava-review/` | TUI/desktop/web review and execution surfaces | `docs/project/backlog.md` plus benchmark/prompt validation follow-up |

## Next Step

- Use `docs/project/backlog.md` as the canonical pending-work intake surface.
- Move P0/P1/P2 implementation work into `docs/project/backlog.md`.
- Use `docs/architecture/cross-surface-runtime-map-m4.md` as the wiring baseline for the shared-vs-divergent audit across TUI, desktop, web, and headless paths.
- Use `docs/architecture/cross-surface-behavior-audit-m5.md` for the historical Milestone 5 behavior audit that classified those differences into shared invariants, intentional adapter-only behavior, and drift/bugs at audit time.
- Keep `docs/architecture/plugin-boundary.md` scoped to HQ-specific plugin migration follow-up only.

## Usage Notes

- This document is intentionally concise and meant to feed Milestone 3 gap analysis, not replace benchmark evidence or code review.
- The external behavior summaries above are documented-reference snapshots as of 2026-04-14, not permanent judgments.
- The most important outcome is not "match competitor feature counts". It is to identify which backend corrections reduce AVA's current risks: surface drift, unclear ownership boundaries, and contract duplication.
