#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ava_cli="${repo_root}/build/cpp/debug/apps/ava_cli"

if [ ! -x "${ava_cli}" ]; then
  echo "error: ava_cli is not built at ${ava_cli}" >&2
  echo "run: just cpp-configure cpp-debug && just cpp-build cpp-debug" >&2
  exit 1
fi

cd "${repo_root}/cpp"
ionice -c 3 nice -n 15 ctest --preset cpp-debug -R '^ava_m6_e2e$' --output-on-failure
