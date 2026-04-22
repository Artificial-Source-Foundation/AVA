---
title: "Subagent Rework Blueprint (Milestone 1)"
description: "Historical Milestone 1 backend-first rework blueprint for AVA subagents."
order: 7
updated: "2026-04-20"
---

# Subagent Rework Blueprint (Milestone 1)

This note captures the historical Milestone 1 backend-first plan for subagent rework.

It is intentionally a **planning artifact only** and is kept for historical context.

## Inputs and intent

1. [Agent backend capability audit (Milestone 1)](agent-backend-capability-audit-m1.md)
2. [Agent backend capability comparison (Milestone 2)](agent-backend-capability-comparison-m2.md)
3. [Active backlog](../project/backlog.md)

Established findings to preserve:

1. OpenCode ideas worth reusing: **session-per-subagent navigation**, **clickable child access**, **clear child status representation**, and a **user-editable subagent config surface**.
2. AVA ownership is currently fragmented across `ava-agent`, `ava-tools`, `ava-config`, `ava-tui`, and docs.
3. Rollout order should be: **backend seams first**, then **TUI/UI adoption**, then **final verification/docs sweep**.

## Target module boundaries

| Owner seam | Owns after rework | Must not own | Likely files/crates |
|---|---|---|---|
| `crates/ava-config` | Subagent config schema + layered loading | Runtime delegation logic | `crates/ava-config/src/agents.rs` |
| `crates/ava-agent::subagents` (**new dedicated module**) | Built-in catalog + resolved effective subagent definitions + introspection DTOs | UI presentation details | new `crates/ava-agent/src/subagents/{mod.rs,catalog.rs,config.rs}` |
| `crates/ava-agent::stack` + `routing` | Spawn/depth/budget lifecycle and routing decisions | Config file parsing and preset ownership | `crates/ava-agent/src/stack/{mod.rs,stack_run.rs}`, `crates/ava-agent/src/routing.rs` |
| `crates/ava-tools` | Tool transport contract (`task`/`subagent`) | Built-in subagent defaults | `crates/ava-tools/src/core/task.rs` |
| TUI/Desktop/Web | Navigation and rendering of backend-provided child sessions/status | Source-of-truth presets | `crates/ava-tui/src/app/event_handler/agent_events.rs`, `crates/ava-web/src/api.rs`, `src-tauri/src/events.rs`, `src/types/rust-ipc.ts`, `src/hooks/rust-agent-events.ts` |

**Boundary decision:** introduce a dedicated `ava-agent::subagents` module now (pragmatic modularization), but avoid a new crate in this milestone.

## Proposed subagent config design

Use a small TOML surface, user-editable, OpenCode-like in spirit.

### File and precedence

1. Backend built-ins (compiled defaults)
2. User-global file: `~/.ava/subagents.toml`
3. Trusted project file: `.ava/subagents.toml`

Compatibility during rollout:

1. Keep reading legacy `~/.ava/agents.toml` and `.ava/agents.toml`.
2. If both legacy and new files exist in the same scope, prefer `subagents.toml`.
3. Legacy `agents.toml` is read-only compatibility input during rollout; backend-owned write/update APIs persist only to `subagents.toml` in the selected scope.
4. Migration and legacy-use warning handling is centralized in the backend (no adapter-local migration/warning logic; no hard break in Milestone 1).
5. Path convention aligns with current repo/runtime docs (`~/.ava` + project `.ava`); any future XDG migration should be a separate tracked migration, not implicit in this milestone.

### Trust boundary (project config)

1. Project-level `.ava/subagents.toml` is loaded **only** when `ava-config` trust decisions mark the project trusted.
2. Untrusted projects must ignore project-local subagent config and use built-ins + user-global config only.
3. Phase 1 test coverage must explicitly include trusted vs untrusted project behavior for `.ava/subagents.toml` (and legacy `.ava/agents.toml` fallback behavior).

### Minimal schema shape (first pass)

```toml
[defaults]
enabled = true
agent = "explore"

[subagents.explore]
description = "Read-first exploration agent"
max_turns = 6

[subagents.review]
description = "Targeted code-review specialist"
temperature = 0.2

[subagents.general]
description = "General coding helper"
model = "openrouter/openai/gpt-4.1-mini"
```

`general` note: this blueprint treats `general` as a **new default candidate** if it is not already a shipped built-in in the current runtime; examples above are illustrative and not a claim that `general` is already present today.

Per-agent override fields to support first:

1. `enabled`
2. `description` (**new proposed field; implementation must add this to the per-agent schema, not assume it exists today**)
3. `prompt`
4. `model`
5. `max_turns`
6. `temperature`
7. `provider`
8. `allowed_tools`
9. `max_budget_usd`

Planning assumptions for compatibility/identity in Milestone 1 (non-normative; canonical event/API field rules live in [shared-backend-contract-m6.md](shared-backend-contract-m6.md)):

