---
title: "Scripts And Build Commands"
description: "Canonical repository commands for check, test, run, build, and desktop/frontend workflows."
order: 3
updated: "2026-04-21"
---

# Scripts And Build Commands

This page is contributor-facing reference for the repository workflow.

Normal product users usually do not need these commands. For user-facing install and run guidance, use [How-to: Install AVA](../how-to/install.md) and [How-to: Run AVA locally](../how-to/run-locally.md).

## Contributor Prerequisites

Required local toolchain:

1. Rust 1.86+
2. `just`
3. `cargo-nextest` (`cargo install cargo-nextest --locked`)
4. Node.js 20+
5. `pnpm`
6. `python3` (required for `just v1-signoff` / `pnpm signoff:v1`)

Bootstrap commands:

```bash
pnpm install
# fallback if hooks were not installed by prepare:
pnpm exec lefthook install
pnpm exec playwright install chromium
```

Use Linux desktop system dependencies from [Tauri Linux toolchain checklist](../troubleshooting/tauri-toolchain-checklist.md) when working on `pnpm tauri dev`.

## Primary Repo Commands (`just`)

From `Justfile`:

1. `just check` - format + clippy + targeted nextest
2. `just test [ARGS...]` - nextest run
3. `just test-all` - full workspace tests
4. `just lint` - workspace clippy with warnings denied
5. `just fmt` - format code
6. `just run [ARGS...]` - run `ava` binary in dev mode
7. `just headless "<goal>" [ARGS...]` - run headless goal
8. `just build-release` - release build for `ava`
9. `just ci` - broader local verification bundle

## Frontend/Desktop Scripts (`pnpm`)

From `package.json` scripts:

1. `pnpm dev` / `pnpm start` - Vite dev server
2. `pnpm build` - frontend build
3. `pnpm lint` - `oxlint` + `eslint`
4. `pnpm typecheck` - TypeScript type checking
5. `pnpm test` / `pnpm test:run` - Vitest
6. `pnpm test:e2e` - Playwright
7. `pnpm tauri <...>` - Tauri CLI passthrough

## Command Guidance In Repo Docs

`AGENTS.md` and `README.md` already point to these as the baseline flow:

1. `just check` before commit
2. `pnpm lint && pnpm typecheck` for frontend shells
3. `ava`, `ava --headless`, and `ava serve` runtime modes

`pre-commit` and `pre-push` both route through the canonical repo-owned entrypoint `scripts/dev/git-hooks.sh`. `pre-commit` is staged-file-only (Rust staged files get `rustfmt --check`; frontend staged files get `biome`/`oxlint`), and `pre-push` is path-aware rather than blindly running the Rust gate for every push.

## Resource-Throttled Variants

`AGENTS.md` includes lower-priority command variants (using `ionice` + `nice`) for heavy local runs.

When local machine responsiveness matters, prefer those throttled forms.

## Related

1. [Development Workflow](development-workflow.md)
2. [How-to: Run Tests And Checks](../how-to/test.md)
3. [Testing](../testing/README.md)
