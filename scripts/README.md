# Scripts

Repository scripts are grouped by purpose instead of collecting at the root of `scripts/`.

## Layout

- `scripts/dev/` — local verification and maintenance helpers.
- `scripts/benchmarks/` — release and performance benchmark helpers.
- `scripts/testing/` — smoke tests, integration checks, and one-off validation flows.
- `scripts/docs/` — documentation automation.

## Key entrypoints

- `scripts/dev/check.sh` — full local verification pass.
- `scripts/dev/rust-coverage.sh` — workspace LCOV coverage.
- `scripts/dev/rust-outdated.sh` — Rust dependency freshness check.
- `scripts/benchmarks/release-benchmark.sh` — release binary benchmark checks.
- `scripts/testing/verify-mvp.sh` — MVP readiness test pass.
- `scripts/testing/claude-code-integration.sh` — manual Claude Code integration verification.
- `scripts/testing/rust-migration-smoke.sh` — legacy CLI smoke regression.
