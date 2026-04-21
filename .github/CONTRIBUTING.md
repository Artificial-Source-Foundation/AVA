# Contributing to AVA

## Contributor bootstrap (one-time)

Install these prerequisites first:

1. Rust 1.86+
2. `just`
3. `cargo-nextest` (`cargo install cargo-nextest --locked`)
4. Node.js 20+
5. `pnpm`
6. `python3` (required for `just v1-signoff` / `pnpm signoff:v1`)

Then from repo root:

```bash
pnpm install
# fallback if hooks were not installed by prepare:
pnpm exec lefthook install
pnpm exec playwright install chromium
```

For Linux desktop development (`pnpm tauri dev`), install the Linux system deps from [Tauri Linux toolchain checklist](../docs/troubleshooting/tauri-toolchain-checklist.md).

Start in:

1. [AGENTS.md](../AGENTS.md) — source of truth for architecture, workflow, and doc-update rules
2. [docs/contributing/development-workflow.md](../docs/contributing/development-workflow.md) — day-to-day contributor loop and hook policy
3. [docs/how-to/test.md](../docs/how-to/test.md) — verification and check commands
4. [docs/contributing/releasing.md](../docs/contributing/releasing.md) — release workflow

For PRs, follow the workflow in [`docs/contributing/development-workflow.md`](../docs/contributing/development-workflow.md), run the relevant checks there, and keep docs/changelog updates in sync with AGENTS-guided requirements.
