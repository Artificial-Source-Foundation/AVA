---
title: "V1 Signoff Plan"
description: "Maintainer-only plan for AVA 0.6 -> V1 validation, benchmark-backed proof, and signoff criteria."
order: 4
updated: "2026-04-19"
---

# V1 Signoff Plan

## Goal

AVA 0.6 is the stabilization cycle before V1.

The goal is to prove that core AVA can:

1. complete realistic coding work,
2. use tools well under pressure,
3. handle real integration workflows such as MCP,
4. behave predictably across TUI, desktop, and web,
5. and compare meaningfully against OpenCode.

The V1-evals milestone-1 proof posture is explicit: the backend is validated in a **headless-first** lane first, with TUI/desktop/web used to confirm lighter surface parity afterward.
Under the current scoped non-interactive exception, that headless lane is authoritative for backend correctness and does not by itself prove final interactive approval/question/plan parity.

Terminology note: this "Milestone 1" label belongs to the V1-evals track and is separate from the architecture milestone chain (`docs/architecture/*-m4..m7.md`).

## Principles

1. Reuse the existing benchmark infrastructure rather than creating a separate test universe.
2. Measure correctness, tool quality, recovery behavior, and efficiency separately.
3. Prefer build/test/validator outcomes over text-pattern-only scoring.
4. Test only the real current core surface; do not pretend future or stubbed features already exist.
5. Turn failures into narrow fixes and permanent regression coverage.

## V1-Evals Milestone 1 Proof Surface

1. **Primary proof is implementation outcomes, not contract checks only**: required evidence comes from real coding tasks that actually modify files and pass build/test validation.
2. **Headless-first execution**: benchmark + queue-based headless runs are the authoritative proving ground for backend reliability and automation shape. They are the source of truth for backend correctness under the scoped non-interactive exception.
3. **Approval policy baseline**: require explicit approval only for dangerous actions (destructive edits, risky command execution, external side effects); keep ordinary safe workspace actions low-friction.
4. **Comparison references**: `OpenCode` is the canonical baseline for backend/runtime contract and automation shape; `Goose` is a secondary reference for execution-mode and automation-pattern parity.

## Parity (Non-Headless) Gate

### Minimum parity/smoke requirement (required now)

Headless proof is still authoritative, and interactive surfaces use a narrow parity gate for the parts of runtime that can only be observed there.

Required now:

1. one core coding smoke journey on each available core surface (`core_tui_smoke`, `core_desktop_smoke`, `core_web_smoke`) or the practical subset if a surface is unavailable,
2. one explicit session/resume smoke (`session_resume_smoke`), and
3. completion of the tiny approval parity matrix below.

### Tiny approval/question/plan parity matrix

| Class | What to verify | Expected behavior |
| --- | --- | --- |
| safe read | tool action that inspects files without edits | runs without explicit approval and returns expected read output |
| safe edit | non-destructive file creation/modification | runs with visible diff and deterministic verification, no destructive permission prompt |
| destructive edit | destructive file changes (delete/overwrite) | requires explicit approval and records intent before execution |
| risky shell/external effect | shell/network actions with meaningful side effects | requires explicit approval, executes only in scoped fixture workflows |

This parity gate is intentionally limited by scope: it validates visible behavior and surface ergonomics, not full backend correctness.

## Scope split: required-now vs expansion lanes

Required-now suites focus on the minimum milestone-1 gate:

1. selected `small_coding`
2. selected `normal_coding`
3. selected `tool_reliability`
4. one `stress_coding`
5. one product-surface smoke journey

Expansion lanes are useful for confidence and breadth but are not required for the milestone-1 pass:

1. `tool_recovery`
2. `verification_discipline`
3. `tool_efficiency`
4. full MCP suites (`mcp_filesystem`, `mcp_git`, `mcp_multi_server`, `mcp_failure_recovery`)
5. full LSP-adjacent suites (`lsp_presence`, `language_project_smoke`, `diagnostic_repair`)

### Repo-owned benchmark-backed headless signoff (required-now entrypoint)

The repo-owned signoff entrypoint for this required-now slice is:

- `scripts/testing/signoff-v1-headless.sh`

It is wired to:

- `just v1-signoff`
- `pnpm signoff:v1`

Current required-now task filter (minimal, explicit):

1. `small_coding_http_status_class`
2. `normal_coding_retry_backoff`
3. `tool_reliability_timeout`
4. `tool_reliability_log_filter`
5. `tool_reliability_normalize`
6. `stress_coding_log_pipeline`
7. `product_smoke_session_config_discovery`

This entrypoint is intentionally separate from `scripts/testing/verify-v1.sh`, which remains a local preflight aggregate and not final signoff.

## Existing Foundation

Current benchmark/eval infrastructure already provides:

1. task suites such as `tool_reliability` and `normal_coding`,
2. validation tiers for pattern checks, compile/test checks, and file-edit validation,
3. benchmark scoring, model comparison, and judge support,
4. repo-owned model metadata for stable eval inputs.