1. Subagent table key is the canonical stable internal ID (`subagents.<id>`).
2. Built-in IDs that are already shipped today should stay stable through migration (for example current shipped IDs such as `explore`/`review`); `general` is treated here as a candidate default profile unless already shipped.
3. Legacy `agents.toml` entries map by ID with no implicit renaming.
4. If aliasing is needed for legacy names, it must be explicit and backend-owned in a compatibility map (no adapter-local alias logic).

`tool_profile` status:

1. Not part of Milestone 1/2 public config contract.
2. Keep as backend-internal behavior only (if used), or defer until a later milestone with explicit contract docs.

Non-goal for this config file: adapter-only UI metadata (icons/cards/grouping).

## Phased rollout

### Phase 1 (next): backend-first foundation

Goal: make subagent definitions backend-owned before UI rework.

1. Create `ava-agent::subagents` and move built-in profiles/catalog out of `stack_run.rs`.
2. Add layered `subagents.toml` loading in `ava-config` with legacy fallback.
3. Resolve one backend effective catalog used by routing and tool-introspection paths.
4. Define backend-owned catalog/config APIs (read effective catalog; update persisted config) as the adapter integration seam.
5. Align `SubAgentComplete` payload work with canonical required-field rules in [shared-backend-contract-m6.md](shared-backend-contract-m6.md), including correlation and parent/child session-link fields.
6. Add backend fixtures/tests for payload and API coverage against the canonical contract before adapter changes.
7. Keep UI behavior functionally unchanged while backend seam settles.

### Phase 2: TUI/UI adoption

Goal: adopt backend APIs/contracts and improve child-session UX.

1. Prerequisite: Phase 1 backend catalog/config APIs and canonical `SubAgentComplete` payload are merged and tested.
2. TUI: session-per-subagent navigation + clearer child status display.
3. Desktop/Web: clickable child session access from parent run history using backend session-link fields (no adapter-specific reconstruction).
4. Settings surfaces read/write backend-owned config APIs (no TS-local preset source of truth).

### Phase 3: verification + docs/signoff

1. Backend-first regression gates stay mandatory (`ava-config` merge tests + `ava-agent` stack integration).
2. Add adapter smoke checks only after backend parity is green.
3. Update architecture/testing/docs references after behavior is stable.

## Backend-first verification approach

Run verification in this order:

1. **Config tests** (`ava-config`): precedence, trust-boundary behavior (trusted/untrusted project config), and legacy fallback behavior.
2. **Backend API tests** (`ava-agent`): catalog/config API behavior and compatibility-map behavior.
3. **Runtime integration tests** (`ava-agent/tests/stack_test.rs`): spawn/routing behavior against resolved catalog.
4. **Event-field conformance tests**: required `SubAgentComplete` fields (including correlation + session links) as defined in [shared-backend-contract-m6.md](shared-backend-contract-m6.md).
5. **Headless delegated smoke** (existing automation scripts): explore/review flows, plus `general` when that profile is present/enabled.
6. **Then adapter smoke only**: ensure backend catalog + event payload render in TUI/Desktop/Web.

Rule: no UI/TUI rework merges without backend tests green first.

## Likely files/crates to change

Backend/config:

1. `crates/ava-config/src/agents.rs`
2. `crates/ava-agent/src/stack/stack_run.rs`
3. `crates/ava-agent/src/stack/mod.rs`
4. `crates/ava-agent/src/routing.rs`
5. `crates/ava-agent/src/subagents/*` (new)
6. `crates/ava-tools/src/core/task.rs`
7. `crates/ava-agent/tests/stack_test.rs`

TUI/UI follow-up:

1. `crates/ava-tui/src/app/event_handler/agent_events.rs`
2. `crates/ava-tui/src/ui/{sidebar.rs,status_bar.rs,layout.rs}` (navigation/status rendering)
3. `src-tauri/src/events.rs` (desktop event projection for canonical delegation payload)
4. `crates/ava-web/src/api.rs` (web projection alignment)
5. `src/types/rust-ipc.ts` and `src/hooks/rust-agent-events.ts` (shared frontend event contract handling)
6. `src/components/settings/tabs/AgentsTab.tsx` and `src/components/settings/tabs/agents-tab-{list,detail}.tsx` (settings UI)

## Risks and non-goals

Risks:

1. Temporary drift if adapters still ship local preset assumptions during migration.
2. Backward-compat edge cases between `agents.toml` and `subagents.toml`.
3. Over-scoping UI polish before backend contracts stabilize.

Non-goals for this milestone:

1. No new delegation heuristics/policy redesign.
2. No new crate split unless module boundary proves insufficient.
3. No full visual redesign of delegated-run cards.

## Open questions for Milestone 2 kickoff

1. Should `defaults.agent = "general"` remain required, or infer from first enabled built-in?
2. Which fields are writable in adapter settings initially vs file-only advanced fields?
3. Is list/read/update API sufficient, or do we need richer filtered query APIs immediately?

This blueprint is done when Milestone 2 starts from these boundaries without re-litigating scope.
