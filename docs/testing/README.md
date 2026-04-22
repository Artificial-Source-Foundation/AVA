---
title: "Testing"
description: "Testing and verification concepts for AVA, including benchmark-backed validation."
order: 1
updated: "2026-04-22"
---

# Testing And Verification

This section explains how AVA verifies code and product changes across Rust, frontend, benchmark, and release-hardening paths.

This is maintainer and contributor material, not part of the normal product-reading path.

## Default Entry Points

1. [Rust Testing](rust-testing.md) - everyday Rust workspace checks
2. [Frontend Testing](frontend-testing.md) - frontend and desktop validation checks
3. [Desktop Testing](desktop-testing.md) - focused desktop regression workflow after refactors
4. [V1 Signoff Plan](v1-signoff-plan.md) - maintainer-only benchmark-backed V1 proof and signoff plan

## Extended Verification Docs

1. [Rust Testing](rust-testing.md) - Rust workspace commands and what they verify
2. [Frontend Testing](frontend-testing.md) - frontend and desktop validation commands
3. [Desktop Testing](desktop-testing.md) - focused desktop regression workflow after refactors
4. [V1 Signoff Plan](v1-signoff-plan.md) - maintainer-only benchmark-backed V1 proof and signoff plan
5. [Benchmark As Tests](benchmark-as-tests.md) - how benchmarks act as regression coverage
6. [Validation Tiers](validation-tiers.md) - Tier 1/2/3 validation model used in benchmarks

## Automation And Signoff

Milestone 2 implementation includes a repo-owned backend automation gate at `scripts/testing/backend-automation-gate.sh`.

- **Required (no-secrets)** checks: focused `ava-config` coverage (`cargo test -p ava-config`), lightweight desktop/Tauri compile smoke (`cargo check --manifest-path src-tauri/Cargo.toml --lib`), mock-stack unattended-approval + delegated-subagent smoke (`cargo run --bin ava-smoke`), and deterministic headless slash smoke (`cargo run --bin ava -- "/help" --headless --max-turns 1 --no-update-check`).
- **Optional live-provider** smoke:
  - Runs only when at least one API key is present.
  - Uses `AVA_OPENAI_API_KEY`, `AVA_ANTHROPIC_API_KEY`, or `AVA_OPENROUTER_API_KEY` in precedence order.
  - Pairing is deterministic by provider:
    - `openai` → `gpt-4.1`
    - `anthropic` → `claude-sonnet-4`
    - `openrouter` → `anthropic/claude-sonnet-4`
  - The command expects `BACKEND_GATE_OK` in output to pass.

Orchestration confidence now lives under `just check` (which runs the `ava-agent-orchestration` stack/e2e coverage), while `backend-gate` is intentionally the lighter backend smoke layer.

Entry points:

- `bash scripts/testing/backend-automation-gate.sh`
- `just backend-gate`

## V1 Preflight (`verify-v1`)

Use the local V1 preflight entrypoint at `scripts/testing/verify-v1.sh`.

Required order:

1. `just check`
2. `pnpm verify:mvp`
3. `just backend-gate`
4. `cargo test -p ava-tui primary_agent` (primary-agent behavior coverage in `ava-tui`)
5. deterministic Playwright parity coverage for `e2e/app.spec.ts` and `e2e/web-mode.spec.ts` only

Entry points:

- `bash scripts/testing/verify-v1.sh`
- `just v1-gate`
- `pnpm verify:v1`

This remains local-only for now (not wired into CI/hooks), and is intentionally an aggregate preflight for currently wired checks only. It is not a full final V1 signoff; full V1 signoff still requires the benchmark-backed headless proof in `docs/testing/v1-signoff-plan.md`, which is not run by this entrypoint, with fuller live-provider headless proof and broader desktop/TUI parity remaining outside this entrypoint.

## V1 Benchmark-Backed Headless Signoff (`signoff-v1-headless`)

Use the dedicated repo-owned benchmark-backed signoff entrypoint at `scripts/testing/signoff-v1-headless.sh`.

Entry points:

- `bash scripts/testing/signoff-v1-headless.sh`
- `just v1-signoff`
- `pnpm signoff:v1`

This signoff path is distinct from `verify-v1` preflight:

1. `verify-v1` remains local preflight only.
2. `signoff-v1-headless` is the authoritative headless V1-evals proof path and enforces pass/fail from benchmark results.
3. Report enforcement is fail-closed: it validates report metadata (`provider`, `model`, `task_filter`, `run_count`, `suite_name`) in addition to per-task success.

Milestone 5 closeout note: run `verify-v1` before `signoff-v1-headless` so signoff evidence includes primary-agent `ava-tui` coverage in addition to backend/subagent checks.

Terminology note: these V1 preflight/signoff labels are for the V1-evals track and are separate from the architecture milestone series (`docs/architecture/*-m4..m7.md`).

Required-now benchmark task slice (from `docs/testing/v1-signoff-plan.md`):

1. `small_coding_http_status_class` (selected `small_coding`)
2. `normal_coding_retry_backoff` (selected `normal_coding`)
3. `tool_reliability_timeout`
4. `tool_reliability_log_filter`
5. `tool_reliability_normalize` (selected `tool_reliability` coverage)
6. `stress_coding_log_pipeline` (one `stress_coding`)
7. `product_smoke_session_config_discovery` (one product-surface smoke journey)

Provider/model selection:

1. Explicit override: set both `AVA_V1_SIGNOFF_PROVIDER` and `AVA_V1_SIGNOFF_MODEL`.
2. Otherwise provider auth is auto-detected in precedence order: `AVA_OPENAI_API_KEY`, `AVA_ANTHROPIC_API_KEY`, `AVA_OPENROUTER_API_KEY`.
3. Missing auth fails closed (no fake pass).

## What This Section Covers

1. normal contributor verification
2. benchmark-backed regression testing
3. layered validation concepts
4. desktop-specific manual and automated smoke coverage
5. when to use narrow checks vs. broad checks

Local policy note: git hooks now keep `pre-commit` staged-file-oriented and make `pre-push` path-aware (docs-only stays light, frontend-sensitive changes run `pnpm typecheck` + `pnpm lint`, Rust/general changes run the local Rust gate with extra compile smokes for touched workspace wiring, desktop/Tauri, `ava-web`, and `ava-config`); the local Rust gate also includes focused `ava-agent` contract/ownership unit coverage plus desktop accepted-and-streaming run-start parity tests, while CI remains the authoritative full-suite gate.

## Related Docs

1. [Development Workflow](../contributing/development-workflow.md)
2. [Benchmark Docs](../benchmark/README.md)
