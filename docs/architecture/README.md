---
title: "Architecture"
description: "Internal structure, crate boundaries, and architecture transition docs for AVA."
order: 1
updated: "2026-04-22"
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

## Active Planning

These are current forward-looking planning docs that build on the canonical seams above. They are implementation planning, not product-reading entrypoints.

1. [Agent backend modularization roadmap (Track Milestone 1)](agent-backend-modularization-roadmap-m1.md) - active planning roadmap for modularization hotspots, target owner seams, phased execution order, risks, and validation gates, building on the existing M6/M7 contract baseline
2. [Plugin boundary checklist](plugin-boundary.md) - future-track note for optional plugin migration work

## Historical Milestone Artifacts

These are preserved historical planning/audit artifacts that explain why the current canonical seams exist. Most contributors should not need them unless they are touching those seams directly.

Historical archive:

1. [Architecture archive](../archive/architecture/README.md) - milestone snapshots and superseded planning/audit artifacts
2. [Active backlog](../project/backlog.md) - current pending execution queue derived from milestone analysis and contract work

## Reading Rule

1. Start with `entrypoints.md`, `crate-map.md`, and `shared-backend-contract-m6.md`.
2. Treat the rest of this section as planning or historical context unless your change directly depends on it.
