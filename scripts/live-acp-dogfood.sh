#!/usr/bin/env bash
set -Eeuo pipefail
IFS=$'\n\t'

readonly SCRIPT_PATH="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/$(basename -- "${BASH_SOURCE[0]}")"
readonly SOURCE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly FAKE_KEY="AVA_ZED_DOGFOOD_FAKE_KEY_NOT_A_SECRET"
readonly MAX_PREFLIGHT_BYTES=16384
readonly MAX_REPORT_BYTES=65536
readonly MAX_RAW_FILE_BLOCKS=16384 # 16 MiB with Bash's Linux 1024-byte blocks.
readonly MAX_ZED_FILE_BLOCKS=65536 # 64 MiB; Zed preallocates state files above the raw-log cap.
readonly DEFAULT_OPERATOR_TIMEOUT=600
readonly ZED_PHASE_TAG_NAME="AVA_ZED_DOGFOOD_PHASE_ROOT"
readonly MAX_PROC_SCAN_PIDS=131072
readonly MAX_PROC_ENVIRON_BYTES=4194304
readonly PROC_SCAN_DEADLINE_SECONDS=5

provider_pid=""
provider_port=""
zed_pid=""
zed_stdout_capture_pid=""
zed_stderr_capture_pid=""
zed_stdout_fifo=""
zed_stderr_fifo=""
active_phase=""
active_phase_root=""

die() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 2
}

usage() {
  cat >&2 <<'EOF'
Usage:
  scripts/live-acp-dogfood.sh sdk --build-dir /absolute/configured/build
  scripts/live-acp-dogfood.sh acpx --build-dir /absolute/configured/build
  scripts/live-acp-dogfood.sh zed run \
    --ava /absolute/path/to/ava \
    --fake-provider /absolute/path/to/ava_fake_provider_server \
    --zed /absolute/path/to/zed \
    (--display :NUMBER | --wayland-display /absolute/path/to/socket) \
    --acknowledge-dedicated-display \
    --confinement sandbox --confinement-description TEXT \
    --preflight-evidence /absolute/path/to/reviewed-preflight.txt
  scripts/live-acp-dogfood.sh zed run \
    --ava /absolute/path/to/ava \
    --fake-provider /absolute/path/to/ava_fake_provider_server \
    --zed /absolute/path/to/zed \
    (--display :NUMBER | --wayland-display /absolute/path/to/socket) \
    --acknowledge-dedicated-display \
    --confinement disposable --confinement-description TEXT \
    --acknowledge-disposable-credential-free
  scripts/live-acp-dogfood.sh zed sanitize-copy \
    --source /absolute/path/to/manually-reviewed-report.md \
    --destination /absolute/path/to/repo/docs/interop/evidence/report.md

Temporary HOME/XDG roots isolate state; they are not a sandbox. `zed run` refuses
an ordinary-account profile-only launch. No mode installs or downloads packages.
EOF
}

