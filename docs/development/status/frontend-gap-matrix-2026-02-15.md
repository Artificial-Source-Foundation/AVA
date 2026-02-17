# Frontend Gap Matrix (Benchmark-Driven)

> Scope: frontend UX/product gaps versus local reference codebases in `docs/reference-code/`.
> Last updated: 2026-02-15.

## Baseline

- AVA frontend baseline audited from `src/components`, `src/hooks`, `src/stores`, `src/services`.
- Benchmarks reviewed:
  - `docs/reference-code/cline` (`webview-ui`)
  - `docs/reference-code/openhands` (`frontend`, `openhands-ui`)
  - `docs/reference-code/opencode` (`packages/app`, `packages/ui`)

## Priority Matrix

| ID | Gap | Priority | Benchmark Signal | AVA Current | Target Outcome | Owner Source |
|---|---|---|---|---|---|---|
| FG-001 | Git control strip in chat | Delivered (P0) | OpenHands has repo/branch/pull/push/PR in conversation flow | Git strip shipped in chat flow | Continue manual runtime QA and polish | `docs/frontend/backlog.md` |
| FG-002 | Conversation cost/token visibility | Delivered (P0) | OpenHands/OpenCode expose per-session or per-turn usage clearly | Persistent usage summary + details dialog shipped | Continue streaming/compaction correctness checks | `docs/frontend/backlog.md` |
| FG-003 | Plugin catalog maturity | In progress (P0) | Marketplace UX in references has richer metadata and trust cues | Metadata/trust/version/changelog baseline shipped; remote catalog depth still thin | Featured curation, remote source integration, and stronger empty/error UX | `docs/development/backlogs/integration-backlog.md` |
| FG-004 | Long-session message performance | P1 | Cline/OpenCode use advanced list/backfill grouping strategies | `MessageList` improved but still simple linear rendering | Stable UX for very long sessions, scroll and render remain responsive | `docs/frontend/backlog.md` |
| FG-005 | Approval-state visibility in chat | P1 | OpenHands treats awaiting confirmation as first-class state | Tool approval exists but state visibility is still modal-centric | Persistent inline approval status + inline approve/reject actions | `docs/development/backlogs/integration-backlog.md` |
| FG-006 | Conversation share/export loop | P1 | OpenHands supports shareable conversation flows | No first-class share/export UX | Export/share session with clean read-only output | `docs/frontend/backlog.md` |
| FG-007 | Workspace panel adaptability | P2 | OpenCode/OpenHands provide stronger panel resizing/tab ergonomics | Layout is functional but less adaptive | Resizable/persisted panel ratios and richer panel ergonomics | `docs/frontend/backlog.md` |

## Execution Backlog

### FG-001 In-chat Git controls (P0)

- Status: delivered.
- Follow-up validation:
  - Confirm branch/pull/push/PR actions under manual desktop runtime matrix.
  - Keep keyboard-first command palette fallback behavior stable.

### FG-002 Usage/cost visibility (P0)

- Status: delivered.
- Follow-up validation:
  - Confirm values stay correct during long streaming runs and compaction.
  - Confirm per-turn usage breakdown remains accessible in <=2 clicks.

### FG-003 Plugin catalog maturity (P0)

- Scope:
  - Keep plugin cards/detail metadata baseline (version, source, trust marker, changelog snippet).
  - Add stronger featured ordering and remote metadata compatibility.
  - Further improve error/empty/loading states for install and catalog fetch.
- Acceptance:
  - Featured and catalog views include trust/version/source info.
  - Install failures expose actionable retry and recovery hints.

### FG-004 Long-session performance hardening (P1)

- Scope:
  - Add render-window/backfill strategy for very large message histories.
  - Improve scroll behavior consistency under fast streaming updates.
  - Preserve existing edit/retry/checkpoint behavior.
- Acceptance:
  - Large sessions remain responsive with smooth scroll and low UI jitter.
  - No regressions in message actions or checkpoint restore flow.

### FG-005 Inline approval-state UX (P1)

- Scope:
  - Expose persistent pending-approval state in chat timeline/header.
  - Move critical approve/reject actions inline near relevant tool calls.
  - Keep modal fallback for complex decisions.
- Acceptance:
  - User can understand and resolve pending approvals without context switching.
  - Approval state remains accurate across agent/chat transitions.

### FG-006 Share/export sessions (P1)

- Scope:
  - Add export path for session transcript + artifacts summary.
  - Add lightweight share flow (local export first, remote sharing optional).
  - Provide clear redaction controls for sensitive fields.
- Acceptance:
  - Session can be exported in a readable format with metadata.
  - User can exclude sensitive content before export.

### FG-007 Panel adaptability (P2)

- Scope:
  - Add draggable split ratios for main panels.
  - Persist per-session or global layout preferences.
  - Preserve accessibility and keyboard operation.
- Acceptance:
  - Panel ratio changes persist and restore reliably.
  - Layout remains usable at small and large window sizes.

## Suggested Sprint Mapping

- Sprint S2.3 (current plugin UX track): FG-003 + FG-005 + INT runtime closeout
- Next frontend hardening sprint: FG-004 remainder + manual QA for FG-001/FG-002
- Follow-up performance/collab sprint: FG-006 + FG-007

## Tracking Rules

- This file is the source of truth for benchmark-derived frontend gaps.
- Frontend backlog references this matrix for prioritization detail.
- Integration backlog references IDs where work crosses frontend/backend boundaries.
