#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -eq 0 ]; then
  printf 'usage: %s <command> [args...]\n' "$0" >&2
  exit 64
fi

if command -v ionice >/dev/null 2>&1; then
  exec ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-4}" "$@"
fi

exec nice -n 15 env CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-4}" "$@"
