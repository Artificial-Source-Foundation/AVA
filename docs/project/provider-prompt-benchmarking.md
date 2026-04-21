---
title: "Provider Prompt Benchmarking"
description: "Implemented provider/system-prompt benchmark workflow, current capabilities, and remaining gaps."
order: 6
updated: "2026-04-10"
---

# Provider Prompt Benchmarking

This document records the implemented provider/system-prompt benchmark workflow for the current `0.6 -> V1` cycle, how to run it, and what still remains to harden.

Use this page in two parts:

1. `Current Status` through `Recommended Workflow` documents what is implemented now.
2. `Future / Not Yet Implemented` records the longer-term plan and intentionally includes aspirational schema and workflow ideas that are not all shipped yet.

Goal:

1. Compare provider families, prompt families, and prompt variants on the same benchmark corpus.
2. Make prompt changes measurable, reproducible, and regression-safe.
3. Produce decision-quality reports instead of anecdotal prompt tuning.

## Current Status

Implemented now:

1. Benchmark CLI support for prompt family, prompt variant, prompt file, prompt version, prompt hash, repeat count, seed, and explicit output paths.
2. Benchmark report metadata for prompt configuration and repeat-run context.
3. Repeat-run support with raw-per-run output plus aggregate summaries.
4. Generic benchmark report comparison using left/right labels instead of only AVA/OpenCode wording.
5. Prompt override plumbing from benchmark mode into prompt assembly, including sub-agent inheritance.
6. A new `prompt_regression` suite with deterministic prompt-sensitive tasks.

Not fully implemented yet:

1. The `--seed` value is currently recorded in metadata but does not yet drive randomized fixture variation.
2. `--prompt-file` currently overrides the prompt-family note content used in prompt assembly, not the entire final system prompt.
3. Comparison reports are strong enough for prompt work now, but they do not yet emit richer failure-kind delta reporting or per-category prompt-specific regression summaries.

## What You Can Run Today

### Baseline prompt run

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

### Candidate prompt run

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark \
  --provider openai \
  --model gpt-5.4 \
  --suite prompt_regression \
  --prompt-family gpt \
  --prompt-variant candidate-a \
  --prompt-file crates/ava-agent/src/prompts/families/gpt.md \
  --repeat 3
```

### Compare two prompt reports

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark-compare-left-report <baseline-summary.json> \
  --benchmark-compare-right-report <candidate-summary.json> \
  --benchmark-compare-output <comparison.json>
```

Alias support currently exists for the older comparison flags too:

1. `--benchmark-compare-ava-report`
2. `--benchmark-compare-opencode-report`
3. `--benchmark-compare-prompt-a-report`
4. `--benchmark-compare-prompt-b-report`

## Shipped CLI Flags

These are implemented in `crates/ava-tui/src/config/cli.rs`:

```text
--prompt-family <name>
--prompt-variant <name>
--prompt-file <path>
--prompt-version <value>
--prompt-hash <value>
--repeat <n>
--seed <u64>
--benchmark-output <path>
--benchmark-compare-left-report <path>
--benchmark-compare-right-report <path>
--benchmark-compare-output <path>
```

## Current Report Behavior

For the canonical explanation of current schema, scoring, validation layers, and repeat semantics, see:

1. [Benchmark Deep Dive](../benchmark/deep-dive.md)
2. [Reports And Comparison](../benchmark/reports-and-comparison.md)

In short, current prompt benchmark reports carry prompt metadata, run metadata, and repeat-summary information sufficient for prompt-vs-prompt comparisons.

## Prompt Override Semantics

Current benchmark override behavior:

1. `--prompt-family` can force the prompt family/base template instead of relying on automatic family detection from the model name.
2. `--prompt-variant` is primarily report metadata so prompt variants can be compared cleanly.
3. `--prompt-file` currently overrides the prompt-family note content used in benchmark prompt assembly.
4. Sub-agents inherit the benchmark prompt override so prompt comparisons are not contaminated by child agents falling back to default prompts.

## Prompt Regression Suite

The current `prompt_regression` lane and its tasks are documented in:

1. [Suites And Workflows](../benchmark/suites-and-workflows.md)
2. [Prompt Benchmarking](../benchmark/prompt-benchmarking.md)

## Validation And Trust Notes

The benchmark system was hardened during implementation to avoid misleading prompt results:

1. Code-task quality can no longer report `PASS` when validation/tests fail.
2. Benchmark workspace scoping now points tools at the benchmark fixture root instead of the repo root.
3. Prompt overrides now propagate into sub-agents.
4. Report hashing now uses deterministic FNV-1a rather than `DefaultHasher`.
5. The workspace override path is protected by an RAII guard plus lock to avoid overlapping benchmark workspace contamination.

## Remaining Gaps

The main follow-up items are:

