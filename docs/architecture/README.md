---
title: "Architecture"
description: "Internal structure, crate boundaries, and architecture transition docs for AVA."
order: 1
updated: "2026-04-25"
---

# Architecture Docs

This section explains how AVA is organized internally and keeps canonical owner docs separate from historical transition material.

Milestone namespace legend:

1. **M4-M7** = shared-backend contract development.
2. **M10-M12** = contract-follow-up closure sequence.
3. **Track Milestone N** = backend modularization roadmap.
4. **C++ Milestone N** = Rust-to-C++ migration phases.
5. **M17-M20** = post-M16 C++ backend/headless/TUI completion roadmap slices.

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
6. [C++ contract freeze (C++ Milestone 1)](cpp-contract-freeze-m1.md) - active freeze scope, fixture anchors, drift risks, and signoff gates
7. [C++ workspace README](../../cpp/README.md) - current scoped C++ backend/headless/TUI workspace documentation and validation lanes
8. [C++/Rust parity gap audit (Post-M26)](cpp-rust-parity-gap-audit-post-m26.md) - current parity assessment for the scoped C++ lane and remaining non-goal buckets

## Active Planning

These are current forward-looking planning docs that build on the canonical seams above. They are implementation planning, not product-reading entrypoints.

1. [Agent backend modularization roadmap (Track Milestone 1)](agent-backend-modularization-roadmap-m1.md) - active planning roadmap for modularization hotspots, target owner seams, phased execution order, risks, and validation gates, building on the existing M6/M7 contract baseline
2. [Plugin boundary checklist](plugin-boundary.md) - future-track note for optional plugin migration work
3. [C++/Rust parity gap audit (Post-M26)](cpp-rust-parity-gap-audit-post-m26.md) - current non-web/non-desktop parity audit, identifying remaining C++ gaps across tools, permissions, MCP/custom tools, runtime, sessions, providers, TUI, CLI, and config

## Historical Milestone Artifacts

These are preserved historical planning/audit artifacts that explain why the current canonical seams exist. Most contributors should not need them unless they are touching those seams directly.

Historical archive:

1. [Architecture archive](../archive/architecture/README.md) - milestone snapshots and superseded planning/audit artifacts
2. [C++ milestone archive](../archive/cpp-milestones/) - historical C++ Milestone 2-33 boundary documents; `cpp/MILESTONE34_BOUNDARIES.md` remains the current final scoped boundary
3. [Active backlog](../project/backlog.md) - current pending execution queue derived from milestone analysis and contract work

## Reading Rule

1. Start with `entrypoints.md`, `crate-map.md`, and `shared-backend-contract-m6.md`.
2. Treat the rest of this section as planning or historical context unless your change directly depends on it.
