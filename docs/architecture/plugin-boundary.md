---
title: "Plugin Boundary"
description: "Execution checklist for moving HQ behind AVA's real plugin boundary."
order: 4
updated: "2026-04-08"
---

# HQ Plugin Boundary Checklist

This document turns the first AVA 3.3 architecture task into an execution checklist.

Goal: move HQ out of core AVA and behind a real installable plugin boundary.

## Outcome

When this track is complete:

1. Core AVA no longer assumes HQ exists.
2. HQ registers itself through plugin-owned backend and frontend seams.
3. Core settings, storage, routes, and shell state do not own HQ concepts.

## Phase 1: Build Plugin Host Seams

Core problem:

1. The current plugin system is hook/tool-process oriented, not an app-extension host.

Relevant files:

1. `crates/ava-plugin/src/lib.rs`
2. `crates/ava-plugin/src/manager.rs`
3. `docs/extend/plugins.md`
4. `src-tauri/src/lib.rs`
5. `crates/ava-web/src/lib.rs`

Checklist:

1. Define plugin registration for Tauri commands.
2. Define plugin registration for web routes.
3. Define plugin registration for event types/streams.
4. Define plugin registration for frontend screens/panels/settings sections.
5. Prove the host with one minimal dummy plugin.

Exit criteria:

1. A plugin can register one backend command.
2. A plugin can register one web route.
3. A plugin can expose one frontend mount point.

## Phase 2: Break Core Crate Ownership

Core problem:

1. HQ is still a direct dependency of core app surfaces.

Relevant files:

1. `Cargo.toml`
2. `crates/ava-tui/Cargo.toml`
3. `src-tauri/Cargo.toml`

Checklist:

1. Inventory all direct HQ dependencies in core crates.
2. Define the replacement plugin-facing interfaces.
3. Remove direct HQ deps from core crates once the host seams exist.

Exit criteria:

1. Core crates do not link HQ directly.
2. HQ can be omitted from the build without breaking core AVA.

Progress:

1. `src-tauri` no longer depends on HQ directly.
2. The old benchmark-only HQ linkage in `ava-tui` has been removed, so core builds no longer reference HQ at all.

## Phase 3: Move HQ Contracts Out Of Core

Core problem:

1. HQ DTOs and events are duplicated and core-owned today.

Relevant files:

1. `src/types/rust-ipc.ts`
2. `src-tauri/src/events.rs`
3. `src/hooks/rust-agent-events.ts`
4. `src/lib/api-client.ts`

Checklist:

1. Define one HQ-owned contract layer.
2. Remove core-owned HQ DTO duplication.
3. Route desktop/web/frontend through the same HQ plugin contract.

Exit criteria:

1. Core AVA does not define HQ-specific DTOs/events.
2. HQ plugin owns its runtime contract.

Progress:

1. Core-owned HQ DTO layers in Tauri, web mode, and frontend TS have already been removed along with the old built-in HQ route/command surfaces.
2. The remaining compatibility surface is intentionally small: the dead `hq_*` web event variants have now been removed from `ava-tui` too, and core desktop/frontend event typing only keeps the defensive legacy `hq_all_complete` fallback path where still needed for old streams.

## Phase 4: Move HQ Config And Storage Out Of Core

Core problem:

1. Core config and DB still own HQ concepts.

Relevant files:

1. `crates/ava-config/src/lib.rs`
2. future plugin-owned HQ role storage
3. `crates/ava-db/src/lib.rs`
4. `crates/ava-db/src/migrations/003_hq.sql`
5. `crates/ava-db/src/migrations/004_hq_agent_costs.sql`
6. `src/stores/settings/settings-types.ts`
7. `src/stores/settings/settings-persistence.ts`

Checklist:

1. Design plugin-scoped config storage for HQ settings and roles.
2. Design plugin-scoped persistence or migration namespace for HQ data.
3. Remove `config.hq` and core-owned HQ settings sync.
4. Remove HQ schema ownership from the shared core DB path.

Progress:

1. The dead `config.hq` section has been removed from `ava-config`, so core config no longer carries an HQ-specific top-level section.
2. Core desktop settings sync for HQ overrides is already gone too, so core no longer pushes HQ settings back into Rust config.
3. Core config no longer owns HQ role-profile logic at all; any future return must stay plugin-owned.
4. The dead HQ Rust model/repository layer has been removed from `ava-db`; only the historical SQL migrations remain in core for database compatibility.

Exit criteria:

1. Core config schema no longer contains HQ-specific sections.
2. Core DB layer no longer owns HQ tables/models.

Status:

1. The live core code path now satisfies those goals except for the historical HQ SQL migration files, which remain intentionally for database compatibility.
2. The other remaining HQ tie in core is benchmark-only isolation in `ava-tui`, not product/runtime ownership.

## Phase 5: Decouple Core Runtime And Shell

Core problem:

1. Core chat flow, shell state, and settings assume HQ is built in.

Relevant files:

1. `src/hooks/useAgentRun.ts`
2. `src/hooks/useAgent.ts`
3. `src/hooks/rust-agent-events.ts`
4. `src/components/layout/AppShell.tsx`
5. `src/components/layout/MainArea.tsx`
6. `src/components/layout/SidebarPanel.tsx`
7. `src/components/settings/settings-modal-content.tsx`
8. `src/components/chat/ChatView.tsx`
9. `src/components/chat/SubagentCard.tsx`

Checklist:

1. Remove built-in HQ mode toggles from core shell.
2. Remove "team mode routes to HQ" from the core agent run path.
3. Replace built-in HQ settings sections with plugin-provided sections.
4. Make core UI work cleanly when no HQ plugin exists.

Exit criteria:

1. Core app has no built-in HQ mode.
2. HQ UI appears only when the plugin is installed and enabled.

Progress:

1. Core shell/navigation/settings no longer ship built-in HQ entry points, the dead HQ/team frontend subtree is gone, and normal chat no longer routes through HQ/team-specific UI paths.
2. The remaining work in this phase is plugin-owned re-registration, not more core-shell removal.

## Phase 6: Move CLI, Web, And Desktop Registration

Core problem:

1. HQ commands and routes are hard-registered in core app startup paths.

Relevant files:

1. `crates/ava-tui/src/config/cli.rs`
2. `crates/ava-web/src/lib.rs`
3. `src-tauri/src/commands/mod.rs`
4. `src-tauri/src/lib.rs`
5. `src-tauri/src/commands/plugin_host.rs`

Checklist:

1. Remove built-in `ava hq ...` registration from core CLI.
2. Remove built-in `/api/hq/*` route registration from core web mode.
3. Remove built-in HQ IPC registration from core Tauri startup.
4. Re-register those surfaces from the HQ plugin.

Exit criteria:

1. Core AVA starts without HQ routes/commands.
2. Installing the HQ plugin restores HQ entry points.

Progress:

1. Core CLI no longer ships `ava hq ...`, core web mode no longer registers `/api/hq/*`, and core Tauri startup no longer registers HQ IPC commands.
2. The next HQ return, if it happens, should start from a fresh plugin-owned implementation instead of reviving old core crate wiring.

## Risks

1. Plugin host scope is larger than the current plugin system supports.
2. Desktop and web HQ backends are duplicated and may need unification first.
3. Existing HQ user data may need a migration path.
4. Frontend layout assumes HQ is a first-class app mode today.

## Suggested First Cut

If this work starts now, the best first implementation slice is:

1. define plugin host seams,
2. prove them with a dummy plugin,
3. then remove one HQ registration path from core.

Do not start by moving HQ internals first.