1. Make `--seed` drive deterministic fixture/task variation instead of metadata only.
2. Add richer failure-kind classification and failure-delta reporting in prompt comparisons.
3. Expand the `prompt_regression` suite from the current 6 tasks toward the planned 10 to 15 task golden prompt slice.
4. Potentially distinguish full-system-prompt file overrides from family-note overrides if we want heavier prompt experiments.
5. Consider making repeat aggregate `score_summary` task-weighted instead of flattened-sample-weighted.

## Recommended Workflow

For normal prompt tuning:

1. Run baseline prompt on `prompt_regression` with `--repeat 3`.
2. Run candidate prompt with the same suite, provider, model, and repeat count.
3. Compare the two aggregate reports.
4. If the candidate wins cleanly, run a larger slice such as `frontier` or `standard` before promoting it.
5. Keep the generated JSON artifacts so prompt changes remain auditable.

## Prompt Tuning Policy

The default policy is to keep prompts as lean as possible.

Use this hierarchy:

1. Start with a lean baseline prompt.
2. If the lean prompt already works on the benchmark slice, stop.
3. If it fails, tune only the specific failure mode the benchmark exposed.
4. Keep the tuned prompt as small as possible while making it reliable.

Practical rule of thumb:

1. Strong frontier models usually need less prompt specialization. Examples include top GPT, Codex-class, and Opus-class models.
2. Weaker or less stable families often need more targeted tuning, especially models that repeatedly fail on verification discipline, tool-call formatting, recovery, or loop resistance.
3. Chinese model families and provider-hosted variants should be benchmarked rather than assumed equivalent. If a provider-hosted variant behaves differently, prefer a small provider-family overlay instead of duplicating the full prompt.

The benchmark policy is:

1. baseline first
2. benchmark the real failure
3. tune the narrowest prompt surface that fixes it
4. re-benchmark
5. keep the smallest prompt that remains reliable

Avoid adding generic instruction bulk that strong models already follow without help.

## Future / Not Yet Implemented

The sections below are the longer-term plan for provider/system-prompt benchmarking. They include aspirational schema fields, workflows, and comparison/reporting goals that are not all shipped yet.

## What "Done" Means

This work is complete when:

1. Benchmark reports record provider, model, prompt family, prompt variant, prompt version, and task-fixture version.
2. The benchmark runner can execute repeated runs for the same provider/model/prompt combination and summarize pass rate plus variance.
3. AVA can compare two prompt variants directly on the same benchmark slice.
4. A small prompt-regression suite exists and is cheap enough to run on every prompt change.
5. Equivalent correct solutions pass validation consistently without reward for prompt-gaming or regex-gaming.

## Scope

This plan covers:

1. AVA benchmark runner support for prompt experiments.
2. Report schema changes.
3. Comparison and aggregation changes.
4. New prompt-sensitive benchmark tasks.
5. A repeatable workflow for provider-family tuning.

This plan does not require:

1. Changing the default tool surface.
2. Reintroducing HQ as a core product feature.
3. Building live dual-run orchestration against external agents inside the AVA benchmark runner.

## Product Requirements

The benchmark system must answer these questions directly:

1. Does prompt variant `B` improve correctness over prompt variant `A`?
2. Does it reduce wasted tool calls, loops, or unnecessary delegation?
3. Does it improve one suite while regressing another?
4. Is the improvement stable across repeated runs, or is it noise?
5. Which prompt family should be the default for each provider/model family?

## Report Schema Additions

Extend benchmark reports so every run includes these fields at the top level:

1. `benchmarkVersion`
2. `binaryCommit`
3. `binaryVersion`
4. `fixtureVersion`
5. `suiteName`
6. `taskFilter`
7. `provider`
8. `model`
9. `promptFamily`
10. `promptVariant`
11. `promptVersion`
12. `promptHash`
13. `runCount`
14. `runSeed`
15. `runnerMode`

Add these fields per task result:

1. `taskName`
2. `taskCategory`
3. `taskVersion`
4. `qualityPass`
5. `compileSuccess`
6. `testsPassed`
7. `testsTotal`
8. `durationMs`
9. `toolCalls`
10. `subagentCalls`
11. `promptFamily`
12. `promptVariant`
13. `promptHash`
14. `failureKind`
15. `failureSummary`
16. `workspaceHash`

Recommended failure kinds:

1. `logic_bug`
2. `verification_missing`
3. `tool_misuse`
4. `path_scope_error`
5. `over_edit`
6. `incomplete_fix`
7. `loop_or_turn_exhaustion`
8. `validator_mismatch`
9. `infra_error`
10. `cancelled`

## CLI Additions

Add first-class prompt experiment flags to `ava` benchmark mode:

