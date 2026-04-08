---
title: "AVA 3.3.1 Evals"
description: "Implementation plan for core validation, benchmark expansion, and comparative E2E testing in AVA 3.3.1."
order: 4
updated: "2026-04-09"
---

# AVA 3.3.1 Evals

## Goal

AVA 3.3.1 is a validation and reliability release for the default core product.

The goal is to prove that core AVA can:

1. complete realistic coding work,
2. use tools well under pressure,
3. handle real integration workflows such as MCP,
4. behave predictably across TUI, desktop, and web,
5. and compare meaningfully against OpenCode.

## Principles

1. Reuse the existing benchmark infrastructure rather than creating a separate test universe.
2. Measure correctness, tool quality, recovery behavior, and efficiency separately.
3. Prefer build/test/validator outcomes over text-pattern-only scoring.
4. Test only the real current core surface; do not pretend future or stubbed features already exist.
5. Turn failures into narrow fixes and permanent regression coverage.

## Existing Foundation

Current benchmark/eval infrastructure already provides:

1. task suites such as `tool_reliability` and `normal_coding`,
2. validation tiers for pattern checks, compile/test checks, and file-edit validation,
3. benchmark scoring, model comparison, and judge support,
4. repo-owned model metadata for stable eval inputs.

Primary extension points:

1. `crates/ava-tui/src/benchmark.rs`
2. `crates/ava-tui/src/benchmark_harness.rs`
3. `crates/ava-tui/src/benchmark_tasks.rs`
4. `crates/ava-tui/src/benchmark_tasks/`
5. `crates/ava-tui/src/benchmark_support/validation.rs`
6. `crates/ava-tui/src/benchmark_support/workspace.rs`

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

1. `3.3.1` should test only the real current LSP surface.
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

`3.3.1` should define at least two classes of evals:

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

The point of `3.3.1` is not to add more product scope.

It is to prove, measure, and harden the core AVA that already exists.
