---
title: "Explanation: AVA Surfaces and Docs Boundaries"
description: "Why public docs are separated from architecture and backlog material in this repository."
order: 1
updated: "2026-04-18"
---

# Explanation: AVA Surfaces and Docs Boundaries

AVA has multiple runtime surfaces (TUI, headless CLI, desktop, and web) and multiple documentation audiences.

This page explains why the public docs layer is separated from internal planning and architecture docs.

## Public-facing docs should optimize for first success

The public path should answer:

1. How do I install AVA?
2. How do I authenticate?
3. How do I run one real workflow safely?

Those answers are now centered in:

1. [`../tutorials/first-run.md`](../tutorials/first-run.md)
2. [`../tutorials/your-first-workflow.md`](../tutorials/your-first-workflow.md)
3. [`../reference/README.md`](../reference/README.md)

## Internal docs have a different job

Architecture audits, roadmap, and backlog are essential for contributors and maintainers, but they are not first-run material.

Examples:

1. [`../architecture/README.md`](../architecture/README.md)
2. [`../project/roadmap.md`](../project/roadmap.md)
3. [`../project/backlog.md`](../project/backlog.md)

## Scope notes (to avoid overclaiming)

1. Plugin capability exists and is install-driven; plugin-owned UI/settings should appear when installed, as described in [`README.md`](../../README.md).
2. AVA works across multiple surfaces, but detailed cross-surface behavior and parity status belongs in architecture audits, not beginner docs.
3. Public quick-start docs intentionally avoid claiming full parity beyond what current docs and code paths explicitly support.

## Grounding sources

1. Surface statements in [`README.md`](../../README.md)
2. CLI/subcommand details in [`../reference/commands.md`](../reference/commands.md)
3. Internal architecture/parity analysis in [`../architecture/README.md`](../architecture/README.md)
