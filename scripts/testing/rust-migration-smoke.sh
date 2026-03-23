#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
AVA_SMOKE=(cargo run --quiet --bin ava-smoke)
TMP_DIR="$ROOT_DIR/.tmp/rust-migration-smoke-$$"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

echo "[1/2] mock provider smoke binary"
(cd "$ROOT_DIR" && "${AVA_SMOKE[@]}" >/dev/null)

echo "[2/2] optional real-provider sanity"
if [[ -n "${AVA_ANTHROPIC_API_KEY:-}" || -n "${AVA_OPENAI_API_KEY:-}" || -n "${AVA_OPENROUTER_API_KEY:-}" ]]; then
  PROVIDER="openrouter"
  if [[ -n "${AVA_ANTHROPIC_API_KEY:-}" ]]; then
    PROVIDER="anthropic"
  elif [[ -n "${AVA_OPENAI_API_KEY:-}" ]]; then
    PROVIDER="openai"
  fi

  (cd "$ROOT_DIR" && cargo run --quiet --bin ava -- "Reply exactly with smoke-pass and stop." --headless --provider "$PROVIDER" --max-turns 2 >/dev/null)
else
  echo "No provider API key present, skipping live-provider sanity test"
fi

echo "All smoke checks passed."
