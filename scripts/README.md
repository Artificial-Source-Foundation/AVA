# Scripts

Repository scripts are grouped by purpose instead of collecting at the root of `scripts/`.

## Layout

- `scripts/dev/` — local verification and maintenance helpers.
- `scripts/benchmarks/` — release and performance benchmark helpers.
- `scripts/testing/` — smoke tests, integration checks, and one-off validation flows.
- `scripts/docs/` — documentation automation.

## Key entrypoints

- `scripts/dev/git-hooks.sh` — canonical hook/check entrypoint for:
  - staged `pre-commit` checks
  - path-aware `pre-push` checks
  - local Rust gate checks used by `just check` and `pre-push` on Rust/general changes
- `scripts/dev/run-rust-throttled.sh` — low-priority wrapper for heavy local Rust verification commands.
- `scripts/dev/rust-coverage.sh` — workspace LCOV coverage.
- `scripts/dev/rust-outdated.sh` — Rust dependency freshness check.
- `scripts/benchmarks/release-benchmark.sh` — release binary benchmark checks.
- `scripts/testing/git-hooks-regression.sh` — regression coverage for staged/pre-push hook routing.
- `scripts/testing/verify-mvp.sh` — MVP readiness test pass.
- `scripts/testing/verify-v1.sh` — local V1 preflight aggregate (`just check` -> `pnpm verify:mvp` -> `just backend-gate` -> deterministic Playwright parity coverage for `e2e/app.spec.ts` + `e2e/web-mode.spec.ts`). This intentionally covers currently wired checks only and does not include full V1 signoff.
- `scripts/testing/signoff-v1-headless.sh` — benchmark-backed headless V1 signoff entrypoint (authoritative V1-evals proof path) that runs the repo-owned required-now task slice from `docs/project/v1-evals.md` and enforces strict pass/fail from the benchmark JSON report.
- `scripts/testing/signoff-v1-regression.sh` — focused regression coverage for signoff branch logic (provider/model auth gating plus report-enforcement pass/fail).
- `scripts/testing/claude-code-integration.sh` — manual Claude Code integration verification.
- `scripts/testing/rust-migration-smoke.sh` — legacy CLI smoke regression.
- `scripts/testing/backend-automation-gate.sh` — mandatory no-secrets backend gate with optional live-provider checks.
