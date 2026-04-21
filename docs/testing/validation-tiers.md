---
title: "Validation Tiers"
description: "The layered validation model used in AVA benchmarks and regression checks."
order: 5
updated: "2026-04-10"
---

# Validation Tiers

AVA benchmark validation is layered so a task is not judged only by final assistant text.

## Tier 1

Expected-pattern checks over the run output.

Use:

1. lightweight confirmation that the agent touched the expected concept

Risk:

1. too-narrow regexes can create false fails if they are overly implementation-specific

## Tier 2

Runtime-derived metrics and benchmark bookkeeping.

Use:

1. tool counts
2. sub-agent counts
3. runtime/cost metrics
4. quality/reliability summaries

## Tier 3

Deterministic fixture validation.

Use:

1. compile/test checks for code tasks
2. fixture-specific validators for smoke and integration tasks

Important rule:

1. code-task `PASS` requires Tier 3 validation success

## Why Tiers Matter

This model makes the benchmark more trustworthy and reduces false confidence from transcript-only success.
