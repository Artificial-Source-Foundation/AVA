---
title: "Suites And Workflows"
description: "Benchmark suites, task categories, and the main ways to run AVA benchmarks."
order: 2
updated: "2026-04-10"
---

# Suites And Workflows

## Core Run Shape

Each benchmark run does this:

1. choose a suite and task filter
2. generate deterministic task fixtures
3. run AVA against each task in the benchmark workspace
4. validate the result using compile/test checks or task-specific checks
5. save report artifacts

## Main Suites

### `speed`

Cheap tasks for fast local confidence and smoke checks.

### `standard`

Broader default benchmark coverage across common coding and tool-use tasks.

### `frontier`

Stronger project-scale and recovery-oriented tasks used to pressure the current runtime.

### `prompt_regression`

Prompt-sensitive tasks designed to detect behavior changes from provider-family or system-prompt edits.

Current prompt-regression tasks:

1. `prompt_regression_verify_before_finish`
2. `prompt_regression_targeted_edit_only`
3. `prompt_regression_minimal_patch`
4. `prompt_regression_read_before_edit`
5. `prompt_regression_wrong_first_edit_recovery`
6. `prompt_regression_tool_choice_discipline`

### `all`

Runs the full currently registered benchmark task corpus.

## Important Task Lanes

Beyond suite names, the benchmark corpus includes categories like:

1. coding tasks
2. tool recovery tasks
3. MCP integration tasks
4. LSP smoke tasks
5. product smoke tasks
6. prompt-regression tasks

## Useful Commands

### Run a single suite

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark \
  --provider openai \
  --model gpt-5.4 \
  --suite frontier
```

### Run a filtered task slice

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark \
  --provider openai \
  --model gpt-5.4 \
  --suite frontier \
  --task-filter "stress_coding_log_pipeline,tool_recovery_missing_file"
```

### Run prompt-regression with repeats

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark \
  --provider openai \
  --model gpt-5.4 \
  --suite prompt_regression \
  --prompt-family gpt \
  --prompt-variant baseline \
  --repeat 3
```

## Workflow Recommendations

For normal development:

1. use `speed` or a narrow task filter first
2. use `frontier` when evaluating meaningful runtime changes
3. use `prompt_regression` for prompt/provider tuning

For prompt tuning:

1. run baseline prompt with `--repeat 3`
2. run candidate prompt with the same provider/model/suite
3. compare the saved reports
4. expand to a broader suite before promoting the candidate

## Benchmark Trust Model

The benchmark is intended to measure real agent behavior, not just transcript keywords.

Current safeguards:

1. workspace-scoped tool access
2. compile/test-backed validation for code tasks
3. repeat-run support for stability measurement
4. report comparison over aligned task names

Current known limitations:

1. some prompt-sensitive behaviors are still measured indirectly rather than with full semantic diff analysis
2. `--seed` is currently metadata-only
