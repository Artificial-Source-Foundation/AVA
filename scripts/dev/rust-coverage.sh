#!/usr/bin/env bash
set -euo pipefail

if ! command -v cargo-llvm-cov >/dev/null 2>&1; then
  printf "[coverage] cargo-llvm-cov is not installed\n"
  printf "[coverage] Install with: cargo install cargo-llvm-cov --locked\n"
  exit 1
fi

printf "[coverage] Running workspace coverage\n"
cargo llvm-cov --workspace --all-features --lcov --output-path target/llvm-cov/lcov.info

printf "[coverage] LCOV report written to target/llvm-cov/lcov.info\n"
