---
title: "How-to: Run Your First Workflow"
description: "Contributor-oriented quick recipe for a small edit, review, and verification loop."
order: 5
updated: "2026-04-21"
---

# How-to: Run Your First Workflow

This page is a short contributor-oriented recipe for a small edit, review, and verification loop.

If you want the main public walkthrough, use [Tutorial: Your First Workflow](../tutorials/your-first-workflow.md).

## Goal

Run one coding task and one review pass in a real repo.

## 1) Start in the repository you want to edit

```bash
ava
```

If you prefer non-interactive mode:

```bash
ava "find and fix one small docs inconsistency" --headless
```

These are the normal interactive and headless entrypoints.

## 2) Give AVA a scoped first task

Good first prompts:

1. "Update one outdated docs link and run the relevant check."
2. "Fix one lint warning in this package and show the diff."
3. "Add one missing test for a small function and run that test."

Keep the first task small so verification is fast.

## 3) Run a review pass

After edits, run:

```bash
ava review
```

`ava review` is a documented CLI subcommand.

## 4) Verify with project-native checks

If you already use `just`, the repository's standard verification command is:

```bash
just check
```

If `just` is not installed, run the canonical gate directly:

```bash
bash scripts/dev/git-hooks.sh check
```

That executes the same Rust gate as `just check`.

For the full repo workflow, use [Development workflow](../contributing/development-workflow.md).

## 5) Capture what changed

Before opening a PR, save:

1. The exact files changed
2. Which checks were run
3. Any checks not run and why

That habit aligns with AVA's contributor workflow docs.
