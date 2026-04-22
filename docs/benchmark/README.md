---
title: "Benchmark"
description: "How AVA benchmarking works, what it measures, and how to run it."
order: 1
updated: "2026-04-12"
---

# Benchmark Docs

This section documents AVA's benchmark system: how suites are structured, how runs are validated, how reports are produced, and how prompt/provider comparisons work.

## Documents

1. [Suites And Workflows](suites-and-workflows.md) - benchmark lanes, task types, and run patterns
2. [Reports And Comparison](reports-and-comparison.md) - JSON artifacts, repeat summaries, and report comparison flow
3. [Prompt Benchmarking](prompt-benchmarking.md) - provider/system-prompt benchmark workflow and prompt-regression usage
4. [Benchmark Deep Dive](deep-dive.md) - schema, scoring, validation layers, and repeat semantics
5. [Provider Prompt Benchmarking](provider-prompt-benchmarking.md) - maintainer-only provider/prompt tuning workflow

## What The Benchmark System Covers

1. Implementation quality on coding tasks
2. Verification discipline and tool-recovery behavior
3. MCP, LSP-adjacent, and product-surface smoke coverage
4. Repeat-run stability and prompt/provider comparison

## Main Modes

### Benchmark Mode

Runs AVA directly against benchmark tasks.

Main entrypoint:

```bash
cargo run -p ava-tui --bin ava --features benchmark -- --benchmark ...
```

### Report Comparison

Compares two saved benchmark JSON reports using generic left/right labels.

Main entrypoint:

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark-compare-left-report <left.json> \
  --benchmark-compare-right-report <right.json>
```

## Logging Requirement

Benchmark output must always make it obvious what the runner is doing.

At minimum, benchmark mode should log:

1. selected suite
2. workspace path
3. task filter
4. provider and model
5. prompt family and prompt variant when present
6. per-task start and finish with pass/fail status
7. validation outcome for code tasks
8. warnings that affected execution
9. saved artifact paths for reports and comparisons

This is a hard requirement for benchmark changes. If benchmark execution becomes less legible, the benchmark becomes harder to trust and debug.

## Suite Model

Current benchmark suites include:

1. `speed`
2. `standard`
3. `frontier`
4. `prompt_regression`
5. `all`

The suites are task filters over the benchmark task corpus. `prompt_regression` is the dedicated prompt-sensitive lane; `frontier` is the stronger stress/recovery slice used for broader agent evaluation.

## Validation Model

AVA benchmarking uses layered validation rather than relying only on final chat output.

Current layers:

1. Expected-pattern checks on agent output
2. Tool- and runtime-derived benchmark metrics
3. Tier 3 compile/test validation for code tasks using deterministic workspace fixtures

Important behavior:

1. Code-task quality does not report `PASS` unless validation succeeds in the benchmark run path.
2. Benchmark workspace scoping is pinned to the benchmark fixture root, not the repo root.
3. Equivalent correct implementations are preferred over narrow regex-only judgments wherever possible.

## Workspace Model

Benchmark tasks run inside generated fixture workspaces under `~/.ava/benchmarks/workspace/`.

The benchmark runner:

1. prepares the task fixture directory
2. trusts the workspace for AVA
3. scopes tool access to that workspace
4. runs the task
5. validates the result in the fixture

## Where To Start

1. Read [Suites And Workflows](suites-and-workflows.md) for how tasks are organized and run.
2. Read [Reports And Comparison](reports-and-comparison.md) for how output artifacts work.
3. Read [Prompt Benchmarking](prompt-benchmarking.md) for provider/system-prompt tuning.
