#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

# Guard: reject C++ module scanner flags in the effective Ninja build
# commands of a configured build tree.
#
# AVA CI configures its ccache-backed build trees with
# -DCMAKE_CXX_SCAN_FOR_MODULES=OFF because ccache 4.9.1 does not support
# caching compilations that carry module dependency-scanning flags
# (-fmodules-ts, -fmodule-mapper=..., -fdeps-format=...): ccache refuses
# them and the affected objects compile uncached, which is an unsupported
# cache configuration for CI rather than a correctness hazard. CMake only
# stops emitting those flags reliably when no target scans; an explicitly
# declared CXX_MODULES FILE_SET scans even with the variable OFF, so this
# guard inspects the effective `ninja -t commands` output instead of
# trusting the configure-time variable.
#
# Strict packaging trees are intentionally excluded: package-linux.sh does a
# fresh configure with default module scanning and CCACHE_DISABLE=1.
#
# Usage: scripts/guard-no-cxx-modules.sh BUILD_DIR
# Environment: NINJA overrides the ninja executable (default: ninja).

usage() {
  printf 'usage: %s BUILD_DIR\n' "${0##*/}" >&2
  exit 2
}

(($# == 1)) || usage
build_dir=$1
[[ -n $build_dir && -d $build_dir ]] || {
  printf 'error: build directory does not exist: %s\n' "$build_dir" >&2
  exit 2
}

ninja=${NINJA:-ninja}

# Do not hide ninja failures: without the effective command list there is
# nothing trustworthy to inspect, so any ninja error aborts the guard.
commands=$(command "$ninja" -C "$build_dir" -t commands)

matches=""
grep_status=0
matches=$(printf '%s\n' "$commands" | grep -n -E -- '-fmodules|-fmodule-|-fdeps-') || grep_status=$?
# grep exit 1 means "no match" (the guard passes); anything above 1 is a
# real grep failure and must not be hidden either.
if ((grep_status > 1)); then
  printf 'error: grep failed with status %d while inspecting ninja commands\n' "$grep_status" >&2
  exit "$grep_status"
fi
if ((grep_status == 0)); then
  printf 'error: %s emits C++ module scanner flags unsupported by the CI compiler cache:\n' "$build_dir" >&2
  printf '%s\n' "$matches" >&2
  printf '%s\n' 'error: configure cacheable CI trees with -DCMAKE_CXX_SCAN_FOR_MODULES=OFF;' >&2
  printf '%s\n' 'error: an explicit CXX_MODULES FILE_SET still scans and remains unsupported here.' >&2
  exit 1
fi
printf 'module-flag guard: no -fmodules*/-fmodule-*/-fdeps-* flags in %s\n' "$build_dir"
