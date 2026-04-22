---
title: "Benchmark Deep Dive"
description: "Schema, scoring, validation, and repeat-run semantics for AVA benchmarks."
order: 5
updated: "2026-04-12"
---

# Benchmark Deep Dive

This page is the canonical overview for benchmark schema, scoring, validation, and repeat-run semantics for the `0.6 -> V1` lane.

## Validation Layers

AVA benchmarks do not rely on final assistant text alone.

Current layers:

1. output-pattern checks
2. tool/runtime metrics captured during the run
3. Tier 3 fixture validation such as compile/test checks for code tasks

Important rule:

1. code-task `PASS` requires validation success and cannot be awarded from regex matches alone

## Scoring Model

The benchmark system preserves both raw metrics and derived score summaries.

The `0.6 -> V1` / `V1 eval` plan tracks these score dimensions conceptually:

1. completion
2. build/test validation
3. tool quality
4. recovery quality
5. efficiency
6. judge/reviewer quality where applicable

Reference weighting from the eval plan:

1. 35% correctness
2. 20% build/test validation
3. 15% tool-use quality
4. 10% recovery quality
5. 10% efficiency
6. 10% judge/reviewer quality

The exact aggregate output is written into benchmark reports rather than being inferred from transcript text.

See the current program framing in [`docs/testing/v1-signoff-plan.md`](../testing/v1-signoff-plan.md) for authoritative milestone-1 proof and parity requirements.

Current implementation note:

1. the shipped aggregate score currently combines `task_pass_rate`, `quality_signal_score`, `cost_normalization`, and `time_normalization`
2. the six-dimension eval-plan weighting above is the program model, not the exact live formula used by the current aggregate report implementation

## Report Schema

Current reports carry top-level metadata for:

1. schema version
2. binary version and git commit
3. suite name and task filter
4. provider and model
5. prompt metadata
6. run count, run index, and optional seed
7. score summary
8. repeat summary when present

Current per-task results carry:

1. task name and category
2. quality pass/fail
3. compile/test status when relevant
4. runtime and cost metrics
5. tool and sub-agent counts
6. prompt metadata and run index

Runtime-metric note:

1. `tokens_per_second` in current benchmark reports is wall-clock throughput, not pure decode-rate TPS
2. the denominator is total benchmark run time for that task, so tool execution and other agent overhead remain included
3. solo benchmark token totals now include sub-agent token usage so the TPS numerator better matches delegated work
4. solo benchmark reports now also expose `generation_tps`, which subtracts `ttft_ms` from total runtime before computing throughput

### TPS Semantics

`tokens_per_second` currently means wall-clock output-token throughput:

1. formula: `output_tokens / (total_time_ms / 1000.0)`
2. numerator: output tokens only, not input tokens
3. denominator: elapsed wall-clock task time for the benchmarked run

`generation_tps` currently means TTFT-normalized output-token throughput:

1. formula: `output_tokens / ((total_time_ms - ttft_ms) / 1000.0)`
2. numerator: the same output-token total used by `tokens_per_second`
3. denominator: the post-TTFT generation window only
4. availability: emitted only when `ttft_ms` exists and `total_time_ms > ttft_ms`

Important details:

1. all benchmark runs now include sub-agent output tokens in the same output-token total before TPS is computed
2. wall-clock time includes agent-loop overhead such as TTFT, tool execution, waiting between turns, network latency, and sub-agent coordination time
3. compile/test validation that runs after the agent finishes is not part of the TPS denominator for benchmark runs
4. `0.0` TPS is reported when elapsed time is zero to avoid divide-by-zero behavior
5. `generation_tps` is omitted when TTFT is missing, when `ttft_ms >= total_time_ms`, or when the post-TTFT window would otherwise be zero

What this metric is useful for:

1. comparing the same task across different models or prompt variants inside AVA
2. spotting regressions in end-to-end task pacing across repeat runs
3. understanding how much output a model produced per second in the full agent workflow, not just during token streaming

What `generation_tps` is useful for:

1. approximating the provider-style TPS users expect from external dashboards and tools
2. separating startup latency from post-first-token generation rate on solo runs
3. making short-task TPS less misleading when TTFT would otherwise dominate wall-clock throughput

What this metric is not:

1. not a provider decode-rate benchmark
2. not directly comparable to provider dashboards or external streaming-only TPS tools such as `oc-tps`
3. not reliable for cross-task comparisons when task structure differs a lot, especially between tool-light and tool-heavy tasks

What `generation_tps` is not:

1. not a perfect active-streaming TPS sampler like a timestamped token-window tool
2. historical pair-harness TPS reporting is no longer relevant because that path has been removed
3. not a pure parent-model metric when delegated sub-agents contribute output tokens to the same solo-run total: the numerator includes delegated output, but the denominator still uses the parent task's post-TTFT timing window

## Runtime Logging

Benchmark logging is part of the benchmark contract, not just a convenience.

The benchmark runner should always emit enough runtime context to answer:

1. what suite is running
2. what task is running now
3. which provider/model/prompt variant is being evaluated
4. what validation happened
5. where the resulting artifacts were saved

Warnings should be preserved when they reveal parser fallbacks, validation downgrades, or provider-specific behavior. Silent fallback paths make benchmark results much harder to trust.

## Failure Classification

The `0.6 -> V1` eval program classifies failures into fixable buckets such as:

1. prompt weakness
2. tool routing weakness
3. tool implementation bug
4. provider/model issue
5. validation gap
6. permission/trust friction
7. session or persistence bug
8. MCP/LSP integration bug
9. product-surface bug

Prompt benchmarking adds finer-grained runtime metadata, but the benchmark still benefits from human review of failure cause before changing defaults.

## Repeat Semantics

When `--repeat > 1`, the benchmark runner produces:

1. raw per-run reports
2. an aggregate repeat summary

Current repeat summary metrics include:

1. pass rate
2. median runtime
3. p95 runtime
4. median tool calls
5. median sub-agent calls
6. worst-task variance
7. average cost
8. compile pass rate when compile validation exists for the task set

Important note:

1. current aggregate `score_summary` values are sample-weighted over flattened runs
2. `repeat_summary` is the cleaner per-task view and should be treated as the primary repeat-run signal

## Workspace Semantics

Benchmark runs execute inside benchmark fixture workspaces under `~/.ava/benchmarks/workspace/`.

The benchmark runtime now explicitly scopes tool access to the benchmark workspace so tasks cannot accidentally resolve against the repo root.

## Prompt Benchmarking Notes

Prompt benchmarking adds:

1. prompt family
2. prompt variant
3. prompt file metadata
4. prompt version/hash metadata
5. repeat-run comparison support

Current limitations:

1. `--seed` is currently metadata-only
2. `--prompt-file` currently overrides prompt-family note content, not the entire final system prompt

## Related Docs

1. [V1 Signoff Plan](../testing/v1-signoff-plan.md)
2. [Suites And Workflows](suites-and-workflows.md)
3. [Reports And Comparison](reports-and-comparison.md)
4. [Prompt Benchmarking](prompt-benchmarking.md)
