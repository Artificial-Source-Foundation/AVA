#!/usr/bin/env bash
set -euo pipefail

if ! command -v cargo-outdated >/dev/null 2>&1; then
  printf "[outdated] cargo-outdated is not installed\n"
  printf "[outdated] Install with: cargo install cargo-outdated --locked\n"
  exit 1
fi

printf "[outdated] Checking workspace dependency freshness\n"
cargo outdated --workspace --depth 1
