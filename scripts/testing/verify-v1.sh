#!/usr/bin/env bash

# Local aggregate preflight for currently wired V1-path checks only.
# This is not the final V1 signoff path; that still requires the
# benchmark-backed headless proof described in docs/project/v1-evals.md
# via scripts/testing/signoff-v1-headless.sh, which is not run here.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

run_step() {
  local label="$1"
  shift

  printf '[verify:v1] %s\n' "$label"
  (cd "$repo_root" && "$@")
}

run_playwright_parity() {
  run_step "deterministic Playwright parity coverage (app + web-mode)" \
    pnpm test:e2e -- --workers=1 --retries=0 e2e/app.spec.ts e2e/web-mode.spec.ts
}

run_step "just check" just check
run_step "pnpm verify:mvp" pnpm verify:mvp
run_step "just backend-gate" just backend-gate
run_step "ava-tui primary-agent coverage" \
  ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo test -p ava-tui primary_agent
run_playwright_parity

echo "[verify:v1] local V1 preflight complete (not a full V1 signoff)"
echo "[verify:v1] full V1 signoff still requires scripts/testing/signoff-v1-headless.sh (benchmark-backed headless proof in docs/project/v1-evals.md), which this preflight does not run"
