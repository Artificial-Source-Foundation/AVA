#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../../.." && pwd)"
binary="$repo_root/target/debug/ava-hq-plugin"

if [[ -x "$binary" ]]; then
  exec "$binary"
fi

exec cargo run --quiet -p ava-hq --bin ava-hq-plugin
