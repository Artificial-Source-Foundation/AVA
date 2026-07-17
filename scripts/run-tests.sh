#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'
umask 077

readonly SOURCE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
build_dir="$SOURCE_ROOT/build"
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

die() {
  printf 'error: %s\n' "$*" >&2
  exit 2
}

is_positive_integer() {
  [[ $1 =~ ^[1-9][0-9]*$ ]]
}

detect_jobs() {
  local detected=""
  if command -v nproc >/dev/null 2>&1; then
    detected=$(nproc 2>/dev/null || true)
  fi
  if ! is_positive_integer "$detected" && command -v getconf >/dev/null 2>&1; then
    detected=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
  fi
  if ! is_positive_integer "$detected" && command -v sysctl >/dev/null 2>&1; then
    detected=$(sysctl -n hw.logicalcpu 2>/dev/null || true)
  fi
  if ! is_positive_integer "$detected" && is_positive_integer "${NUMBER_OF_PROCESSORS:-}"; then
    detected=$NUMBER_OF_PROCESSORS
  fi
  if ! is_positive_integer "$detected"; then
    detected=2
  fi
  printf '%s\n' "$detected"
}

while (($# > 0)); do
  case $1 in
    --build-dir|--test-dir)
      (($# >= 2)) || die "$1 requires a directory"
      build_dir=$2
      shift 2
      ;;
    --build-dir=*|--test-dir=*)
      build_dir=${1#*=}
      shift
      ;;
    --jobs|-j|--parallel)
      (($# >= 2)) || die "$1 requires a positive integer"
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
      die "use --build-dir with this runner so it can lock the selected build tree"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      ctest_args+=("$@")
      break
      ;;
    *)
      ctest_args+=("$1")
      shift
      ;;
  esac
done

[[ -n $build_dir ]] || die "--build-dir must not be empty"
[[ -d $build_dir ]] || die "build directory does not exist: $build_dir"
build_dir=$(cd -- "$build_dir" && pwd -P)
[[ -f $build_dir/CTestTestfile.cmake ]] || die "build directory is not configured for CTest: $build_dir"

if [[ -z $jobs ]]; then
  if [[ ${CTEST_PARALLEL_LEVEL+x} == x ]]; then
    jobs=$CTEST_PARALLEL_LEVEL
  else
    jobs=$(detect_jobs)
  fi
fi
is_positive_integer "$jobs" || die "parallel job count must be a positive integer (got '${jobs:-<empty>}')"

ctest_command=${AVA_CTEST_COMMAND:-ctest}
if [[ $ctest_command == */* ]]; then
  [[ -x $ctest_command ]] || die "CTest command is not executable: $ctest_command"
else
  command -v "$ctest_command" >/dev/null 2>&1 || die "CTest command is unavailable: $ctest_command"
fi

lock_file=$build_dir/.ava-ctest.lock
[[ ! -L $lock_file ]] || die "test lock path must not be a symlink: $lock_file"
if command -v flock >/dev/null 2>&1; then
  exec {lock_fd}>"$lock_file"
  flock -n "$lock_fd" || die "another AVA test run already owns build tree: $build_dir"
else
  lock_dir=${lock_file}.d
  lock_owned=false
  release_lock() {
    if [[ $lock_owned == true ]]; then
      rm -f -- "$lock_dir/pid"
      rmdir -- "$lock_dir" 2>/dev/null || true
    fi
  }
  trap release_lock EXIT
  if ! mkdir -- "$lock_dir" 2>/dev/null; then
    [[ ! -L $lock_dir && -d $lock_dir ]] || die "test lock path is not a real directory: $lock_dir"
    owner=""
    if [[ ! -L $lock_dir/pid && -f $lock_dir/pid ]]; then
      read -r owner <"$lock_dir/pid" || true
    fi
    if is_positive_integer "$owner" && ! kill -0 "$owner" 2>/dev/null; then
      rm -f -- "$lock_dir/pid"
      rmdir -- "$lock_dir" 2>/dev/null || die "stale test lock contains unexpected files: $lock_dir"
      mkdir -- "$lock_dir" || die "another AVA test run acquired build tree: $build_dir"
    else
      die "another AVA test run already owns build tree: $build_dir${owner:+ (pid $owner)}"
    fi
  fi
  lock_owned=true
  printf '%s\n' "$$" >"$lock_dir/pid"
fi

printf 'Running AVA tests with %s parallel jobs in %s\n' "$jobs" "$build_dir"
"$ctest_command" --test-dir "$build_dir" --output-on-failure --parallel "$jobs" "${ctest_args[@]}"