```text
--prompt-family <name>
--prompt-variant <name>
--prompt-file <path>
--prompt-version <value>
--prompt-hash <value>
--repeat <n>
--seed <u64>
--benchmark-compare-left-report <path>
--benchmark-compare-right-report <path>
--benchmark-compare-output <path>
```

Behavior:

1. `--prompt-family` selects a known family assembly path such as `gpt`, `claude`, `gemini`, or `generic`.
2. `--prompt-variant` names the prompt variant within that family, such as `baseline`, `candidate-a`, or `candidate-b`.
3. `--prompt-file` allows local override experiments without editing repo prompt files.
4. `--repeat` executes the same benchmark slice multiple times and writes both raw runs and aggregate summaries.
5. `--seed` fixes repeatable randomization where the benchmark uses seeded variation.

## Runtime Wiring

Implement prompt selection as a benchmark-only override layer on top of normal prompt assembly.

Required behavior:

1. Benchmark runs must be able to force prompt family detection instead of relying only on automatic family inference.
2. Benchmark runs must be able to inject a prompt variant name into report output even if the underlying prompt is assembled from repo files.
3. Benchmark runs must be able to load a one-off prompt file without permanently changing repo prompt files.
4. Normal non-benchmark runtime behavior must stay unchanged when these flags are not set.

Suggested implementation path:

1. Add prompt benchmark overrides to the benchmark config object in `crates/ava-tui/src/config/cli.rs` and `crates/ava-tui/src/benchmark.rs`.
2. Thread those values into `AgentStackConfig` or an equivalent prompt-assembly override surface in `crates/ava-agent`.
3. Expose resolved prompt metadata back to the benchmark runner for report serialization.

## Repeat Runs And Variance

Add a repeat-run mode instead of relying on a single benchmark run.

For each task, record:

1. `attempts`
2. `passes`
3. `failures`
4. `passRate`
5. `medianDurationMs`
6. `p95DurationMs`
7. `medianToolCalls`
8. `medianSubagentCalls`
9. `failureKinds`

Aggregate summary should report:

1. Overall pass rate
2. Per-category pass rate
3. Median runtime
4. Worst-task variance
5. Prompt-variant win/loss/tie counts

Recommended defaults:

1. `--repeat 3` for routine prompt development.
2. `--repeat 5` for release decisions or default prompt swaps.

## Comparison Runner Expansion

Extend `benchmark_compare.rs` so it can compare:

1. AVA vs AVA prompt variant
2. AVA prompt family vs AVA prompt family
3. AVA prompt family vs OpenCode external report

Comparison output must include:

1. Win/loss/tie totals
2. Per-category win/loss/tie totals
3. New regressions introduced by candidate prompt
4. Tasks improved by candidate prompt
5. Tasks made slower by candidate prompt
6. Tool-call inflation or reduction
7. Failure-kind deltas

Comparison output should not collapse everything into one score. Keep these separate:

1. Correctness
2. Verification discipline
3. Tool efficiency
4. Delegation quality
5. Runtime cost/time

## New Prompt-Sensitive Benchmark Tasks

Add a small dedicated task lane for prompt behavior, separate from the existing implementation-heavy lanes.

Suggested lane name:

1. `prompt_regression`

Required task types:

1. `verify_before_finish`
2. `targeted_edit_only`
3. `minimal_patch_multifile`
4. `wrong_first_edit_recovery`
5. `no_unnecessary_subagent`
6. `read_before_edit`
7. `avoid_broad_rewrite`
8. `tool_choice_discipline`

Each task should score more than final correctness. Include targeted checks like:

1. Did the agent verify after editing?
2. Did it edit only the intended symbol or also unrelated ones?
3. Did it rewrite an entire file when a one-line change was enough?
4. Did it recover after a mistaken early edit?
5. Did it spawn subagents when the task did not need them?
6. Did it inspect the code before changing it?

## Golden Prompt Regression Suite

Create a cheap, stable subset that is run on every prompt change.

Recommended composition:

1. 4 existing coding/recovery tasks from the frontier slice
2. 6 to 10 new prompt-sensitive tasks

Recommended constraints:

1. Runs in under 15 minutes on a normal dev machine
2. Uses deterministic fixtures only
3. Avoids network-dependent MCP tasks

Success criteria for prompt edits:

1. No regressions in golden-suite pass rate
2. No new failure kind concentration
3. No major median runtime or tool-call blowup unless explicitly accepted

## Benchmark Validation Hardening

To make the benchmark more trustworthy, add validator guardrails:

1. Prefer semantic validation and tests over narrow implementation regexes.
2. When regexes are needed, allow equivalent correct implementations.
3. Record validator failures separately from agent failures.
4. Add fixtures that prove equivalent solutions pass.
5. Add tests for benchmark scoring so code-task `PASS` always implies validation success.

Recommended additions:

