#!/usr/bin/env bash
set -euo pipefail

run_step() {
  local label="$1"
  shift
  printf "[check] %s\n" "$label"
  "$@"
}

if command -v cargo-nextest >/dev/null 2>&1; then
  run_step "cargo nextest run --workspace" cargo nextest run --workspace
  run_step "cargo nextest run -p ava-tools -E test(default_tools_gives_6_tools)" cargo nextest run -p ava-tools -E 'test(default_tools_gives_6_tools)'
else
  run_step "cargo test --workspace (cargo-nextest missing)" cargo test --workspace
  run_step "cargo test -p ava-tools -- default_tools_gives_6_tools --exact" cargo test -p ava-tools -- default_tools_gives_6_tools --exact
fi

run_step "cargo clippy --workspace -- -D warnings" cargo clippy --workspace -- -D warnings
run_step "pnpm run lint" pnpm run lint
run_step "pnpm run format:check" pnpm run format:check
run_step "pnpm exec tsc --noEmit" pnpm exec tsc --noEmit
run_step "pnpm run test:run" pnpm run test:run

printf "[check] All checks passed\n"
