#!/bin/bash
# Run print-members generation and atomically publish its completion marker.
#
# The shared generation lock is acquired before the old marker is removed or
# the source signature is read. Only a successful Python exit reaches the
# same-directory temp + rename below, so a failed or interrupted generator
# leaves completion missing and a publisher interruption can leave only an
# unrecognized temp file or the complete new marker.
#
# Usage: run_generate_print_members.sh <python> <generator.py> <tags.json> \
#          <output-dir> <source-signature> <completion-marker> \
#          <flock-executable> <generation-lock>
set -euo pipefail

lock_held=false
if [[ "${1:-}" == "--ava-generation-lock-held" ]]; then
  lock_held=true
  shift
fi

if [[ $# -ne 8 ]]; then
  echo "usage: $0 <python> <generator.py> <tags.json> <output-dir> <source-signature> <completion-marker> <flock-executable> <generation-lock>" >&2
  exit 2
fi

python_exe="$1"
generator="$2"
tags_json="$3"
output_dir="$4"
source_signature="$5"
completion_marker="$6"
flock_exe="$7"
generation_lock="$8"

if [[ "$generation_lock" != /* ]]; then
  generation_lock="$(pwd -P)/$generation_lock"
fi

# Keep the lock descriptor out of Python and anything it launches. Re-exec the
# complete transaction under the same per-build-tree lock as tags generation.
if [[ "$lock_held" == false ]]; then
  mkdir -p -- "$(dirname -- "$generation_lock")"
  exec "$flock_exe" --exclusive --close "$generation_lock" \
    "$0" --ava-generation-lock-held \
    "$python_exe" "$generator" "$tags_json" "$output_dir" \
    "$source_signature" "$completion_marker" "$flock_exe" "$generation_lock"
fi

completion_tmp="${completion_marker}.tmp.$$"

cleanup() {
  rm -f -- "$completion_tmp"
}
trap cleanup EXIT

mkdir -p -- "$output_dir"
rm -f -- "$completion_marker"
# Capture the exact signature paired with the tags before Python starts. The
# build runner lock keeps those inputs stable, and this temp is not completion.
cat -- "$source_signature" >"$completion_tmp"
"$python_exe" "$generator" "$tags_json" "$output_dir"
mv -f -- "$completion_tmp" "$completion_marker"
trap - EXIT
