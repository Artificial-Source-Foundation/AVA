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
  local owner=""
  AVA_PARALLEL_LOCK_DIR=$build_dir/.ava-build-tree.lock.d
  AVA_PARALLEL_LOCK_OWNED=false

  if mkdir -m 0700 -- "$AVA_PARALLEL_LOCK_DIR" 2>/dev/null; then
    AVA_PARALLEL_LOCK_OWNED=true
    printf '%s\n' "$$" >"$AVA_PARALLEL_LOCK_DIR/pid"
    trap ava_parallel_release_tree_lock EXIT
    return
  fi

  [[ ! -L $AVA_PARALLEL_LOCK_DIR && -d $AVA_PARALLEL_LOCK_DIR ]] ||
    ava_parallel_die "build-tree lock path is not a real directory: $AVA_PARALLEL_LOCK_DIR"
  if [[ ! -L $AVA_PARALLEL_LOCK_DIR/pid && -f $AVA_PARALLEL_LOCK_DIR/pid ]]; then
    read -r owner <"$AVA_PARALLEL_LOCK_DIR/pid" || true
  fi
  if ava_parallel_is_positive_integer "$owner" && kill -0 "$owner" 2>/dev/null; then
    ava_parallel_die "another AVA build or test run already owns build tree: $build_dir (pid $owner)"
  fi
  ava_parallel_die "stale AVA build-tree lock requires manual recovery after verifying no worker remains: $AVA_PARALLEL_LOCK_DIR${owner:+ (former pid $owner)}"
}

ava_parallel_group_alive() {
  kill -0 -- "-$1" 2>/dev/null
}

ava_parallel_signal_group() {
  local pgid=$1
  local signal_name=$2
  if ava_parallel_group_alive "$pgid"; then
    kill -s "$signal_name" -- "-$pgid" 2>/dev/null || return 1
  fi
}

ava_parallel_wait_group_exit() {
  local pgid=$1
  local timeout_seconds=$2
  local deadline=$((SECONDS + timeout_seconds))
  while ava_parallel_group_alive "$pgid"; do
    if ((SECONDS >= deadline)); then
      return 1
    fi
    sleep 0.05
  done
}

ava_parallel_fail_closed() {
  AVA_PARALLEL_LOCK_OWNED=false
  ava_parallel_die "worker process group teardown was not proven; leaving build-tree lock fail-closed: $AVA_PARALLEL_LOCK_DIR"
}

ava_parallel_request_child_stop() {
  AVA_PARALLEL_REQUESTED_EXIT=$1
  if [[ -n ${AVA_PARALLEL_CHILD_PGID:-} ]]; then
    ava_parallel_signal_group "$AVA_PARALLEL_CHILD_PGID" TERM || ava_parallel_fail_closed
    if [[ -z ${AVA_PARALLEL_ESCALATION_PID:-} ]]; then
      (
        if ! ava_parallel_wait_group_exit "$AVA_PARALLEL_CHILD_PGID" 2; then
          ava_parallel_signal_group "$AVA_PARALLEL_CHILD_PGID" KILL ||
            kill -USR1 "$AVA_PARALLEL_WRAPPER_PID"
        fi
      ) &
      AVA_PARALLEL_ESCALATION_PID=$!
    fi
  fi
}

ava_parallel_run_locked() {
  # A private process group makes the worker PID its process-group ID. The lock is released only
  # after that whole group disappears; uncertain teardown leaves the directory fail-closed.

  AVA_PARALLEL_CHILD_PID=""
  AVA_PARALLEL_CHILD_PGID=""
  AVA_PARALLEL_REQUESTED_EXIT=0
  AVA_PARALLEL_SIGNAL_FAILED=false
  AVA_PARALLEL_ESCALATION_PID=""
  AVA_PARALLEL_WRAPPER_PID=$$
  trap 'ava_parallel_request_child_stop 129' HUP
  trap 'ava_parallel_request_child_stop 130' INT
  trap 'ava_parallel_request_child_stop 143' TERM
  trap ava_parallel_fail_closed USR1

  # Bash monitor mode assigns a private process group to a background job on supported Unix hosts.
  # Restore the caller's mode immediately; only the worker launch needs this behavior.
  local restore_monitor_mode=false
  if [[ $- != *m* ]]; then
    set -m
    restore_monitor_mode=true
  fi
  "$@" &
  AVA_PARALLEL_CHILD_PID=$!
  AVA_PARALLEL_CHILD_PGID=$AVA_PARALLEL_CHILD_PID
  if [[ $restore_monitor_mode == true ]]; then
    set +m
  fi
  if ((AVA_PARALLEL_REQUESTED_EXIT != 0)); then
    ava_parallel_request_child_stop "$AVA_PARALLEL_REQUESTED_EXIT"
  fi

  local child_status
  set +e
  while true; do
    wait "$AVA_PARALLEL_CHILD_PID"
    child_status=$?
    kill -0 "$AVA_PARALLEL_CHILD_PID" 2>/dev/null || break
  done
  set -e

  if [[ -n $AVA_PARALLEL_ESCALATION_PID ]]; then
    set +e
    while kill -0 "$AVA_PARALLEL_ESCALATION_PID" 2>/dev/null; do
      wait "$AVA_PARALLEL_ESCALATION_PID"
    done
    wait "$AVA_PARALLEL_ESCALATION_PID" 2>/dev/null
    set -e
  fi

  if ava_parallel_group_alive "$AVA_PARALLEL_CHILD_PGID"; then
    ava_parallel_signal_group "$AVA_PARALLEL_CHILD_PGID" TERM || AVA_PARALLEL_SIGNAL_FAILED=true
    if ! ava_parallel_wait_group_exit "$AVA_PARALLEL_CHILD_PGID" 2; then
      ava_parallel_signal_group "$AVA_PARALLEL_CHILD_PGID" KILL || AVA_PARALLEL_SIGNAL_FAILED=true
      ava_parallel_wait_group_exit "$AVA_PARALLEL_CHILD_PGID" 2 || AVA_PARALLEL_SIGNAL_FAILED=true
    fi
  fi

  if [[ $AVA_PARALLEL_SIGNAL_FAILED == true ]] || ava_parallel_group_alive "$AVA_PARALLEL_CHILD_PGID"; then
    AVA_PARALLEL_LOCK_OWNED=false
    ava_parallel_die "worker process group teardown was not proven; leaving build-tree lock fail-closed: $AVA_PARALLEL_LOCK_DIR"
  fi
  if ((AVA_PARALLEL_REQUESTED_EXIT != 0)); then
    exit "$AVA_PARALLEL_REQUESTED_EXIT"
  fi
  exit "$child_status"
}
