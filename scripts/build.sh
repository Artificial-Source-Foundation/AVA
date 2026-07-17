#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'
umask 077

readonly SOURCE_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
# shellcheck source=parallel-runner-common.sh
source "$SOURCE_ROOT/scripts/parallel-runner-common.sh"

build_dir=$SOURCE_ROOT/build
jobs=""
build_args=()

usage() {
  cat <<'EOF'
Usage: scripts/build.sh [--build-dir DIR] [--jobs N] [CMAKE_BUILD_OPTIONS...]

Build one configured AVA tree in parallel. The default build is ./build and the
default job count is the machine's available logical cores. Set
CMAKE_BUILD_PARALLEL_LEVEL or pass --jobs to override that count. Remaining
arguments, such as --target ava_tests, are forwarded to `cmake --build`.

Examples:
  scripts/build.sh
  scripts/build.sh --target ava ava_tests
  scripts/build.sh --build-dir build-sanitize --jobs 2 --target ava_tests
EOF
}

while (($# > 0)); do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --build-dir)
      (($# >= 2)) || ava_parallel_die "--build-dir requires a directory"
      build_dir=$2
      shift 2
      ;;
    --build-dir=*)
      build_dir=${1#*=}
      shift
      ;;
    --jobs|-j|--parallel)
      (($# >= 2)) || ava_parallel_die "$1 requires a positive integer"
      jobs=$2
      shift 2
      ;;
    --jobs=*|--parallel=*)
      jobs=${1#*=}
      shift
      ;;
    -j[0-9]*)
      jobs=${1#-j}
      shift
      ;;
    --preset|--preset=*)
      ava_parallel_die "use --build-dir with this runner so it can lock the selected build tree"
      ;;
    --)
      shift
      build_args+=("$@")
      break
      ;;
    *)
      build_args+=("$1")
      shift
      ;;
  esac
done

[[ -n $build_dir ]] || ava_parallel_die "--build-dir must not be empty"
[[ -d $build_dir ]] || ava_parallel_die "build directory does not exist: $build_dir"
build_dir=$(cd -- "$build_dir" && pwd -P)
[[ -f $build_dir/CMakeCache.txt ]] || ava_parallel_die "build directory is not configured by CMake: $build_dir"

if [[ -z $jobs ]]; then
  if [[ ${CMAKE_BUILD_PARALLEL_LEVEL+x} == x ]]; then
    jobs=$CMAKE_BUILD_PARALLEL_LEVEL
  else
    jobs=$(ava_parallel_detect_jobs)
  fi
fi
ava_parallel_is_positive_integer "$jobs" ||
  ava_parallel_die "parallel job count must be a positive integer (got '${jobs:-<empty>}')"

cmake_command=${AVA_CMAKE_COMMAND:-cmake}
if [[ $cmake_command == */* ]]; then
  [[ -x $cmake_command ]] || ava_parallel_die "CMake command is not executable: $cmake_command"
else
  command -v "$cmake_command" >/dev/null 2>&1 || ava_parallel_die "CMake command is unavailable: $cmake_command"
fi

ava_parallel_acquire_tree_lock "$build_dir"
printf 'Building AVA with %s parallel jobs in %s\n' "$jobs" "$build_dir"
ava_parallel_run_locked "$cmake_command" --build "$build_dir" --parallel "$jobs" "${build_args[@]}"
