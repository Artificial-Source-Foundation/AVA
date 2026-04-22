---
title: "Architecture"
description: "Internal structure, crate boundaries, and architecture transition docs for AVA."
order: 1
updated: "2026-04-21"
---

# Architecture Docs

This section explains how AVA is organized internally and tracks important architecture transitions.

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

These are current forward-looking planning docs that build on the canonical seams above.

1. [Agent backend modularization roadmap (Track Milestone 1)](agent-backend-modularization-roadmap-m1.md) - active planning roadmap for modularization hotspots, target owner seams, phased execution order, risks, and validation gates, building on the existing M6/M7 contract baseline

## Historical Milestone Artifacts

These are preserved historical planning/audit artifacts that explain why the current canonical seams exist.

## Documents

1. [Plugin boundary checklist](plugin-boundary.md) - the active core-to-plugin migration plan for HQ and related seams
2. [Active backlog](../project/backlog.md) - current pending execution queue derived from milestone analysis and contract work
3. [Agent backend capability audit (Milestone 1)](agent-backend-capability-audit-m1.md) - historical Milestone 1 snapshot of coding-agent backend surfaces
4. [Agent backend capability comparison (Milestone 2)](agent-backend-capability-comparison-m2.md) - concise AVA-vs-OpenCode/Pi/Claude Code/Codex backend capability matrix and correction references
5. [Subagent rework blueprint (Milestone 1)](subagent-rework-blueprint-m1.md) - historical Milestone 1 planning blueprint for the subagent rework sequence
6. [Cross-surface runtime map (Milestone 4)](cross-surface-runtime-map-m4.md) - entrypoint-to-transport-to-shared-runtime map across interactive TUI, headless CLI, desktop, and web with parity-relevant divergence callouts
7. [Cross-surface behavior audit (Milestone 5)](cross-surface-behavior-audit-m5.md) - historical Milestone 5 contract-prep drift classification snapshot
8. [Cross-surface runtime audit (supporting M5 detail)](cross-surface-runtime-audit-m5.md) - historical supporting runtime-audit detail behind the Milestone 5 behavior audit
9. [Canonical shared-backend contract (Milestone 6)](shared-backend-contract-m6.md) - concrete contract-definition artifact covering scope, owner seams, adopters, command/lifecycle/event/session/queue/delegation semantics, headless rules, open decisions, and required conformance tests
10. [Backend correction implementation roadmap (Milestone 7)](backend-correction-roadmap-m7.md) - implementation-ready dependency-ordered roadmap turning M5 drift + M6 contract into prioritized workstreams, first code slices, adopter rollout, and conformance gates
11. [Backend contract exceptions](backend-contract-exceptions.md) - versioned registry of intentional adapter-specific exceptions to the shared backend contract