require_absolute_dir() {
  local value=$1 label=$2
  [[ $value == /* ]] || die "$label must be an absolute path: $value"
  [[ -d $value ]] || die "$label is not a directory: $value"
  realpath -e -- "$value"
}

require_absolute_executable() {
  local value=$1 label=$2 directory normalized
  [[ $value == /* ]] || die "$label must be an explicit absolute path: $value"
  [[ -f $value && -x $value ]] || die "$label is not an executable file: $value"
  directory=$(cd -- "$(dirname -- "$value")" && pwd -P)
  normalized="$directory/$(basename -- "$value")"
  [[ -f $normalized && -x $normalized ]] || die "$label did not resolve to an executable file: $value"
  # Preserve the explicitly supplied final symlink. Some launchers, including
  # Zed's CLI shim, use argv[0] to locate and invoke the real GUI binary.
  printf '%s\n' "$normalized"
}

run_ctest_gate() {
  local mode=$1 option=$2 test_regex=$3
  shift 3
  local build_dir=""
  while (($#)); do
    case $1 in
      --build-dir)
        (($# >= 2)) || die "$mode --build-dir requires a value"
        build_dir=$2
        shift 2
        ;;
      *) die "unknown $mode argument: $1" ;;
    esac
  done
  [[ -n $build_dir ]] || die "$mode requires --build-dir"
  build_dir=$(require_absolute_dir "$build_dir" "build directory")
  [[ -f $build_dir/CMakeCache.txt ]] || die "build directory has no CMakeCache.txt: $build_dir"
  grep -Fqx "${option}:BOOL=ON" "$build_dir/CMakeCache.txt" ||
    die "$mode requires a build configured with -D${option}=ON"
  command -v ctest >/dev/null 2>&1 || die "ctest is not available"

  # This invokes only the already-configured CTest gate. The gate itself owns
  # its exact AVA/provider/package arguments; this script never runs npm.
  ctest --test-dir "$build_dir" --output-on-failure --no-tests=error -R "$test_regex"
}

process_group_alive() {
  local pid=$1
  kill -0 -- "-$pid" 2>/dev/null
}

process_alive_non_zombie() {
  local pid=$1 state
  kill -0 -- "$pid" 2>/dev/null || return 1
  state=$(ps -o stat= -p "$pid" 2>/dev/null || true)
  [[ -n $state && $state != Z* ]]
}

wait_for_group_exit() {
  local pid=$1 timeout_seconds=$2 state
  local deadline=$((SECONDS + timeout_seconds))
  while process_group_alive "$pid" && ((SECONDS < deadline)); do
    state=$(ps -o stat= -p "$pid" 2>/dev/null || true)
    if [[ $state == Z* ]]; then
      wait "$pid" 2>/dev/null || true
    fi
    sleep 0.05
  done
  ! process_group_alive "$pid"
}

terminate_group() {
  local pid=${1:-}
  [[ -n $pid ]] || return 0
  if process_group_alive "$pid"; then
    kill -TERM -- "-$pid" 2>/dev/null || true
    if ! wait_for_group_exit "$pid" 2; then
      kill -KILL -- "-$pid" 2>/dev/null || true
      if ! wait_for_group_exit "$pid" 3; then
        printf 'ERROR: owned process group %s survived SIGKILL\n' "$pid" >&2
        return 1
      fi
    fi
  elif kill -0 "$pid" 2>/dev/null; then
    # Cover the tiny interval before a newly spawned `setsid` call establishes
    # its group. No child is admitted before the startup preflight completes.
    kill -TERM -- "$pid" 2>/dev/null || true
    local deadline=$((SECONDS + 2))
    while kill -0 "$pid" 2>/dev/null && ((SECONDS < deadline)); do sleep 0.05; done
    if kill -0 "$pid" 2>/dev/null; then kill -KILL -- "$pid" 2>/dev/null || true; fi
  fi
  wait "$pid" 2>/dev/null || true
}

scan_phase_tagged_processes() {
  local phase_root=${1:-} action=${2:-scan}
  [[ -n $phase_root ]] || { printf 'ERROR: phase process scan requires a phase root\n' >&2; return 1; }
  [[ $action == scan || $action == term || $action == kill ]] || {
    printf 'ERROR: invalid phase process scan action: %s\n' "$action" >&2
    return 1
  }

  python3 - "$ZED_PHASE_TAG_NAME" "$phase_root" "$action" "$$" \
    "$MAX_PROC_SCAN_PIDS" "$MAX_PROC_ENVIRON_BYTES" "$PROC_SCAN_DEADLINE_SECONDS" <<'PY'
import os
import select
import signal
import sys
import time


def fail(message):
    print(f"ERROR: bounded phase process scan failed: {message}", file=sys.stderr)
    raise SystemExit(1)


tag_name, phase_root, action, launcher_pid_text, max_pids_text, max_environ_text, deadline_text = sys.argv[1:]
if not hasattr(os, "pidfd_open") or not hasattr(signal, "pidfd_send_signal"):
    fail("Python/Linux pidfd support is required")
try:
    launcher_pid = int(launcher_pid_text)
    max_pids = int(max_pids_text)
    max_environ = int(max_environ_text)
    deadline = time.monotonic() + int(deadline_text)
except ValueError:
    fail("invalid numeric bound")

tag = os.fsencode(f"{tag_name}={phase_root}")
signal_number = {
    "scan": None,
    "term": signal.SIGTERM,
    "kill": signal.SIGKILL,
}[action]
matches = []
scanned = 0

try:
    proc_entries = os.scandir("/proc")
except OSError as error:
    fail(f"cannot open /proc: {error.strerror or error}")

with proc_entries:
    for entry in proc_entries:
        if time.monotonic() > deadline:
            fail("deadline exceeded")
        if not entry.name.isascii() or not entry.name.isdecimal():
            continue
        scanned += 1
        if scanned > max_pids:
            fail(f"more than {max_pids} PID entries")
        pid = int(entry.name)
        if pid == launcher_pid:
            continue
        try:
            pidfd = os.pidfd_open(pid)
        except (ProcessLookupError, PermissionError):
            # Exited and inaccessible processes are expected while walking /proc.
            continue
        except OSError:
            continue
        try:
            try:
                with open(f"/proc/{pid}/environ", "rb", buffering=0) as environment_file:
                    environment = environment_file.read(max_environ + 1)
            except (FileNotFoundError, ProcessLookupError, PermissionError, OSError):
                continue
            if len(environment) > max_environ:
                fail(f"/proc/{pid}/environ exceeded {max_environ} bytes")
            if tag not in environment.split(b"\0"):
                continue

            poller = select.poll()
            poller.register(pidfd, select.POLLIN)
            if poller.poll(0):
                continue
            if signal_number is not None:
                try:
                    signal.pidfd_send_signal(pidfd, signal_number)
                except ProcessLookupError:
                    continue
                except (PermissionError, OSError) as error:
                    fail(f"cannot signal tagged PID {pid}: {error}")
            matches.append(pid)
        finally:
            os.close(pidfd)

for pid in sorted(matches):
    print(pid)
PY
}

wait_for_no_phase_tagged_processes() {
  local phase_root=$1 timeout_seconds=$2 remaining
  local deadline=$((SECONDS + timeout_seconds))
  while ((SECONDS < deadline)); do
    if ! remaining=$(scan_phase_tagged_processes "$phase_root" scan); then
      return 2
    fi
    [[ -z $remaining ]] && return 0
    sleep 0.05
  done
  if ! remaining=$(scan_phase_tagged_processes "$phase_root" scan); then
    return 2
  fi
  [[ -z $remaining ]]
}

cleanup_phase_tagged_processes() {
  local phase_root=$1 remaining wait_status pids
  if ! scan_phase_tagged_processes "$phase_root" term >/dev/null; then
    return 1
  fi
  if wait_for_no_phase_tagged_processes "$phase_root" 2; then
    :
  else
    wait_status=$?
    ((wait_status == 1)) || return 1
    # Re-scan before each signal so exited/reused PIDs are never targeted by a
    # stale PID. pidfds then keep the exact matching process stable for signal.
    if ! scan_phase_tagged_processes "$phase_root" term >/dev/null; then
      return 1
    fi
    sleep 0.1
    if ! scan_phase_tagged_processes "$phase_root" kill >/dev/null; then
      return 1
    fi
    if wait_for_no_phase_tagged_processes "$phase_root" 3; then
      :
    else
      wait_status=$?
      ((wait_status == 1)) || return 1
    fi
  fi

  # This is the bounded residual proof. A phase is never reported clean merely
  # because signals were sent.
  if ! remaining=$(scan_phase_tagged_processes "$phase_root" scan); then
    return 1
  fi
  if [[ -n $remaining ]]; then
    pids=${remaining//$'\n'/,}
    printf 'ERROR: exact phase-tagged PID(s) survived cleanup: %.1024s\n' "$pids" >&2
    return 1
  fi
}

wait_for_capture_exit() {
  local pid=${1:-} label=$2 state wait_status=0
  [[ -n $pid ]] || return 0
  local deadline=$((SECONDS + 3))
  while kill -0 "$pid" 2>/dev/null && ((SECONDS < deadline)); do
    state=$(ps -o stat= -p "$pid" 2>/dev/null || true)
    [[ $state == Z* ]] && break
    sleep 0.05
  done
  state=$(ps -o stat= -p "$pid" 2>/dev/null || true)
  if kill -0 "$pid" 2>/dev/null && [[ $state != Z* ]]; then
    kill -TERM "$pid" 2>/dev/null || true
    sleep 0.1
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    printf 'ERROR: %s capture did not drain after all tagged producers exited\n' "$label" >&2
    return 1
  fi
  if wait "$pid" 2>/dev/null; then
    wait_status=0
  else
    wait_status=$?
  fi
  if ((wait_status != 0)); then
    printf 'ERROR: %s capture exited nonzero (status=%s); its raw artifact may be truncated or capped\n' "$label" "$wait_status" >&2
    return 1
  fi
}

cleanup_zed_captures() {
  local status=0
  wait_for_capture_exit "$zed_stdout_capture_pid" stdout || status=1
  wait_for_capture_exit "$zed_stderr_capture_pid" stderr || status=1
  zed_stdout_capture_pid=""
  zed_stderr_capture_pid=""
  [[ -z $zed_stdout_fifo ]] || rm -f -- "$zed_stdout_fifo"
  [[ -z $zed_stderr_fifo ]] || rm -f -- "$zed_stderr_fifo"
  zed_stdout_fifo=""
  zed_stderr_fifo=""
  return "$status"
}

cleanup_zed_phase_processes() {
  local phase_root=${1:-} status=0
  terminate_group "$zed_pid" || status=1
  zed_pid=""
  if [[ -n $phase_root ]] && ! cleanup_phase_tagged_processes "$phase_root"; then
    status=1
  fi
  # Captures use separately tracked direct-child PIDs instead of the producer
  # tag. This lets them drain pipe tails after every tagged Zed descendant has
  # exited, and makes a raw-file cap or capture failure fail the phase.
  cleanup_zed_captures || status=1
  return "$status"
}

cleanup_active_processes() {
  local status=$? phase_root=$active_phase_root
  trap - EXIT INT TERM HUP
  cleanup_zed_phase_processes "$phase_root" || status=1
  terminate_group "$provider_pid" || status=1
  provider_pid=""
  exit "$status"
}

is_forbidden_environment_name() {
  local name=$1
  case $name in
    SSH_AUTH_SOCK|SSH_AGENT_PID|GPG_AGENT_INFO|DBUS_SESSION_BUS_ADDRESS|GNOME_KEYRING_CONTROL|GNOME_KEYRING_PID|KRB5CCNAME|KDEWALLET5_BINARY|KDE_WALLET_PASSWORD)
      return 0
      ;;
    GIT_ASKPASS|SSH_ASKPASS|GIT_SSH|GIT_SSH_COMMAND|GIT_CONFIG_GLOBAL|GIT_CONFIG_SYSTEM|GIT_CONFIG_COUNT|GITHUB_TOKEN|GH_TOKEN|GITLAB_TOKEN|CI_JOB_TOKEN|NODE_AUTH_TOKEN|NPM_TOKEN|NPM_CONFIG_USERCONFIG)
      return 0
      ;;
  esac
  if [[ $name =~ ^(AWS|AZURE|GOOGLE|GCP|CLOUDSDK|OPENAI|ANTHROPIC|GEMINI|DEEPSEEK|MOONSHOT|KIMI|OPENROUTER|CLOUDFLARE|HF|HUGGINGFACE|GITHUB|GITLAB|NPM|npm_config)_ ]] &&
     [[ $name =~ (KEY|TOKEN|SECRET|PASS|AUTH|CREDENTIAL|PROFILE|CONFIG|ACCOUNT) ]]; then
    return 0
  fi
  [[ $name =~ (_API_KEY|_ACCESS_KEY|_ACCESS_KEY_ID|_SECRET_ACCESS_KEY|_TOKEN|_AUTH_TOKEN|_PASSWORD|_CREDENTIAL|_CREDENTIALS)$ ]]
}

forbidden_environment_names() {
  local name
  while IFS= read -r name; do
    if is_forbidden_environment_name "$name"; then
      printf '%s\n' "$name"
    fi
  done < <(compgen -e | LC_ALL=C sort -u)
}

print_zed_reproduction() {
  local ava=${1:-/ABSOLUTE/PATH/TO/ava}
  local provider=${2:-/ABSOLUTE/PATH/TO/ava_fake_provider_server}
  local zed=${3:-/ABSOLUTE/PATH/TO/zed}
  cat >&2 <<EOF
Reproduction after provisioning a reviewed confinement boundary and a dedicated display:
  env -i HOME=/tmp/ava-dogfood-launcher-home PATH=/usr/bin:/bin TMPDIR=/tmp \\
    $(printf '%q' "$SCRIPT_PATH") zed run \\
    --ava $(printf '%q' "$ava") \\
    --fake-provider $(printf '%q' "$provider") \\
    --zed $(printf '%q' "$zed") \\
    --display :99 --acknowledge-dedicated-display \\
    --confinement sandbox \\
    --confinement-description 'REPLACE WITH REVIEWED BOUNDARY DESCRIPTION' \\
    --preflight-evidence /absolute/path/to/reviewed-preflight.txt
For an already-provisioned disposable credential-free graphical account/VM, use
  --confinement disposable --acknowledge-disposable-credential-free
instead of the sandbox preflight-evidence arguments. Do not use either declaration
from an ordinary account merely to bypass this check. For a dedicated nested
Wayland compositor, replace --display :99 with an absolute --wayland-display
socket path that is visible inside the confinement boundary.
EOF
}

write_model_config() {
  local phase_root=$1
  mkdir -p -- "$phase_root/xdg-config/ava"
  cat >"$phase_root/xdg-config/ava/models.json" <<'EOF'
{
  "default_provider": "moonshot",
  "default_model": "zed-dogfood",
  "models": [
    {
      "provider": "moonshot",
      "id": "zed-dogfood",
      "name": "Zed confined dogfood",
      "family": "fake",
      "context_window_tokens": 8192,
      "max_output_tokens": 1024,
      "supports_tools": true,
      "supports_streaming": false,
      "input_modalities": ["text"],
      "output_modalities": ["text"]
    }
  ]
}
EOF
  chmod 600 "$phase_root/xdg-config/ava/models.json"
}

write_zed_settings() {
  local phase_root=$1 ava=$2 provider_port=$3 phase=$4
  mkdir -p -- "$phase_root/xdg-config/zed"
  python3 - "$phase_root/xdg-config/zed/settings.json" "$ava" "$provider_port" "$phase" "$FAKE_KEY" <<'PY'
import json
from pathlib import Path
import sys

path, ava, port, phase, fake_key = sys.argv[1:]
settings = {
    "agent_servers": {
        f"AVA M6 dogfood ({phase})": {
            "type": "custom",
            "command": ava,
            "args": ["--acp"],
            "env": {
                "MOONSHOT_BASE_URL": f"http://127.0.0.1:{port}",
                "MOONSHOT_API_KEY": fake_key,
            },
        }
    }
}
Path(path).write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
PY
  chmod 600 "$phase_root/xdg-config/zed/settings.json"
}

prepare_phase_root() {
  local root=$1 phase=$2
  local phase_root="$root/phases/$phase"
  mkdir -p -- "$phase_root"/{home,xdg-config,xdg-data,xdg-state,xdg-cache,xdg-runtime,tmp,workspace,raw/screenshots,provider}
  chmod 700 "$phase_root" "$phase_root"/{home,xdg-config,xdg-data,xdg-state,xdg-cache,xdg-runtime,tmp,workspace,raw,raw/screenshots,provider}
  write_model_config "$phase_root"
  printf '%s\n' "$phase_root"
}

wait_for_provider_port() {
  local port_file=$1 pid=$2
  local deadline=$((SECONDS + 5)) value
  while ((SECONDS < deadline)); do
    if [[ -s $port_file ]]; then
      IFS= read -r value <"$port_file"
      if [[ $value =~ ^[0-9]+$ ]] && ((value > 0 && value <= 65535)); then
        printf '%s\n' "$value"
        return 0
      fi
    fi
    if ! process_group_alive "$pid" && ! kill -0 "$pid" 2>/dev/null; then
      wait "$pid" 2>/dev/null || true
      die "fake provider exited before publishing its loopback port for phase $active_phase"
    fi
    sleep 0.05
  done
  die "fake provider startup exceeded 5 seconds for phase $active_phase"
}

start_provider() {
  local phase_root=$1 fake_provider=$2 delay_ms=$3 scenario=$4 target=$5
  local port_file="$phase_root/provider/port"
  local request_log="$phase_root/raw/provider-requests.log"
  (
    ulimit -f "$MAX_RAW_FILE_BLOCKS"
    exec setsid env -i \
      HOME="$phase_root/home" \
      XDG_CONFIG_HOME="$phase_root/xdg-config" \
      XDG_DATA_HOME="$phase_root/xdg-data" \
      XDG_STATE_HOME="$phase_root/xdg-state" \
      XDG_CACHE_HOME="$phase_root/xdg-cache" \
      XDG_RUNTIME_DIR="$phase_root/xdg-runtime" \
      TMPDIR="$phase_root/tmp" \
      PATH=/usr/bin:/bin LANG=C.UTF-8 LC_ALL=C.UTF-8 \
      "$fake_provider" "$port_file" "$request_log" "$delay_ms" "$scenario" "$target"
  ) >"$phase_root/raw/provider.stdout" 2>"$phase_root/raw/provider.stderr" &
  provider_pid=$!
  provider_port=$(wait_for_provider_port "$port_file" "$provider_pid")
}

start_zed() {
  local phase_root=$1 zed=$2 display_kind=$3 display=$4 display_auth=$5 workspace=$6
  local -a environment=(
    HOME="$phase_root/home"
    XDG_CONFIG_HOME="$phase_root/xdg-config"
    XDG_DATA_HOME="$phase_root/xdg-data"
    XDG_STATE_HOME="$phase_root/xdg-state"
    XDG_CACHE_HOME="$phase_root/xdg-cache"
    XDG_RUNTIME_DIR="$phase_root/xdg-runtime"
    TMPDIR="$phase_root/tmp"
    PATH=/usr/local/bin:/usr/bin:/bin
    LANG=C.UTF-8
    LC_ALL=C.UTF-8
    AVA_ZED_DOGFOOD_PHASE_ROOT="$phase_root"
    ZED_ALLOW_EMULATED_GPU=1
    NO_COLOR=1
  )
  if [[ $display_kind == x11 ]]; then
    environment+=(DISPLAY="$display")
    if [[ -n $display_auth ]]; then
      environment+=(XAUTHORITY="$display_auth")
    fi
  else
    environment+=(WAYLAND_DISPLAY="$display" XDG_SESSION_TYPE=wayland)
  fi
  zed_stdout_fifo="$phase_root/raw/zed.stdout.pipe"
  zed_stderr_fifo="$phase_root/raw/zed.stderr.pipe"
  mkfifo -- "$zed_stdout_fifo" "$zed_stderr_fifo"
  chmod 600 "$zed_stdout_fifo" "$zed_stderr_fifo"
  (
    ulimit -f "$MAX_RAW_FILE_BLOCKS"
    exec env -i PATH=/usr/bin:/bin LANG=C.UTF-8 LC_ALL=C.UTF-8 cat "$zed_stdout_fifo"
  ) >"$phase_root/raw/zed.stdout" &
  zed_stdout_capture_pid=$!
  (
    ulimit -f "$MAX_RAW_FILE_BLOCKS"
    exec env -i PATH=/usr/bin:/bin LANG=C.UTF-8 LC_ALL=C.UTF-8 cat "$zed_stderr_fifo"
  ) >"$phase_root/raw/zed.stderr" &
  zed_stderr_capture_pid=$!
  (
    ulimit -f "$MAX_ZED_FILE_BLOCKS"
    exec env -i "${environment[@]}" "$zed" --foreground "$workspace"
  ) >"$zed_stdout_fifo" 2>"$zed_stderr_fifo" &
  zed_pid=$!
  sleep 1
  if ! process_alive_non_zombie "$zed_pid"; then
    local observed_state observed_children
    observed_state=$(ps -o stat= -p "$zed_pid" 2>/dev/null || true)
    observed_children=$(ps -o pid=,ppid=,stat=,comm= --ppid "$zed_pid" 2>/dev/null | tr '\n' ';' || true)
    wait "$zed_pid" 2>/dev/null || true
    die "Zed launcher exited or became a zombie during startup for phase $active_phase (state=${observed_state:-absent}; children=${observed_children:-none}); inspect $phase_root/raw/zed.stderr"
  fi
  # Zed's CLI foreground launcher must remain in the caller's process group;
  # forcing a separate session causes the launcher to return after GUI handoff.
  # Cleanup binds every descendant to the exact inherited phase tag instead.
}

record_operator_outcome() {
  local root=$1 phase=$2 timeout=$3
  local outcome observation
  printf '\nRecord %s phase outcome (pass, fail, or incomplete) within %s seconds: ' "$phase" "$timeout" >&2
  if ! IFS= read -r -t "$timeout" outcome; then
    outcome=incomplete
    observation="operator input was absent or timed out"
  else
    case $outcome in
      pass|fail|incomplete) ;;
      *)
        observation="invalid phase outcome supplied: ${outcome:0:120}"
        outcome=incomplete
        ;;
    esac
    if [[ -z ${observation:-} ]]; then
      printf 'One-line observed evidence (max 1000 characters; no credentials or raw logs): ' >&2
      if ! IFS= read -r -t 120 observation; then
        observation="no operator observation recorded"
        outcome=incomplete
      elif [[ -z $observation ]]; then
        observation="operator supplied an empty observation"
        outcome=incomplete
      fi
    fi
  fi
  observation=${observation//$'\n'/ }
  observation=${observation:0:1000}
  printf '%s\t%s\t%s\n' "$phase" "$outcome" "$observation" >>"$root/operator-observations.tsv"
  printf '%s\n' "$outcome"
}

phase_checklist() {
  local phase=$1 workspace=$2 screenshots=$3
  if [[ $phase == lifecycle ]]; then
    cat >&2 <<EOF

LIFECYCLE/TOOLS PHASE — observations are required; launch alone is never a pass.
1. In Zed, select “AVA M6 dogfood (lifecycle)” as the external agent.
2. Start a new thread rooted at: $workspace
3. Send exactly: run the deterministic M6 lifecycle and tool checklist
4. Observe initialization/new-session and streamed/final agent text.
5. Observe tool-call lifecycle and a visible permission choice; make a deliberate choice.
6. Confirm client filesystem read/write or patch effects are visible in src/todo.txt.
7. Confirm the negotiated client terminal runs \`cat\` and no AVA local-shell fallback is inferred.
8. Confirm end_turn and close the external-agent thread cleanly.
9. If a screenshot is necessary, save the raw image only under: $screenshots
Do not infer any item from process launch or provider startup.
EOF
  else
    cat >&2 <<EOF

CANCELLATION PHASE — observations are required; launch alone is never a pass.
1. In Zed, select “AVA M6 dogfood (cancellation)” as the external agent.
2. Start a new thread rooted at: $workspace
3. Send exactly: cancel this delayed deterministic M6 turn
4. While the delayed provider request is in flight, cancel from Zed.
5. Observe cancelled termination (not end_turn), then close the agent cleanly.
6. If a screenshot is necessary, save the raw image only under: $screenshots
Do not infer cancellation from killing Zed or from process cleanup.
EOF
  fi
}

write_workspace() {
  local phase_root=$1 phase=$2
  mkdir -p -- "$phase_root/workspace/src"
  if [[ $phase == lifecycle ]]; then
    cat >"$phase_root/workspace/src/todo.txt" <<'EOF'
status: TODO
detail: replace TODO with DONE and verify.
EOF
  else
    printf 'Cancellation phase workspace. No mutation is expected.\n' >"$phase_root/workspace/README.txt"
  fi
  cat >"$phase_root/workspace/OPERATOR-CHECKLIST.md" <<EOF
# AVA Zed M6 $phase phase

This workspace is deterministic staging for the bounded checklist printed by
scripts/live-acp-dogfood.sh. Process launch alone is not evidence of a passed phase.
Raw screenshots belong only in the phase raw/screenshots directory outside this workspace.
EOF
}

append_phase_metadata() {
  local root=$1 phase=$2 phase_root=$3 provider_command=$4 zed_command=$5
  cat >>"$root/evidence-report.md" <<EOF

### $phase command metadata (staged; not an outcome)

- AVA command: \`$zed_command\`
- Fake-provider command: \`$provider_command\`
- Raw phase directory: \`$phase_root/raw\`
- Operator outcome: [REQUIRED — transcribe and review operator-observations.tsv]
- Observed facts: [REQUIRED]
- Inferred facts: [REQUIRED]
- Cleanup result: [REQUIRED]
EOF
}

run_zed_phase() {
  local root=$1 phase=$2 ava=$3 fake_provider=$4 zed=$5 display_kind=$6 display=$7 display_auth=$8 operator_timeout=$9
  local phase_root delay scenario target provider_port outcome cleanup_ok=true
  active_phase=$phase
  phase_root=$(prepare_phase_root "$root" "$phase")
  active_phase_root=$phase_root
  write_workspace "$phase_root" "$phase"

  if [[ $phase == lifecycle ]]; then
    delay=0
    scenario=end-to-end-workflow
    target="$phase_root/workspace/src/todo.txt"
  else
    delay=15000
    scenario=text
    target=unused
  fi

  start_provider "$phase_root" "$fake_provider" "$delay" "$scenario" "$target"
  write_zed_settings "$phase_root" "$ava" "$provider_port" "$phase"
  append_phase_metadata "$root" "$phase" "$phase_root" \
    "$fake_provider <port-file> <raw-request-log> $delay $scenario <workspace-target>" \
    "$ava --acp (spawned by the exact Zed binary)"
  phase_checklist "$phase" "$phase_root/workspace" "$phase_root/raw/screenshots"
  start_zed "$phase_root" "$zed" "$display_kind" "$display" "$display_auth" "$phase_root/workspace"
  outcome=$(record_operator_outcome "$root" "$phase" "$operator_timeout")

  if ! cleanup_zed_phase_processes "$phase_root"; then
    cleanup_ok=false
  fi
  if ! terminate_group "$provider_pid"; then
    cleanup_ok=false
  fi
  provider_pid=""
  if [[ $cleanup_ok != true ]]; then
    die "phase $phase cleanup did not prove every exact phase-tagged process exited"
  fi
  active_phase_root=""
  printf '%s\tcleanup\tobserved: owned Zed launcher/provider processes terminated; exact inherited phase-tag cleanup completed; bounded residual scan found no PID\n' \
    "$phase" >>"$root/operator-observations.tsv"
  [[ $outcome == pass ]]
}

write_evidence_template() {
  local root=$1 confinement=$2 description=$3 display=$4 version=$5 ava=$6 provider=$7 zed=$8 preflight_digest=$9
  cat >"$root/evidence-report.md" <<EOF
---
report_status: incomplete
evidence_label: deferred
client: Zed
client_version: "[REQUIRED — observed version; preflight output was: ${version:0:200}]"
client_commit: "[REQUIRED — observed commit or explicitly unavailable]"
protocol: "ACP stable v1 schema-v1.19.0"
date_utc: "[REQUIRED]"
operator: "[REQUIRED — non-sensitive identifier]"
---

# Zed / AVA ACP evidence report

This generated report is intentionally incomplete. It is not a manual-verification
artifact and must not be copied into the repository until every required field and
phase outcome is manually reviewed and recorded.

## Scope and label

- Requested label: [REQUIRED]
- Claimed scope: [REQUIRED — observed flows only]
- Explicit exclusions: broad Zed compatibility, unobserved features, draft ACP v2

## Version and commit

- Zed exact executable: \`$zed\`
- AVA exact executable: \`$ava\`
- Fake-provider exact executable: \`$provider\`
- AVA version/commit: [REQUIRED]

## Confinement record

- Mode: \`$confinement\`
- Description: $description
- Dedicated display: \`$display\`
- Reviewed preflight SHA-256: \`${preflight_digest:-not-applicable-disposable-account-acknowledgement}\`
- Host-home denial: [REQUIRED]
- External-network denial or disposable-account scope: [REQUIRED]
- D-Bus/keyring/SSH-agent/unrelated-process denial: [REQUIRED]
- Temporary HOME/XDG clarification: state isolation only; not claimed as confinement

## Commands

- Exact launcher command: [REQUIRED — sanitized, no environment values]
- AVA agent command: \`<AVA> --acp\`
- Fake provider: loopback-only; exact raw command remains outside repository

## Phase outcomes

### Lifecycle, tool, permission, client filesystem, and terminal

- [ ] Initialization and new session observed
- [ ] Streamed/final agent text observed
- [ ] Tool-call lifecycle observed
- [ ] Visible permission decision observed and recorded
- [ ] Client filesystem operation observed
- [ ] Client terminal operation observed; no local-shell fallback inferred
- [ ] end_turn and clean close observed
- Outcome: [REQUIRED — pass, fail, or incomplete]
- Observed facts: [REQUIRED]
- Inferred facts: [REQUIRED]

### Cancellation

- [ ] Delayed turn observed in flight
- [ ] Cancellation initiated from Zed
- [ ] Cancelled termination observed
- [ ] Clean close observed
- Outcome: [REQUIRED — pass, fail, or incomplete]
- Observed facts: [REQUIRED]
- Inferred facts: [REQUIRED]

## Cleanup

- Zed process-group cleanup: [REQUIRED]
- AVA descendant cleanup: [REQUIRED]
- Fake-provider process-group cleanup: [REQUIRED]
- Residual-process check: [REQUIRED]

## Evidence derivatives and redaction

- Raw logs/screenshots location: private dogfood root only; never copy raw artifacts
- Manually reviewed textual derivatives: [REQUIRED]
- Screenshot derivatives: [REQUIRED — none, or individually justified/cropped/reviewed]
- Temporary paths normalized: [REQUIRED]
- Credentials/fake keys/environment values absent: [REQUIRED]
- Redaction reviewer: [REQUIRED]

## Observed versus inferred conclusion

- Observed: [REQUIRED]
- Inferred: [REQUIRED]
- Unsupported/unobserved: [REQUIRED]
EOF
  chmod 600 "$root/evidence-report.md"
  : >"$root/operator-observations.tsv"
  chmod 600 "$root/operator-observations.tsv"
}

zed_sanitize_copy() {
  local source="" destination=""
  shift
  while (($#)); do
    case $1 in
      --source) (($# >= 2)) || die "--source requires a value"; source=$2; shift 2 ;;
      --destination) (($# >= 2)) || die "--destination requires a value"; destination=$2; shift 2 ;;
      *) die "unknown zed sanitize-copy argument: $1" ;;
    esac
  done
  [[ $source == /* ]] || die "sanitize source must be an absolute path"
  [[ $destination == /* ]] || die "sanitize destination must be an absolute path"
  command -v python3 >/dev/null 2>&1 || die "python3 is required for bounded textual sanitization"
  python3 - "$source" "$destination" "$SOURCE_ROOT" "$MAX_REPORT_BYTES" "$FAKE_KEY" <<'PY'
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

source = Path(sys.argv[1])
destination = Path(sys.argv[2])
repo = Path(sys.argv[3]).resolve()
limit = int(sys.argv[4])
fake_key = sys.argv[5]
policy_dir = (repo / "docs/interop/evidence").resolve()

info = source.lstat()
if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
    raise SystemExit("sanitization rejected: source must be a regular non-symlink file")
if info.st_size > limit:
    raise SystemExit(f"sanitization rejected: source exceeds {limit} bytes")
raw = source.read_bytes()
if b"\0" in raw:
    raise SystemExit("sanitization rejected: NUL/binary content")
try:
    text = raw.decode("utf-8")
except UnicodeDecodeError as error:
    raise SystemExit(f"sanitization rejected: source is not UTF-8 text: {error}")
lines = text.splitlines()
if len(lines) > 500 or any(len(line) > 4096 for line in lines):
    raise SystemExit("sanitization rejected: report line/count bounds exceeded")
if fake_key in text or re.search(r"AVA_(?:ACP|ACPX|ZED)[A-Z0-9_]*FAKE[A-Z0-9_]*NOT_A_SECRET", text):
    raise SystemExit("sanitization rejected: fake key value is present")
if re.search(r"/(?:home|Users)/[^/\s`<>]+", text):
    raise SystemExit("sanitization rejected: user home path is present")
if re.search(r"/(?:tmp|var/tmp)/[^\s`<>]*ava-(?:acp|zed|dogfood)[^\s`<>]*", text, re.I) or re.search(r"/[^\s`<>]*ava-zed-dogfood[.][^\s`<>]*", text, re.I):
    raise SystemExit("sanitization rejected: private temporary path is present")
if re.search(r'''(?i)\b(?:api[_ -]?key|access[_ -]?token|auth(?:orization)?|password|secret|credential)s?\b["']?\s*[:=]\s*["']?(?!<redacted>|none\b|absent\b|not captured\b|\[required)[^\s`"']{4,}''', text):
    raise SystemExit("sanitization rejected: credential-looking value is present")
if re.search(r"(?im)^\s*(?:authorization|proxy-authorization)\s*:", text) or re.search(r"(?i)\bbearer\s+[A-Za-z0-9._~+/=-]{8,}", text):
    raise SystemExit("sanitization rejected: authorization material is present")
if re.search(r"!\[[^]]*\]\([^)]*\)", text):
    raise SystemExit("sanitization rejected: embedded/raw image reference is present")
if "report_status: complete" not in text:
    raise SystemExit("sanitization rejected: report_status is not complete")
if "[REQUIRED" in text or re.search(r"(?m)^\s*- \[ \]", text):
    raise SystemExit("sanitization rejected: required fields or checklist items remain incomplete")
for heading in ("## Scope and label", "## Version and commit", "## Confinement record", "## Commands", "## Phase outcomes", "## Cleanup", "## Evidence derivatives and redaction", "## Observed versus inferred conclusion"):
    if heading not in text:
        raise SystemExit(f"sanitization rejected: missing required section {heading}")

resolved_parent = destination.parent.resolve()
if resolved_parent != policy_dir or destination.suffix != ".md":
    raise SystemExit(f"sanitization rejected: destination must be a Markdown file directly under {policy_dir}")
if destination.exists():
    raise SystemExit("sanitization rejected: destination already exists")
policy_dir.mkdir(parents=True, exist_ok=True)
fd, temporary = tempfile.mkstemp(prefix=".zed-evidence-", dir=policy_dir)
try:
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
        if not text.endswith("\n"):
            handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.chmod(temporary, 0o644)
    os.replace(temporary, destination)
except BaseException:
    try:
        os.unlink(temporary)
    except FileNotFoundError:
        pass
    raise
print(f"copied manually reviewed bounded textual evidence to {destination}")
PY
}

zed_run() {
  local ava="" fake_provider="" zed="" display="" display_kind="" display_label="" confinement="" description="" preflight="" root_parent="${TMPDIR:-/tmp}"
  local display_auth_source="" display_auth_staged="" disposable_ack=false display_ack=false operator_timeout=$DEFAULT_OPERATOR_TIMEOUT
  local command_name
  for command_name in python3 setsid timeout ps realpath stat sha256sum awk install mktemp cat; do
    command -v "$command_name" >/dev/null 2>&1 || die "zed run requires local command: $command_name"
  done
  shift
  while (($#)); do
    case $1 in
      --ava) (($# >= 2)) || die "--ava requires a value"; ava=$2; shift 2 ;;
      --fake-provider) (($# >= 2)) || die "--fake-provider requires a value"; fake_provider=$2; shift 2 ;;
      --zed) (($# >= 2)) || die "--zed requires a value"; zed=$2; shift 2 ;;
      --display)
        (($# >= 2)) || die "--display requires a value"
        [[ -z $display_kind ]] || die "declare exactly one of --display or --wayland-display"
        display_kind=x11; display=$2; shift 2
        ;;
      --wayland-display)
        (($# >= 2)) || die "--wayland-display requires a value"
        [[ -z $display_kind ]] || die "declare exactly one of --display or --wayland-display"
        display_kind=wayland; display=$2; shift 2
        ;;
      --display-auth-file) (($# >= 2)) || die "--display-auth-file requires a value"; display_auth_source=$2; shift 2 ;;
      --acknowledge-dedicated-display) display_ack=true; shift ;;
      --confinement) (($# >= 2)) || die "--confinement requires a value"; confinement=$2; shift 2 ;;
      --confinement-description) (($# >= 2)) || die "--confinement-description requires a value"; description=$2; shift 2 ;;
      --preflight-evidence) (($# >= 2)) || die "--preflight-evidence requires a value"; preflight=$2; shift 2 ;;
      --acknowledge-disposable-credential-free) disposable_ack=true; shift ;;
      --root-parent) (($# >= 2)) || die "--root-parent requires a value"; root_parent=$2; shift 2 ;;
      --operator-timeout-seconds) (($# >= 2)) || die "--operator-timeout-seconds requires a value"; operator_timeout=$2; shift 2 ;;
      *) die "unknown zed run argument: $1" ;;
    esac
  done

  if [[ -z $ava || -z $fake_provider || -z $zed ]]; then
    print_zed_reproduction "$ava" "$fake_provider" "$zed"
    die "zed run requires explicit absolute AVA, fake-provider, and Zed paths"
  fi
  ava=$(require_absolute_executable "$ava" "AVA path")
  fake_provider=$(require_absolute_executable "$fake_provider" "fake-provider path")
  zed=$(require_absolute_executable "$zed" "Zed path")
  case $display_kind in
    x11)
      [[ -n $display && $display != *[[:space:]]* && ${#display} -le 128 ]] || die "--display must declare one bounded dedicated X11 display value"
      display_label="x11:$display"
      ;;
    wayland)
      [[ $display == /* && ${#display} -le 107 && -S $display && ! -L $display ]] || die "--wayland-display must be an absolute non-symlink socket path of at most 107 bytes"
      [[ -z $display_auth_source ]] || die "--display-auth-file is valid only with --display"
      display_label="wayland:$display"
      ;;
    *) die "declare exactly one of --display or --wayland-display" ;;
  esac
  [[ $display_ack == true ]] || { print_zed_reproduction "$ava" "$fake_provider" "$zed"; die "dedicated display acknowledgement is required; profile isolation is insufficient"; }
  [[ -n $description && ${#description} -le 1024 && $description != *$'\n'* ]] || die "a one-line confinement description (1..1024 bytes) is required"
  [[ $operator_timeout =~ ^[0-9]+$ ]] && ((operator_timeout >= 60 && operator_timeout <= 1800)) || die "operator timeout must be 60..1800 seconds"

  case $confinement in
    sandbox)
      [[ $disposable_ack == false ]] || die "sandbox and disposable-account declarations cannot be combined"
      [[ $preflight == /* && -f $preflight && ! -L $preflight ]] || { print_zed_reproduction "$ava" "$fake_provider" "$zed"; die "sandbox mode requires an absolute regular reviewed preflight-evidence file"; }
      (($(stat -c %s -- "$preflight") <= MAX_PREFLIGHT_BYTES)) || die "sandbox preflight evidence exceeds $MAX_PREFLIGHT_BYTES bytes"
      ;;
    disposable)
      [[ $disposable_ack == true ]] || { print_zed_reproduction "$ava" "$fake_provider" "$zed"; die "disposable mode requires --acknowledge-disposable-credential-free"; }
      [[ -z $preflight ]] || die "disposable mode does not accept sandbox preflight evidence"
      ;;
    "")
      print_zed_reproduction "$ava" "$fake_provider" "$zed"
      die "no confinement mode declared; ordinary-account profile-only execution is rejected"
      ;;
    *) die "--confinement must be sandbox or disposable" ;;
  esac

  mapfile -t forbidden < <(forbidden_environment_names)
  if ((${#forbidden[@]})); then
    printf 'ERROR: credential/agent/keyring/DBus variables are present in the launcher environment:\n' >&2
    printf '  %s\n' "${forbidden[@]}" >&2
    print_zed_reproduction "$ava" "$fake_provider" "$zed"
    die "remove every listed variable before zed run; the child allowlist alone does not satisfy the preflight"
  fi
  [[ $root_parent == /* ]] || die "--root-parent/TMPDIR must be absolute"
  mkdir -p -- "$root_parent"
  root_parent=$(realpath -e -- "$root_parent")
  local root
  root=$(mktemp -d -- "$root_parent/ava-zed-dogfood.XXXXXXXX")
  chmod 700 "$root"
  trap cleanup_active_processes EXIT INT TERM HUP
  scan_phase_tagged_processes "$root/process-tag-preflight" scan >/dev/null ||
    die "zed run requires a bounded Linux /proc environment scan with pidfd signaling"

  local preflight_digest=""
  if [[ $confinement == sandbox ]]; then
    install -m 600 -- "$preflight" "$root/confinement-preflight.txt"
    preflight_digest=$(sha256sum "$root/confinement-preflight.txt" | awk '{print $1}')
  else
    printf 'Operator explicitly acknowledged an already-provisioned disposable credential-free graphical account/VM.\n' >"$root/disposable-account-acknowledgement.txt"
    chmod 600 "$root/disposable-account-acknowledgement.txt"
  fi
  if [[ -n $display_auth_source ]]; then
    [[ $display_auth_source == /* && -f $display_auth_source && ! -L $display_auth_source ]] || die "--display-auth-file must be an absolute regular non-symlink file"
    install -m 600 -- "$display_auth_source" "$root/display-authority"
    display_auth_staged="$root/display-authority"
  fi
  cat >"$root/confinement-record.txt" <<EOF
mode=$confinement
description=$description
dedicated_display=$display_label
dedicated_display_acknowledged=true
launcher_credential_agent_keyring_dbus_environment=absent
child_environment=allowlisted
zed_emulated_gpu_acknowledged=true
home_xdg_roots=state-isolation-only-not-a-sandbox
sandbox_preflight_sha256=${preflight_digest:-not-applicable-disposable-account-acknowledgement}
EOF
  chmod 600 "$root/confinement-record.txt"

  mkdir -p -- "$root/version-profile"/{home,config,data,state,cache,runtime,tmp}
  chmod 700 "$root/version-profile" "$root/version-profile"/*
  local zed_version zed_detected_version zed_detected_commit
  zed_version=$(timeout 5 env -i \
    HOME="$root/version-profile/home" XDG_CONFIG_HOME="$root/version-profile/config" \
    XDG_DATA_HOME="$root/version-profile/data" XDG_STATE_HOME="$root/version-profile/state" \
    XDG_CACHE_HOME="$root/version-profile/cache" XDG_RUNTIME_DIR="$root/version-profile/runtime" \
    TMPDIR="$root/version-profile/tmp" PATH=/usr/local/bin:/usr/bin:/bin LANG=C.UTF-8 LC_ALL=C.UTF-8 \
    "$zed" --version 2>&1) || die "exact Zed binary version probe failed or exceeded 5 seconds"
  zed_version=${zed_version//$'\r'/ }
  zed_version=${zed_version//$'\n'/ ; }
  [[ -n $zed_version ]] || die "exact Zed binary version probe returned no version/commit (use the Zed CLI path, not its internal editor binary)"
  ((${#zed_version} <= 1024)) || die "Zed version output exceeded 1024 bytes"
  if [[ $zed_version =~ Zed[[:space:]]+([^[:space:]]+)[[:space:]]+([0-9a-f]{40}) ]]; then
    zed_detected_version=${BASH_REMATCH[1]}
    zed_detected_commit=${BASH_REMATCH[2]}
  else
    die "could not parse a Zed version and 40-character commit from: $zed_version"
  fi
  printf 'Zed version probe: %s (commit %s)\n' "$zed_detected_version" "$zed_detected_commit"

  write_evidence_template "$root" "$confinement" "$description" "$display_label" "$zed_version" "$ava" "$fake_provider" "$zed" "$preflight_digest"
  printf 'private dogfood root (mode 0700): %s\n' "$root"
  printf 'raw logs/screenshots must remain under this root; normal Zed settings are not used.\n'

  local lifecycle_ok=false cancellation_ok=false
  if run_zed_phase "$root" lifecycle "$ava" "$fake_provider" "$zed" "$display_kind" "$display" "$display_auth_staged" "$operator_timeout"; then
    lifecycle_ok=true
  fi
  if run_zed_phase "$root" cancellation "$ava" "$fake_provider" "$zed" "$display_kind" "$display" "$display_auth_staged" "$operator_timeout"; then
    cancellation_ok=true
  fi

  trap - EXIT INT TERM HUP
  terminate_group "$zed_pid"; zed_pid=""
  terminate_group "$provider_pid"; provider_pid=""
  cat <<EOF
Dogfood staging finished. The generated report remains INCOMPLETE regardless of phase input:
  $root/evidence-report.md
Review only bounded textual derivatives. Never copy raw logs or screenshots. After manually
completing and sanitizing the report, run:
  $(printf '%q' "$SCRIPT_PATH") zed sanitize-copy \\
    --source $(printf '%q' "$root/evidence-report.md") \\
    --destination $(printf '%q' "$SOURCE_ROOT/docs/interop/evidence/zed-${zed_detected_version}-YYYY-MM-DD.md")
This staged run remains unverified until its completed captured report is reviewed and copied successfully.
EOF
  [[ $lifecycle_ok == true && $cancellation_ok == true ]]
}

main() {
  (($# >= 1)) || { usage; exit 2; }
  local mode=$1
  shift
  case $mode in
    sdk)
      run_ctest_gate sdk AVA_REQUIRE_ACP_SDK_INTEROP '^ava_cli[.]acp_sdk_interop$' "$@"
      ;;
    acpx)
      run_ctest_gate acpx AVA_ENABLE_ACPX_INTEROP '^ava_cli[.]acpx_interop$' "$@"
      ;;
    zed)
      (($# >= 1)) || { usage; exit 2; }
      case $1 in
        run) zed_run "$@" ;;
        sanitize-copy) zed_sanitize_copy "$@" ;;
        *) die "zed action must be run or sanitize-copy" ;;
      esac
      ;;
    -h|--help|help)
      usage
      ;;
    *)
      usage
      die "mode must be sdk, acpx, or zed"
      ;;
  esac
}

main "$@"
