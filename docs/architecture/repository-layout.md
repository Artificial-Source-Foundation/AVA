# Repository Layout

Top-level files and directories should stay intentional and stable.

## Root zones

- `crates/` — Rust-first CLI, agent, tooling, and shared runtime crates.
- `src/` and `src-tauri/` — desktop frontend and native host.
- `packages/` and `cli/` — legacy or desktop-only TypeScript code.
- `docs/` — product, architecture, sprint, and reference docs.
- `scripts/` — repository automation, validation, migration, and benchmark helpers.
- hidden directories such as `.ava/`, `.github/`, `.config/`, and `.tmp/` — local config, automation, or scratch state.

## Root hygiene rules

- Keep the root limited to entrypoint docs, workspace manifests, shared config, and a small number of operational scripts.
- Do not leave one-off benchmark outputs, compiled binaries, or language exercise files in the root.
- Put disposable local artifacts in `.tmp/` so cleanup stays obvious and `git status` stays readable.
- When adding a new top-level directory, document why it cannot live under `crates/`, `docs/`, `scripts/`, `src/`, or `packages/`.

## Root file categories

- Tool-discovered manifests and configs stay at root: `Cargo.toml`, `package.json`, `pnpm-workspace.yaml`, `tsconfig.json`, `vite.config.ts`, `playwright.config.ts`, `eslint.config.js`, `biome.json`, `lefthook.yml`, `commitlint.config.js`, `rustfmt.toml`, `deny.toml`, `clippy.toml`.
- Project entrypoint docs stay at root: `README.md`, `CHANGELOG.md`, `SECURITY.md`, `LICENSE`, `AGENTS.md`, `CLAUDE.md`.
- Convention-based distribution files stay at root when external tools expect them there, such as `install.sh`, `.env.example`, and `llms.txt`.
- Notes or historical snapshots that are not tool-discovered should live under `docs/` instead of the root.

## Current cleanup

Loose benchmark and scratch artifacts that had accumulated in the repository root were moved under `.tmp/root-scratch/`.
Additional leaked exercise files that had landed in `src/`, `tests/`, and Rust crate source directories were moved there as well so production paths only contain real project code.
The legacy `MEMORY.md` snapshot was archived under `docs/archives/project-history/` because it is historical documentation, not a root entrypoint.
