---
title: "Prompt Benchmarking"
description: "Provider-family and system-prompt benchmark workflow for AVA."
order: 4
updated: "2026-04-10"
---

# Prompt Benchmarking

This page is the benchmark-section entrypoint for provider/system-prompt benchmarking.

The detailed implementation and usage documentation lives here:

1. [Provider Prompt Benchmarking](provider-prompt-benchmarking.md)

Use this page when you are navigating from the benchmark docs section and want the prompt-tuning workflow specifically.

## Current Prompt Benchmark Features

1. `--prompt-family`
2. `--prompt-variant`
3. `--prompt-file`
4. `--prompt-version`
5. `--prompt-hash`
6. `--repeat`
7. `--seed`
8. prompt-regression suite support
9. left/right report comparison

## Typical Workflow

1. run a baseline prompt on `prompt_regression`
2. run a candidate prompt on the same suite
3. compare the two saved reports
4. rerun on a larger suite if the candidate looks better

## Prompt Policy

Prompt tuning should be benchmark-first and as lean as possible.

1. start with the lean baseline prompt
2. if the model already passes, keep it lean
3. if it fails, tune only the specific failure mode the benchmark exposed
4. re-run the benchmark and keep the smallest prompt that works reliably

General rule of thumb:

1. stronger frontier models usually need less prompt specialization
2. weaker or more failure-prone families usually need more targeted tuning
3. provider-hosted variants should be tested, not assumed identical to the same family on another host
