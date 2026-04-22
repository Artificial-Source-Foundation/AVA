---
title: "Development Workflow"
description: "Daily contributor commands, verification steps, and required doc update rules."
order: 2
updated: "2026-04-21"
---

# Development Workflow

This page describes the day-to-day contributor loop for AVA.

## Contributor Bootstrap And Prerequisites

Install and confirm these tools before running contributor checks:

1. Rust 1.86+
2. `just`
3. `cargo-nextest` (`cargo install cargo-nextest --locked`)
4. Node.js 20+
5. `pnpm`
6. `python3` (required for `just v1-signoff` / `pnpm signoff:v1`)

Then run from repo root:

```bash
pnpm install
# fallback if hooks were not installed by prepare:
pnpm exec lefthook install
pnpm exec playwright install chromium
```

Notes:

1. `pnpm install` runs `prepare` (`lefthook install`) in normal setups.
2. Playwright browser install is required before `pnpm test:e2e`.
3. Linux desktop contributors should install system packages from [Tauri Linux toolchain checklist](../troubleshooting/tauri-toolchain-checklist.md) before running `pnpm tauri dev`.

## Core Verification Commands

Use `just` for the main Rust workflow:

1. `just check` - format check, clippy, and targeted nextest
2. `just test` - run tests with `cargo nextest`
3. `just test-all` - run the full workspace test suite
4. `just lint` - run clippy across the workspace
5. `just fmt` - format Rust code
6. `just run` - run the TUI
7. `just headless "goal"` - run a headless task
 8. `just ci` - broader local verification pass (CI remains authoritative)

`just check` is intentionally the pragmatic local Rust confidence gate: it keeps the Rust loop fast enough for normal interactive work while leaving the broader local pass to `just ci` and the authoritative full gate to CI.

## Git Hook Policy

1. `pre-commit` is staged-file-oriented only via `scripts/dev/git-hooks.sh pre-commit`: it validates the staged snapshot rather than the working-tree copy, staged Rust files get non-mutating `rustfmt --check` (`cargo fmt --check` behavior for staged `.rs` files), staged JS/TS/JSON/CSS files get non-mutating `biome check`, and staged TypeScript files also get `oxlint`.
2. `pre-push` is path-aware via `scripts/dev/git-hooks.sh pre-push`: docs-only pushes skip heavy code gates, frontend-sensitive pushes run `pnpm typecheck` + `pnpm lint`, and Rust/general repo changes run the local Rust gate plus targeted compile smokes for touched workspace wiring, desktop/Tauri, `ava-web`, and `ava-config` surfaces.
3. CI remains the authoritative full gate.

## Frontend And Desktop Verification

Use `pnpm` for the desktop frontend and supporting JS/TS surfaces:

1. `pnpm lint`
2. `pnpm format:check`
3. `pnpm typecheck`
4. `pnpm test`
5. `pnpm test:e2e`
6. `pnpm tauri dev`

## Common Working Loop

1. Read `AGENTS.md` and the relevant docs before changing code.
2. Make the smallest correct change.
3. Run the narrowest useful checks while iterating.
4. Run `just check` before considering the work done.
5. Run frontend checks too if you changed `src/` or `src-tauri/` integration points.

## Required Doc Updates After Significant Changes

After a meaningful feature, fix, or refactor:

1. Update `CHANGELOG.md`
2. Update `docs/project/backlog.md`
3. Update `AGENTS.md` if workflow or architecture guidance changed
4. Update `docs/architecture/crate-map.md` if crate topology changed

## Supporting Files

1. `AGENTS.md` - primary workflow and architecture policy
2. `Justfile` - canonical Rust/dev commands
3. `package.json` - frontend, desktop, and JS/TS scripts
4. `scripts/README.md` - grouped repository helper scripts

## Related Docs

1. [Releasing AVA Desktop](releasing.md)
2. [Testing And Verification](../testing/README.md)
