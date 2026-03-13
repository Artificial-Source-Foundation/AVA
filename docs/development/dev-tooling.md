# Dev Tooling Setup

> Last updated: 2026-03-13

## Goal

Provide a consistent local verification workflow that matches CI expectations while keeping optional Rust tooling easy to adopt.

## Required Baseline

```bash
pnpm install --frozen-lockfile
cargo test --workspace
cargo clippy --workspace -- -D warnings
pnpm run lint
pnpm run format:check
pnpm exec tsc --noEmit
pnpm run test:run
```

## One-Command Local Verification

Use the helper script for the full mixed Rust + TypeScript verification pass:

```bash
bash scripts/dev/check.sh
```

Notes:

- Uses `cargo nextest run --workspace` when `cargo-nextest` is available.
- Falls back to `cargo test --workspace` when `cargo-nextest` is not installed.

## Optional Rust Tooling

These tools are recommended for faster iteration and better maintenance hygiene:

```bash
cargo install cargo-nextest --locked
cargo install cargo-llvm-cov --locked
cargo install cargo-outdated --locked
```

Helpers:

- Coverage: `bash scripts/dev/rust-coverage.sh`
- Dependency freshness: `bash scripts/dev/rust-outdated.sh`

## Git Hooks

Lefthook enforces pre-commit checks for Rust and TypeScript changes.

- Rust: `cargo fmt --all --check`, `cargo clippy --workspace -- -D warnings`
- TypeScript/Web: biome write-check, oxlint, typecheck

Install hooks:

```bash
pnpm run prepare
```
