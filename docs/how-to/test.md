---
title: "How-to: Run Tests And Checks"
description: "Run the repository's Rust and frontend verification commands that AVA contributors use."
order: 4
updated: "2026-04-21"
---

# How-to: Run Tests And Checks

Use this page when you want to verify a local change in this repository.

This is contributor-facing documentation for working on AVA itself, not a normal end-user guide.

See also: [How-to: Run your first workflow](first-workflow.md), [Development workflow](../contributing/development-workflow.md), [How-to: Run AVA in CI/headless automation](ci-headless-automation.md)

## Prerequisites (contributor setup)

Before running repo checks, install:

1. Rust 1.86+
2. `just`
3. `cargo-nextest` (`cargo install cargo-nextest --locked`)
4. Node.js 20+
5. `pnpm`
6. `python3` (required for `just v1-signoff` / `pnpm signoff:v1` report enforcement)

From repo root:

```bash
pnpm install
# fallback if hooks were not installed by prepare:
pnpm exec lefthook install
pnpm exec playwright install chromium
```

Notes:

1. `pnpm install` should install git hooks via `prepare` (`lefthook install`).
2. `pnpm exec playwright install chromium` is required for `pnpm test:e2e`.
3. Linux desktop contributors should install system dependencies from [Tauri Linux toolchain checklist](../troubleshooting/tauri-toolchain-checklist.md) before running `pnpm tauri dev`.
4. `pnpm test:e2e` exercises the Vite-served frontend with stubbed Tauri APIs; use [Desktop Testing](../testing/desktop-testing.md) when you need native desktop/IPC confidence.

## Run the standard Rust check set

```bash
just check
```

This command is implemented by the canonical entrypoint:

- [`scripts/dev/git-hooks.sh check`](../../scripts/dev/git-hooks.sh)

The equivalent check list is:

1. `cargo fmt --all --check`
2. `cargo clippy --workspace -- -D warnings`
3. `cargo nextest run -p ava-agent --test agent_loop --test stack_test --test e2e_test --test reflection_loop -j 4`
4. `cargo nextest run -p ava-tools -p ava-review -j 4`

This is the local Rust confidence gate. `just check` is just a convenience wrapper for this canonical hook path. It intentionally avoids unrelated provider/prompt-family unit-test coverage in `ava-agent`; CI remains the place where the full workspace suite is authoritative.

`pre-commit` validates the staged snapshot through `scripts/dev/git-hooks.sh pre-commit`, so partially staged same-file unstaged edits do not leak into hook results.

## Run the backend automation gate

For milestone backend proof alignment, use the dedicated gate script:

```bash
bash scripts/testing/backend-automation-gate.sh
```

or

```bash
just backend-gate
```

The gate has two layers:

1. **Required no-secrets checks (mandatory):**
    - `cargo test -p ava-config` (focused config-seam coverage for canonical subagent config path/read-write behavior)
    - `cargo test -p ava-agent agent_stack_run_dispatches_subagent_when_enabled -- --exact` (delegated runtime signoff path)
    - `ava-smoke` (mock provider path, now covering both unattended approval behavior and delegated `SubAgentComplete` smoke assertions)
    - headless deterministic slash smoke: `ava -- "/help" --headless --max-turns 1 --no-update-check`
2. **Optional live-provider smoke:** runs only when at least one key is present (`AVA_OPENAI_API_KEY`, `AVA_ANTHROPIC_API_KEY`, `AVA_OPENROUTER_API_KEY`).
   - Provider/model pairings are explicit and deterministic: `openai` with `gpt-4.1`, `anthropic` with `claude-sonnet-4`, and `openrouter` with `anthropic/claude-sonnet-4`.
   - The command must emit `BACKEND_GATE_OK`.

This remains a local validation aid only at this stage; it is not wired into hooks or CI yet.

## Run the local V1 preflight (`verify-v1`)

Use this repo-owned preflight entrypoint before broader V1 validation:

```bash
bash scripts/testing/verify-v1.sh
```

or

```bash
just v1-gate
# equivalent: pnpm verify:v1
```

The preflight runs this required sequence in order:

1. `just check`
2. `pnpm verify:mvp`
3. `just backend-gate`
4. `cargo test -p ava-tui primary_agent` (primary-agent behavior coverage in `ava-tui`)
5. deterministic Playwright parity coverage for `e2e/app.spec.ts` and `e2e/web-mode.spec.ts` only

Scope note: this is an aggregate local gate for the currently wired checks. It is intentionally not wired into CI or git hooks and is **not** the full final V1 signoff. Full V1 signoff still requires the benchmark-backed headless proof path described in `docs/project/v1-evals.md`, which is not run by this preflight; fuller live-provider headless proof and broader desktop/TUI parity coverage remain outside this entrypoint.

## Run the benchmark-backed headless V1 signoff (`signoff-v1-headless`)

For authoritative V1-evals headless proof, run the dedicated signoff path:

```bash
bash scripts/testing/signoff-v1-headless.sh
```

or

```bash
just v1-signoff
# equivalent: pnpm signoff:v1
```

This path runs a minimal required-now benchmark task slice from `docs/project/v1-evals.md` and enforces fail-closed pass/fail from the saved benchmark JSON report (including `provider`/`model`/`task_filter`/`run_count` metadata checks plus per-task success).

Milestone 5 closeout note: run `verify-v1` before `signoff-v1-headless` so primary-agent `ava-tui` coverage is included alongside backend/subagent checks.

Required-now task slice:

1. `small_coding_http_status_class`
2. `normal_coding_retry_backoff`
3. `tool_reliability_timeout`
4. `tool_reliability_log_filter`
5. `tool_reliability_normalize`
6. `stress_coding_log_pipeline`
7. `product_smoke_session_config_discovery`

Provider/model selection behavior:

1. Use explicit `AVA_V1_SIGNOFF_PROVIDER` + `AVA_V1_SIGNOFF_MODEL` together, or
2. auto-detect provider auth in precedence order (`AVA_OPENAI_API_KEY` -> `AVA_ANTHROPIC_API_KEY` -> `AVA_OPENROUTER_API_KEY`) with deterministic default models.

If no provider auth is available, signoff fails closed and does not report success.

Terminology note: these preflight/signoff labels are from the V1-evals track and are separate from the architecture milestone chain (`docs/architecture/*-m4..m7.md`).

## Local hook policy

1. `pre-commit` only touches staged files. It runs non-mutating checks on staged Rust files (`rustfmt --check` via `scripts/dev/git-hooks.sh pre-commit`, equivalent to `cargo fmt --check` on staged `.rs` files only), non-mutating `biome check` on staged JS/TS/JSON/CSS files, and `oxlint` on staged TypeScript files.
2. `pre-push` is path-aware: docs-only pushes skip heavy code gates, frontend-sensitive pushes run `pnpm typecheck` + `pnpm lint`, and Rust/general repo changes run the local Rust gate via `scripts/dev/git-hooks.sh pre-push`.
3. `just ci` remains the broader local verification pass.

## Run the broader local CI pass

```bash
just ci
```

`just ci` extends `just check` with the full workspace tests, docs build, and frontend checks. It is a broader local pass, not a literal CI clone; treat CI itself as the authoritative full gate.

## Run frontend checks directly

```bash
pnpm typecheck
pnpm lint
pnpm test
pnpm test:e2e
```

These scripts are defined in [`../../package.json`](../../package.json).

## Run a narrow test pass while iterating

```bash
just test
```

For source-backed usage details, see [`../contributing/development-workflow.md`](../contributing/development-workflow.md) and [`../testing/README.md`](../testing/README.md).
