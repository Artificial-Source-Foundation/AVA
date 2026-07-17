#!/usr/bin/env bash

ava_parallel_die() {
  printf 'error: %s\n' "$*" >&2
  exit 2
}

ava_parallel_is_positive_integer() {
  [[ $1 =~ ^[1-9][0-9]*$ ]]
}

ava_parallel_detect_jobs() {
  local detected=""
  if command -v nproc >/dev/null 2>&1; then
    detected=$(nproc 2>/dev/null || true)
  fi
  if ! ava_parallel_is_positive_integer "$detected" && command -v getconf >/dev/null 2>&1; then
    detected=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
  fi
  if ! ava_parallel_is_positive_integer "$detected" && command -v sysctl >/dev/null 2>&1; then
    detected=$(sysctl -n hw.logicalcpu 2>/dev/null || true)
  fi
  if ! ava_parallel_is_positive_integer "$detected" && ava_parallel_is_positive_integer "${NUMBER_OF_PROCESSORS:-}"; then
    detected=$NUMBER_OF_PROCESSORS
  fi
  if ! ava_parallel_is_positive_integer "$detected"; then
    detected=1
  fi
  printf '%s\n' "$detected"
}

ava_parallel_release_tree_lock() {
  if [[ ${AVA_PARALLEL_LOCK_OWNED:-false} == true ]]; then
    rm -f -- "$AVA_PARALLEL_LOCK_DIR/pid"
    rmdir -- "$AVA_PARALLEL_LOCK_DIR" 2>/dev/null || true
  fi
}

ava_parallel_acquire_tree_lock() {
  local build_dir=$1
  local lock_file=$build_dir/.ava-build-tree.lock
  [[ ! -L $lock_file ]] || ava_parallel_die "build-tree lock path must not be a symlink: $lock_file"
  if [[ -e $lock_file && ! -f $lock_file ]]; then
    ava_parallel_die "build-tree lock path must be a regular file: $lock_file"
  fi

  AVA_PARALLEL_LOCK_MODE=directory
  if command -v flock >/dev/null 2>&1; then
    AVA_PARALLEL_LOCK_MODE=flock
    exec {AVA_PARALLEL_LOCK_FD}>"$lock_file"
    flock -n "$AVA_PARALLEL_LOCK_FD" || ava_parallel_die "another AVA build or test run already owns build tree: $build_dir"
    return
  fi

  AVA_PARALLEL_LOCK_DIR=${lock_file}.d
  AVA_PARALLEL_LOCK_OWNED=false
  trap ava_parallel_release_tree_lock EXIT
  if ! mkdir -- "$AVA_PARALLEL_LOCK_DIR" 2>/dev/null; then
    [[ ! -L $AVA_PARALLEL_LOCK_DIR && -d $AVA_PARALLEL_LOCK_DIR ]] ||
      ava_parallel_die "build-tree lock path is not a real directory: $AVA_PARALLEL_LOCK_DIR"
    local owner=""
    if [[ ! -L $AVA_PARALLEL_LOCK_DIR/pid && -f $AVA_PARALLEL_LOCK_DIR/pid ]]; then
      read -r owner <"$AVA_PARALLEL_LOCK_DIR/pid" || true
    fi
    if ava_parallel_is_positive_integer "$owner" && ! kill -0 "$owner" 2>/dev/null; then
      rm -f -- "$AVA_PARALLEL_LOCK_DIR/pid"
      rmdir -- "$AVA_PARALLEL_LOCK_DIR" 2>/dev/null ||
        ava_parallel_die "stale build-tree lock contains unexpected files: $AVA_PARALLEL_LOCK_DIR"
      mkdir -- "$AVA_PARALLEL_LOCK_DIR" ||
        ava_parallel_die "another AVA build or test run acquired build tree: $build_dir"
    else
      ava_parallel_die "another AVA build or test run already owns build tree: $build_dir${owner:+ (pid $owner)}"
    fi
  fi
  AVA_PARALLEL_LOCK_OWNED=true
  printf '%s\n' "$$" >"$AVA_PARALLEL_LOCK_DIR/pid"
}

ava_parallel_request_child_stop() {
  AVA_PARALLEL_REQUESTED_EXIT=$1
  if [[ -n ${AVA_PARALLEL_CHILD_PID:-} ]]; then
    kill -TERM "$AVA_PARALLEL_CHILD_PID" 2>/dev/null || true
  fi
}

ava_parallel_run_locked() {
  if [[ $AVA_PARALLEL_LOCK_MODE == flock ]]; then
    # Replacing the wrapper makes direct signals reach the worker while the inherited descriptor
    # keeps the build-tree lock held until it and any descriptor-inheriting children have exited.
    exec "$@"
  fi

  # The portable directory-lock fallback cannot exec because its EXIT trap removes the lock.
  # Forward termination and wait for the worker before allowing that cleanup to run.
  AVA_PARALLEL_CHILD_PID=""
  AVA_PARALLEL_REQUESTED_EXIT=0
  trap 'ava_parallel_request_child_stop 129' HUP
  trap 'ava_parallel_request_child_stop 130' INT
  trap 'ava_parallel_request_child_stop 143' TERM

  "$@" &
  AVA_PARALLEL_CHILD_PID=$!
  if ((AVA_PARALLEL_REQUESTED_EXIT != 0)); then
    kill -TERM "$AVA_PARALLEL_CHILD_PID" 2>/dev/null || true
  fi

  local child_status
  set +e
  while true; do
    wait "$AVA_PARALLEL_CHILD_PID"
    child_status=$?
    kill -0 "$AVA_PARALLEL_CHILD_PID" 2>/dev/null || break
  done
  set -e

  if ((AVA_PARALLEL_REQUESTED_EXIT != 0)); then
    exit "$AVA_PARALLEL_REQUESTED_EXIT"
  fi
  exit "$child_status"
}
