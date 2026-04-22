---
title: "Agent Backend Modularization Roadmap (Track Milestone 1)"
description: "Track Milestone 1 planning roadmap for backend modularization, including hotspots, target boundaries, risks, and validation gates."
order: 12
updated: "2026-04-21"
---

# Agent Backend Modularization Roadmap (Track Milestone 1)

This document captures **Track Milestone 1 planning only** for the backend modularization refactor.

This milestone numbering is local to the modularization track. It starts **after** the existing shared-backend contract and backend-correction milestones (`M6` and `M7`) that already normalized the current cross-surface runtime contract.

Scope for this milestone is intentionally tight:

1. Record hotspots and target ownership seams.
2. Define phased execution and validation gates for later milestones.
3. Avoid structural code refactors in this milestone.

## Inputs (baseline + research)

1. [Entrypoints](entrypoints.md)
2. [Crate map](crate-map.md)
3. [Canonical shared-backend contract (Milestone 6)](shared-backend-contract-m6.md)
4. [Backend correction roadmap (Milestone 7)](backend-correction-roadmap-m7.md)
5. [Agent backend capability audit (Milestone 1)](agent-backend-capability-audit-m1.md)
6. [Agent backend capability comparison (Milestone 2)](agent-backend-capability-comparison-m2.md)

## Relationship to prior milestone work

This roadmap is a follow-on structural track, not a replacement for the existing contract work.

1. `M6` remains the canonical shared-backend contract.
2. `M7` remains the implementation roadmap that established the current backend behavior baseline.
3. This modularization track starts from that baseline and focuses on ownership, extraction order, and long-term maintainability.

Best-practice direction preserved from Milestone 2 comparison:

1. Keep backend ownership seams explicit (Codex-like control-plane discipline).
2. Keep delegation/skills/approval semantics coherent as one runtime model (Claude Code-like cohesion).
3. Prefer extension boundaries over core-surface sprawl (PI Code/OpenCode-style boundary discipline).

## Current modularization hotspots

| Hotspot | Current risk | Why it matters now |
|---|---|---|
| `crates/ava-agent-orchestration/src/stack/` (`mod.rs`, `stack_run.rs`, `stack_tools.rs`) | High ownership concentration and mixed responsibilities | Makes changes risky and slows safe iteration on runtime behavior |
| `crates/ava-control-plane/src/` plus adapter projection layers | Contract normalization is in place, but adapter DTO/projection logic still clusters around multiple surfaces | Small backend changes can still create translation churn across desktop/web/TUI adapters |
| Subagent/delegation seams across `ava-agent-orchestration::subagents`, `ava-agent-orchestration::stack`, `ava-tools`, `ava-config` | Ownership improved, but composition/runtime/config boundaries still span multiple layers | Increases chance of duplicated policy and inconsistent introspection if future work recentralizes into orchestration |
| Bridge contracts (`src-tauri/src/commands/*`, `src-tauri/src/events.rs`, `crates/ava-web/src/api*.rs`, `src/types/rust-ipc.ts`) | DTO and event-field duplication risk | Small backend changes can create silent adapter regressions |

## Target boundaries (end-state intent)

| Owner seam | Should own | Should not own |
|---|---|---|
| `ava-control-plane` | Pure command/event/lifecycle/session/queue semantics | Adapter-specific transport shapes or backend runtime helpers |
| `ava-agent-orchestration::stack` runtime modules | Orchestration wiring and runtime composition | Mixed config parsing, delegation policy ownership, and long DTO projection logic |
| `ava-agent-orchestration::subagents` | Delegation catalog/runtime helpers and subagent-specific ownership seams | Generic stack composition or adapter projection logic |
| `ava-config` | Layered config schema + trust-gated loading | Runtime policy execution |
| Adapters (`ava-tui` web/headless, `src-tauri`) | Translation and transport only | Backend semantic decisions or fallback contracts |
| Frontend TS bridge/types | Typed mirrors of backend contract | Adapter-invented semantic fields |

Boundary policy for this refactor:

1. Prefer module extraction before crate extraction.
2. Keep transport-only layers thin and backend-owned semantics explicit.
3. Treat cross-surface parity drift as a backend contract defect, not UI-only debt.

## Milestone sequence

### Track Milestone 1 (this document only)

1. Lock hotspots, target seams, and phased execution plan.
2. Define risks and validation gates.
3. Make no structural code moves yet.

### Track Milestone 2 (first code slice)

1. Extract one high-churn `stack` concern into a dedicated backend module seam.
2. Keep public runtime behavior unchanged.
3. Add backend-owned fixtures/tests for the extracted seam before adapter changes.

### Track Milestone 3 (contract and adapter adoption)

1. Replace one adapter-local semantic branch with backend contract calls.
2. Align Tauri/web/TUI projections with the same required contract fields.
3. Remove duplicate fallback semantics where contract coverage exists.

### Track Milestone 4 (hardening and cleanup)

1. Finish remaining seam adoptions.
2. Delete dead compatibility branches that are no longer required.
3. Re-run bounded parity checks and update architecture docs with closure notes.

## Risks and mitigations

1. **Over-scoping refactor work**
   - Mitigation: one seam per milestone; no broad multi-owner rewrites.
2. **Behavior drift during extraction**
   - Mitigation: backend fixture gate before adapter edits; preserve wire contracts until parity tests pass.
3. **Contract duplication persisting in adapters**
   - Mitigation: make backend owner explicit per seam and fail PRs that introduce new adapter-owned semantics.
4. **Regression in headless-first proof lane**
   - Mitigation: keep headless verification mandatory for each milestone, with desktop/web/TUI as parity confirmation.

## Validation strategy for later milestones

Execution order for validation:

1. Backend unit/fixture gates on the changed owner seam.
2. Backend integration checks for session/lifecycle/delegation behavior where touched.
3. Headless-first smoke/regression checks for authoritative runtime proof.
4. Desktop/web/TUI parity checks for adapter projection correctness.
5. Focused docs/changelog update confirming scope and any bounded exceptions.

Promotion rule for later milestones:

1. No adapter-wide rollout until backend seam tests are green.
2. No “done” status without headless proof and parity confirmation.
3. No new exception without explicit entry in `backend-contract-exceptions.md`.

## Track Milestone 1 definition of done

1. A repo-local roadmap exists in architecture docs.
2. Hotspots, boundaries, sequence, risks, and validation strategy are explicit.
3. No structural code refactor is included in this milestone.
