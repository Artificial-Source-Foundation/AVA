---
title: "Reports And Comparison"
description: "Benchmark JSON artifacts, repeat summaries, and how report comparison works."
order: 3
updated: "2026-04-12"
---

# Reports And Comparison

## Report Types

The benchmark system currently produces two kinds of artifacts:

1. raw benchmark reports
2. aggregate repeat summaries when `--repeat > 1`

Comparison mode consumes saved benchmark JSON reports and emits a comparison JSON artifact.

## Top-Level Report Metadata

Current reports include metadata for:

1. schema version
2. AVA binary version and git commit
3. suite name and task filter
4. provider and model
5. prompt family, variant, version, hash, and prompt-file metadata
6. run count, run index, and optional seed
7. score summary
8. repeat summary when present

## Per-Task Result Data

Current task results include:

1. task name and category
2. quality pass/fail
3. compile/test results when relevant
4. runtime and cost data
5. tool and sub-agent counts
6. prompt metadata and run index

## Repeat Summary Behavior

When `--repeat > 1`, the runner writes:

1. raw per-run results
2. an aggregate summary with pass-rate and median-style metrics

Current repeat summary fields include:

1. pass rate
2. median runtime
3. p95 runtime
4. median tool calls
5. median sub-agent calls
6. worst-task variance
7. average cost
8. compile pass rate when compile validation exists for the task set

Important note:

1. the aggregate report's `score_summary` is currently based on flattened per-run samples
2. the `repeat_summary` field is the cleaner per-task repeat-run view and should be used as the primary repeat-run signal

## Output Paths

You can force a benchmark artifact path with:

```text
--benchmark-output <path>
```

Without an explicit path, benchmark artifacts are written under `~/.ava/benchmarks/`.

## Comparing Reports

Current comparison mode is generic left-vs-right comparison, not only AVA-vs-OpenCode.

Main command:

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark-compare-left-report <left.json> \
  --benchmark-compare-right-report <right.json> \
  --benchmark-compare-output <comparison.json>
```

Legacy aliases still work for compatibility.

## What Comparison Computes

Comparison output currently includes:

1. aligned task rows by task name
2. left-only and right-only task lists
3. left and right aggregate summaries
4. win/loss/tie counts
5. time and cost savings

For repeat-summary reports, comparison uses pass rate plus median task metrics so prompt-vs-prompt comparisons remain meaningful.

## Practical Comparison Workflow

1. run baseline benchmark and save the summary
2. run candidate benchmark with identical suite/provider/model settings
3. compare the two summary reports
4. inspect regressions before changing defaults

## Remaining Gaps

Not fully implemented yet:

1. richer failure-kind delta reporting
2. per-category prompt-specific regression summaries
3. fully task-weighted aggregate scoring for repeats
