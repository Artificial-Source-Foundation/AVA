#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

readonly SOURCE_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
# shellcheck source=parallel-runner-common.sh
source "$SOURCE_ROOT/scripts/parallel-runner-common.sh"

build_dir=$SOURCE_ROOT/build
jobs=""
ctest_args=()

usage() {
  cat <<'EOF'
Usage: scripts/run-tests.sh [--build-dir DIR] [--jobs N] [CTEST_OPTIONS...]

Run one configured AVA build tree's CTest suite in parallel. The default build
is ./build and the default job count is the machine's available logical cores.
Set CTEST_PARALLEL_LEVEL or pass --jobs to override that count. Remaining
arguments, such as -R REGEX, are forwarded to CTest.

Examples:
  scripts/run-tests.sh
  scripts/run-tests.sh -R '^ava_tests\.lsp$'
  scripts/run-tests.sh --build-dir build-sanitize --jobs 4
EOF
}

while (($# > 0)); do
  case $1 in
    --build-dir|--test-dir)
      (($# >= 2)) || ava_parallel_die "$1 requires a directory"
      build_dir=$2
      shift 2
      ;;
    --build-dir=*|--test-dir=*)
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
    --build-and-test|--build-and-test=*|--collect-instrumentation|--collect-instrumentation=*|-S|-S?*|--script|--script=*|-SP|-SP?*|--script-new-process|--script-new-process=*|-D|-D?*|--dashboard|--dashboard=*|-M|-M?*|--test-model|--test-model=*|-T|-T?*|--test-action|--test-action=*)
      ava_parallel_die "CTest build/script/dashboard/instrumentation modes are unsupported because they can bypass the locked build tree"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      ava_parallel_die "-- is unsupported because post-boundary CTest options could bypass the locked build tree"
      ;;
    *)
      ctest_args+=("$1")
      shift
      ;;
  esac
done

[[ -n $build_dir ]] || ava_parallel_die "--build-dir must not be empty"
[[ -d $build_dir ]] || ava_parallel_die "build directory does not exist: $build_dir"
build_dir=$(cd -- "$build_dir" && pwd -P)
[[ -f $build_dir/CTestTestfile.cmake ]] || ava_parallel_die "build directory is not configured for CTest: $build_dir"

if [[ -z $jobs ]]; then
  if [[ ${CTEST_PARALLEL_LEVEL+x} == x ]]; then
    jobs=$CTEST_PARALLEL_LEVEL
  else
    jobs=$(ava_parallel_detect_jobs)
  fi
fi
ava_parallel_is_positive_integer "$jobs" ||
  ava_parallel_die "parallel job count must be a positive integer (got '${jobs:-<empty>}')"

ctest_command=${AVA_CTEST_COMMAND:-ctest}
if [[ $ctest_command == */* ]]; then
  [[ -x $ctest_command ]] || ava_parallel_die "CTest command is not executable: $ctest_command"
else
  command -v "$ctest_command" >/dev/null 2>&1 || ava_parallel_die "CTest command is unavailable: $ctest_command"
fi

ava_parallel_acquire_tree_lock "$build_dir"
printf 'Running AVA tests with %s parallel jobs in %s\n' "$jobs" "$build_dir"
ava_parallel_run_locked "$ctest_command" --test-dir "$build_dir" --output-on-failure --parallel "$jobs" "${ctest_args[@]}"
