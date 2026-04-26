#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

cd "${repo_root}/cpp"
ctest --preset cpp-debug -R '^ava_m3_runtime$' --output-on-failure