1. Benchmark self-tests for false-pass prevention
2. Benchmark self-tests for false-fail prevention on known equivalent implementations
3. Benchmark schema version tests so report readers fail loudly on incompatible changes

## Storage Layout

Recommended output layout under `~/.ava/benchmarks/`:

```text
~/.ava/benchmarks/
  raw/
    2026-04-10/
      <suite>-<provider>-<model>-<promptFamily>-<promptVariant>-run1.json
      <suite>-<provider>-<model>-<promptFamily>-<promptVariant>-run2.json
  aggregate/
    2026-04-10/
      <suite>-<provider>-<model>-<promptFamily>-<promptVariant>-summary.json
  compare/
    2026-04-10/
      <suite>-baseline-vs-candidate.json
```

## Suggested Execution Order

### Phase 1 — Metadata And Overrides

Implement:

1. Prompt-family and prompt-variant CLI flags
2. Prompt metadata in report schema
3. Prompt override plumbing into prompt assembly

Definition of done:

1. A benchmark report clearly identifies which prompt family and variant produced it.

### Phase 2 — Repeat Runs And Aggregates

Implement:

1. `--repeat`
2. Raw-per-run output plus aggregate summary output
3. Median/pass-rate reporting

Definition of done:

1. One command produces a stable multi-run summary for a prompt variant.

### Phase 3 — Prompt Regression Lane

Implement:

1. `prompt_regression` suite
2. 10 to 15 high-signal prompt-sensitive tasks
3. Golden regression slice

Definition of done:

1. Prompt changes can be evaluated in under 15 minutes on a stable high-signal task set.

### Phase 4 — Comparison Reports

Implement:

1. Prompt-vs-prompt compare runner support
2. Failure-kind deltas
3. Per-category win/loss output

Definition of done:

1. Prompt tuning decisions can be made from report artifacts without manual transcript archaeology.

### Phase 5 — Release Gating

Implement:

1. A documented benchmark command for prompt edits
2. A golden-suite gate in CI or release workflow
3. A default-prompt promotion checklist

Definition of done:

1. Default prompt changes cannot land without benchmark evidence.

## Concrete Commands To Support

Baseline run:

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark \
  --provider openai \
  --model gpt-5.4 \
  --suite prompt_regression \
  --prompt-family gpt \
  --prompt-variant baseline \
  --benchmark-output ~/.ava/benchmarks/aggregate/prompt-regression-baseline.json \
  --repeat 3
```

Candidate prompt run:

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark \
  --provider openai \
  --model gpt-5.4 \
  --suite prompt_regression \
  --prompt-family gpt \
  --prompt-variant candidate-a \
  --prompt-file crates/ava-agent/src/prompts/families/gpt-candidate-a.md \
  --benchmark-output ~/.ava/benchmarks/aggregate/prompt-regression-candidate-a.json \
  --repeat 3
```

Comparison:

```bash
cargo run -p ava-tui --bin ava --features benchmark -- \
  --benchmark-compare-left-report ~/.ava/benchmarks/aggregate/...baseline-summary.json \
  --benchmark-compare-right-report ~/.ava/benchmarks/aggregate/...candidate-a-summary.json \
  --benchmark-compare-output ~/.ava/benchmarks/compare/...baseline-vs-candidate-a.json
```

Legacy `--benchmark-compare-ava-report`, `--benchmark-compare-opencode-report`, `--benchmark-compare-prompt-a-report`, and `--benchmark-compare-prompt-b-report` aliases should remain accepted for compatibility.

## Suggested File Touch Points

Likely implementation areas:

1. `crates/ava-tui/src/config/cli.rs`
2. `crates/ava-tui/src/benchmark.rs`
3. `crates/ava-tui/src/benchmark_harness.rs`
4. `crates/ava-tui/src/benchmark_compare.rs`
5. `crates/ava-tui/src/benchmark_tasks.rs`
6. `crates/ava-agent/src/system_prompt.rs`
7. `crates/ava-agent/src/prompts/families/*`
8. `crates/ava-agent/src/stack/*`

## Release Checklist For Prompt Changes

Before promoting a prompt variant to default:

1. Run the golden prompt-regression suite at least `3` times.
2. Compare against the current default prompt report.
3. Confirm no correctness regressions.
4. Review any tool-efficiency regression explicitly.
5. Confirm no new benchmark validator false-fails were introduced.
6. Record the prompt variant, hash, and benchmark artifact path in the changelog or release notes.

## Recommended Next Step

The best first implementation slice is:

1. Add prompt metadata fields to the report schema.
2. Add `--prompt-family`, `--prompt-variant`, and `--repeat`.
3. Create a small `prompt_regression` suite with 6 to 8 tasks.
4. Extend the comparison runner to compare AVA prompt variants directly.

That gives AVA a real prompt-tuning loop quickly, without waiting for every ideal benchmark feature first.
