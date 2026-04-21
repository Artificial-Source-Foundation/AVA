---
title: "Benchmark As Tests"
description: "How AVA benchmarks act as regression coverage for runtime, prompt, and tool behavior."
order: 4
updated: "2026-04-10"
---

# Benchmark As Tests

Benchmarks in AVA are not only performance or comparison tools. They are also regression coverage for agent behavior.

## What Benchmarks Catch

1. coding regressions
2. verification-discipline regressions
3. tool-recovery regressions
4. prompt/provider behavior regressions
5. integration regressions across MCP, LSP-adjacent, and product smoke paths

## Why Benchmarks Matter

Some failures are hard to capture with ordinary unit tests because they involve:

1. prompt behavior
2. tool sequencing
3. recovery after a mistaken action
4. end-to-end agent correctness under workspace constraints

## Practical Use

1. use narrow suites for iteration
2. use broader suites for release-hardening
3. use `prompt_regression` for prompt changes
4. use report comparison when choosing between alternatives