Primary extension points:

1. `crates/ava-tui/src/benchmark.rs`
2. `crates/ava-tui/src/benchmark_tasks.rs`
3. `crates/ava-tui/src/benchmark_tasks/`
4. `crates/ava-tui/src/benchmark_support/validation.rs`
5. `crates/ava-tui/src/benchmark_support/workspace.rs`

## Suite Plan

### 1. Core Coding Suites

These answer: can AVA actually write software?

Planned suites:

1. `small_coding` - compact but real coding tasks from scratch
2. `normal_coding` - representative feature and bugfix tasks
3. `stress_coding` - longer prompts, more tool pressure, more recovery pressure
4. `large_project` - project-scale feature or scaffold tasks across many files
5. `test_heavy` - code plus test creation/fix/verification
6. `maintenance` - refactors, migrations, cleanup, and low-regression work

### 2. Tool-Use Quality Suites

These answer: does AVA use tools correctly and efficiently?

Planned suites:

1. `tool_reliability`
2. `tool_recovery`
3. `verification_discipline`
4. `tool_efficiency`

### 3. MCP And Integration Suites

These answer: does core AVA integrate with external server/tool systems?

Planned suites:

1. `mcp_filesystem`
2. `mcp_git`
3. `mcp_multi_server`
4. `mcp_failure_recovery`

### 4. LSP-Adjacent Suites

These answer: does the current language-intelligence surface behave correctly?

Planned suites:

1. `lsp_presence`
2. `language_project_smoke`
3. `diagnostic_repair`

Important scope note:

1. `0.6 -> V1` should test only the real current LSP surface.
2. Stub or future-only LSP features should be documented as deferred, not silently implied.

### 5. Product-Surface Smoke Suites

These answer: does core AVA behave correctly as a product?

Planned suites:

1. `core_tui_smoke`
2. `core_desktop_smoke`
3. `core_web_smoke`
4. `session_resume_smoke`
5. `provider_switching_smoke`
6. `permissions_smoke`

### 6. Competitive Baseline Suites

These answer: is AVA improving relative to an external baseline?

Planned comparison path:

1. run the same corpus against AVA,
2. run the same corpus against OpenCode,
3. score both with the same validator pipeline,
4. compare success, quality, cost, and recovery behavior.

Where useful, use `Goose` as a secondary reference for execution-mode behavior and automation workflow patterns.

## Scoring Model

Each run should preserve raw metrics and derived scores.

Recommended score dimensions:

1. `completion`
2. `build`
3. `tests`
4. `tool_quality`
5. `recovery`
6. `efficiency`
7. `judge_quality`

Recommended weighting:

1. 35% correctness
2. 20% build/test validation
3. 15% tool-use quality
4. 10% recovery quality
5. 10% efficiency
6. 10% judge/reviewer quality

## Failure Classification

Every failed run should be bucketed into a fixable class.

Primary buckets:

1. prompt weakness
2. tool routing weakness
3. tool implementation bug
4. provider/model issue
5. validation gap
6. permission/trust friction
7. session or persistence bug
8. MCP/LSP integration bug
9. product-surface bug

## Remediation Loop

When an eval fails:

1. reproduce the run deterministically if possible,
2. classify the failure,
3. fix the narrowest responsible layer,
4. add or tighten a regression task/test,
5. rerun the affected suite,
6. rerun the aggregate gate before merge.

## Implementation Phases

### Phase 1 - Spec And Plumbing

1. lock this eval spec,
2. define suite names and release-gate labels,
3. define result schema additions if needed,
4. decide which suites are required vs experimental.

### Phase 2 - Core Coding Expansion

1. `small_coding`
2. `stress_coding`
3. `large_project`
4. `test_heavy`
5. `maintenance`

### Phase 3 - Tool And Recovery Expansion

1. stronger `tool_reliability`
2. `tool_recovery`
3. `verification_discipline`
4. `tool_efficiency` refinements

### Phase 4 - MCP And LSP Core Integration

1. filesystem MCP E2E
2. git MCP E2E
3. multi-server MCP smoke
4. honest LSP-presence and language-smoke coverage

### Phase 5 - Product-Surface Smoke Coverage

1. TUI core smoke journey
2. desktop core smoke journey
3. web core smoke journey
4. session/model/permission smoke coverage

### Phase 6 - Competitive Baselines

1. AVA-vs-OpenCode runner
2. comparable output format
3. score and metric diff reports

## Release Gate Proposal

`0.6 -> V1` should define at least three classes of evals:

1. `core_required`
2. `integration_required`
3. `experimental`

Suggested initial `core_required` set:

1. selected `small_coding` tasks
2. selected `normal_coding` tasks
3. selected `tool_reliability` tasks
4. one `stress_coding` task
5. one product-surface smoke journey

Suggested initial `integration_required` set:

1. filesystem MCP
2. git MCP
3. session resume smoke

## Standpoint

The point of `0.6 -> V1` is not to add more product scope.

It is to prove, measure, and harden the core AVA that already exists.
