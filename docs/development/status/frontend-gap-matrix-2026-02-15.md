# Frontend Gap Matrix (Benchmark-Driven)

> Scope: frontend UX/product gaps versus local reference codebases in `docs/reference-code/`.
> Last updated: 2026-02-28.

## Baseline

- AVA frontend baseline audited from `src/components`, `src/hooks`, `src/stores`, `src/services`.
- Benchmarks reviewed:
  - `docs/reference-code/cline` (`webview-ui`)
  - `docs/reference-code/openhands` (`frontend`, `openhands-ui`)
  - `docs/reference-code/opencode` (`packages/app`, `packages/ui`)

## Priority Matrix

| ID | Gap | Priority | Status | Resolution |
|---|---|---|---|---|
| FG-001 | Git control strip in chat | P0 | **Delivered** | Git strip shipped in chat flow |
| FG-002 | Conversation cost/token visibility | P0 | **Delivered** | Per-session usage summary + details dialog |
| FG-003 | Plugin catalog maturity | P0 | **Delivered** | Marketplace sort, ratings, downloads, publish stub, creation wizard, remote catalog with TTL cache |
| FG-004 | Long-session message performance | P1 | **Delivered** | Adaptive visibleLimit, scroll-up backfill, content-visibility CSS |
| FG-005 | Approval-state visibility in chat | P1 | **Delivered** | ApprovalDock inline in composer, keyboard shortcuts, auto-expand for risky tools |
| FG-006 | Conversation share/export loop | P1 | **Delivered** | ExportOptionsDialog + export-conversation.ts with redaction/metadata/artifacts |
| FG-007 | Workspace panel adaptability | P2 | **Delivered** | Draggable right panel, persisted width (250-600px) |

## Status: ALL GAPS DELIVERED

All 7 benchmark-derived gaps are now delivered. Remaining work is manual QA validation only.

### Follow-up Validation (Manual QA)
- [ ] Confirm Git strip branch/pull/push/PR actions under desktop runtime
- [ ] Confirm cost/token values stay correct during long streaming and compaction
- [ ] Confirm plugin catalog install failures expose actionable retry hints
- [ ] Confirm large sessions remain responsive with smooth scroll
- [ ] Confirm approval state remains accurate across agent/chat transitions
- [ ] Confirm session export with redaction controls works end-to-end
- [ ] Confirm panel ratio changes persist and restore reliably

## Tracking Rules

- This file is the source of truth for benchmark-derived frontend gaps.
- Frontend backlog references this matrix for prioritization detail.
- Integration backlog references IDs where work crosses frontend/backend boundaries.
