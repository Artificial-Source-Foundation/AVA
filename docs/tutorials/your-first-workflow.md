---
title: "Tutorial: Your First Workflow"
description: "Use AVA for one small task, then review and verify the result."
order: 2
updated: "2026-04-21"
---

# Tutorial: Your First Workflow

This tutorial walks through a small end-to-end workflow in a repository: ask AVA for one scoped change, review the result, and run the repository checks.

See also: [Tutorial: First run](first-run.md)

## Before you start

1. AVA is installed.
2. At least one provider is configured.
3. You are in a repository where a small change is safe to make.

If you still need setup, start with [Tutorial: First run](first-run.md).

## Step 1: Start AVA in the repository

From the repository root:

```bash
ava
```

The interactive TUI is the default run mode.

## Step 2: Ask for one small, verifiable task

Use a prompt like one of these:

1. `Update one outdated docs link and run the relevant check.`
2. `Fix one small lint warning and explain the change.`
3. `Add one missing test for a small function and run that test.`

Keep the first task small so verification is fast and the result is easy to inspect.

## Step 3: Let AVA finish the loop

During the run, expect AVA to inspect files, propose or make edits, and summarize what changed.

If you want the full command surface, use [Reference: Commands](../reference/commands.md).

## Step 4: Run a review pass

After the edit, run:

```bash
ava review
```

`ava review` is one of the documented CLI subcommands.

## Step 5: Verify the result

For this repository, the standard check command is:

```bash
just check
```

If `just` is not installed, run the canonical gate directly:

```bash
bash scripts/dev/git-hooks.sh check
```

That executes the same Rust gate as `just check`.

## Expected outcome

By the end of this tutorial, you should have:

1. Completed one small change with AVA.
2. Run `ava review` on the resulting diff.
3. Run the repository checks that apply to the change.

## Next step

Use [How-to: Configure providers and local settings](../how-to/configure.md) and [How-to: Run AVA locally](../how-to/run-locally.md) for the next tasks.
