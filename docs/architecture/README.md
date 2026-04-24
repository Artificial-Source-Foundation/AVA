---
title: "Architecture"
description: "Internal structure, crate boundaries, and architecture transition docs for AVA."
order: 1
updated: "2026-04-24"
---

# Architecture Docs

This section explains how AVA is organized internally and keeps canonical owner docs separate from historical transition material.

Milestone namespace note:

1. The shared-backend contract chain uses M4-M7, plus contract-follow-up closure milestones M10-M12.
2. The backend modularization roadmap uses separate **Track Milestone N** labels.

## Ownership Snapshot (Authoritative)

1. `ava-control-plane` owns pure cross-surface contracts (`commands`, `events`, `interactive`, `sessions`, `queue`, `orchestration`).
2. `ava-agent` owns runtime core plus backend-only helpers that depend on runtime types.
3. `ava-agent-orchestration` owns stack/subagent composition and delegation runtime wiring.
4. `ava-web` owns the web API/WebSocket surface for `ava serve`.

## Canonical Architecture Owners (Current)

Use these first when deciding where code should live today:

1. [Entrypoints](entrypoints.md) - runtime composition roots and adapter wiring for CLI/TUI, desktop, web, and shared backend seams
2. [Crate map](crate-map.md) - current Rust workspace boundaries and crate responsibilities
3. [Canonical shared-backend contract (Milestone 6)](shared-backend-contract-m6.md) - normative command/event/session/queue/delegation contract for cross-surface behavior
4. [Backend contract exceptions](backend-contract-exceptions.md) - versioned registry of intentional adapter-level contract exceptions
5. [Backend correction implementation roadmap (Milestone 7)](backend-correction-roadmap-m7.md) - implementation sequencing and adoption order against the canonical contract
6. [C++ backend/TUI migration plan (C++ Milestone 1)](cpp-backend-tui-migration-plan-m1.md) - staged backend/TUI rewrite plan, target CMake layout, and file-by-file migration order
7. [C++ contract freeze (C++ Milestone 1)](cpp-contract-freeze-m1.md) - concrete Phase 0 freeze scope, existing fixture anchors, drift risks, and Phase 1 signoff gates
8. [C++ M1 event-stream parity checklist](cpp-m1-event-stream-parity-checklist.md) - concrete headless JSON output checklist required before Phase 1 C++ work can start
9. [C++ workspace bootstrap README (Milestone 2)](../../cpp/README.md) - implemented CMake build graph bootstrap and explicit placeholder scope for initial C++ targets
10. [C++ Milestone 2 boundaries (green-fix pass)](../../cpp/MILESTONE2_BOUNDARIES.md) - scoped M2 bootstrap quality/documentation fixes, verification lane, and explicit deferred boundaries

## Active Planning

These are current forward-looking planning docs that build on the canonical seams above. They are implementation planning, not product-reading entrypoints.

1. [Agent backend modularization roadmap (Track Milestone 1)](agent-backend-modularization-roadmap-m1.md) - active planning roadmap for modularization hotspots, target owner seams, phased execution order, risks, and validation gates, building on the existing M6/M7 contract baseline
2. [Plugin boundary checklist](plugin-boundary.md) - future-track note for optional plugin migration work
3. [C++ backend/TUI migration completion gap audit (Post-M16)](cpp-backend-tui-migration-completion-gap-audit-m16.md) - planning audit of completion-critical gaps, deferred inventory, and RP evidence targets after Milestone 16
4. [C++ backend/TUI parity contract audit (Post-M16)](cpp-backend-tui-parity-contract-audit-post-m16.md) - planning checklist of scoped backend/headless/TUI contract evidence needed before claiming migration completion

## Historical Milestone Artifacts

These are preserved historical planning/audit artifacts that explain why the current canonical seams exist. Most contributors should not need them unless they are touching those seams directly.

Historical archive:

1. [Architecture archive](../archive/architecture/README.md) - milestone snapshots and superseded planning/audit artifacts
2. [Active backlog](../project/backlog.md) - current pending execution queue derived from milestone analysis and contract work

## Reading Rule

1. Start with `entrypoints.md`, `crate-map.md`, and `shared-backend-contract-m6.md`.
2. Treat the rest of this section as planning or historical context unless your change directly depends on it.
