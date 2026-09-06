# Shared fake-provider broker ownership for generated POSIX sh CLI test drivers.
#
# Sourcing contract: AVA_PYTHON, AVA_FAKE_PROVIDER_PY, and AVA_FAKE_PROVIDER_EXE
# must be set. One broker is owned at a time; fake_provider_start stops any
# previous broker first. Broker replies are bounded by the broker's own command
# timeouts, so these plain blocking reads stay bounded without relying on
# non-POSIX `read -t` support (/bin/sh may be dash).
#
# Protocol: fake_provider_start DIR PREFIX DELAY_MS SCENARIO TARGET publishes
# FAKE_PROVIDER_PORT; fake_provider_wait INDEX TIMEOUT and
# fake_provider_release INDEX drive request gates. fake_provider_finish TIMEOUT
# requires natural provider exit 0 and reaps the broker; fake_provider_stop is
# cancellation/error cleanup.

FAKE_PROVIDER_PORT=""
_FAKE_PROVIDER_BROKER_PID=""
_FAKE_PROVIDER_DIRECTORY=""
_FAKE_PROVIDER_PREFIX=""

_fake_provider_dump_logs() {
  [ -n "$_FAKE_PROVIDER_DIRECTORY" ] || return 0
  for _fake_provider_suffix in broker.err err out; do
    _fake_provider_file="$_FAKE_PROVIDER_DIRECTORY/$_FAKE_PROVIDER_PREFIX.$_fake_provider_suffix"
    if [ -s "$_fake_provider_file" ]; then
      echo "--- $_fake_provider_file ---" >&2
      cat "$_fake_provider_file" >&2
    fi
  done
}

fake_provider_stop() {
  _fake_provider_status=0
  if [ -n "$_FAKE_PROVIDER_BROKER_PID" ]; then
    _fake_provider_pid=$_FAKE_PROVIDER_BROKER_PID
    _FAKE_PROVIDER_BROKER_PID=""
    if kill -0 "$_fake_provider_pid" 2>/dev/null; then
      # Ask the broker for cancellation/error cleanup; EOF on the closed
      # descriptors is the fallback that still guarantees provider cleanup.
      printf 'stop\n' >&8 2>/dev/null || true
    fi
    wait "$_fake_provider_pid" 2>/dev/null || _fake_provider_status=$?
    exec 8>&- 9<&-
  fi
  _FAKE_PROVIDER_DIRECTORY=""
  _FAKE_PROVIDER_PREFIX=""
  FAKE_PROVIDER_PORT=""
  return "$_fake_provider_status"
}

fake_provider_finish() {
  # $1 timeout seconds for natural completion after scripted requests.
  _fake_provider_status=0
  _fake_provider_command "finish $1" || _fake_provider_status=$?
  if [ -n "$_FAKE_PROVIDER_BROKER_PID" ]; then
    _fake_provider_pid=$_FAKE_PROVIDER_BROKER_PID
    _FAKE_PROVIDER_BROKER_PID=""
    wait "$_fake_provider_pid" 2>/dev/null || {
      _fake_provider_wait_status=$?
      [ "$_fake_provider_status" -ne 0 ] || _fake_provider_status=$_fake_provider_wait_status
    }
    exec 8>&- 9<&-
  fi
  if [ "$_fake_provider_status" -ne 0 ]; then
    _fake_provider_dump_logs
  fi
  _FAKE_PROVIDER_DIRECTORY=""
  _FAKE_PROVIDER_PREFIX=""
  FAKE_PROVIDER_PORT=""
  return "$_fake_provider_status"
}

fake_provider_start() {
  # $1 directory, $2 artifact prefix, $3 delay_ms (streaming cadence only),
  # $4 scenario, $5 scenario target path ("unused" when the scenario has none).
  fake_provider_stop >/dev/null 2>&1 || true
  if [ ! -x "${AVA_PYTHON:-}" ] || [ ! -f "${AVA_FAKE_PROVIDER_PY:-}" ] || [ ! -x "${AVA_FAKE_PROVIDER_EXE:-}" ]; then
    echo "fake provider broker inputs are missing or not executable" >&2
    return 1
  fi
  _FAKE_PROVIDER_DIRECTORY=$1
  _FAKE_PROVIDER_PREFIX=$2
  _fake_provider_in="$1/$2.broker-in"
  _fake_provider_out="$1/$2.broker-out"
  rm -f -- "$_fake_provider_in" "$_fake_provider_out"
  if ! mkfifo -- "$_fake_provider_in" "$_fake_provider_out"; then
    echo "failed to create fake provider broker channels" >&2
    return 1
  fi
  "$AVA_PYTHON" "$AVA_FAKE_PROVIDER_PY" broker \
    --provider "$AVA_FAKE_PROVIDER_EXE" \
    --directory "$1" \
    --prefix "$2" \
    --delay-ms "$3" \
    --scenario "$4" \
    --target "$5" \
    < "$_fake_provider_in" > "$_fake_provider_out" 2> "$1/$2.broker.err" &
  _FAKE_PROVIDER_BROKER_PID=$!
  exec 8>"$_fake_provider_in"
  exec 9<"$_fake_provider_out"
  if ! IFS= read -r _fake_provider_reply <&9; then
    echo "fake provider broker exited before publishing its port" >&2
    _fake_provider_dump_logs
    fake_provider_stop >/dev/null 2>&1 || true
    return 1
  fi
  case $_fake_provider_reply in
    "ready port="[0-9]*)
      FAKE_PROVIDER_PORT=${_fake_provider_reply#ready port=}
      return 0
      ;;
    *)
      echo "fake provider broker failed: $_fake_provider_reply" >&2
      _fake_provider_dump_logs
      fake_provider_stop >/dev/null 2>&1 || true
      return 1
      ;;
  esac
}

_fake_provider_command() {
  if [ -z "$_FAKE_PROVIDER_BROKER_PID" ] || ! kill -0 "$_FAKE_PROVIDER_BROKER_PID" 2>/dev/null; then
    echo "fake provider broker is not running" >&2
    _fake_provider_dump_logs
    return 1
  fi
  if ! printf '%s\n' "$1" >&8; then
    echo "fake provider broker command channel closed" >&2
    return 1
  fi
  if ! IFS= read -r _fake_provider_reply <&9; then
    echo "fake provider broker closed its reply channel" >&2
    _fake_provider_dump_logs
    return 1
  fi
  case $_fake_provider_reply in
    ok)
      return 0
      ;;
    *)
      echo "fake provider broker: $_fake_provider_reply" >&2
      return 1
      ;;
  esac
}

fake_provider_wait() {
  # $1 zero-based provider request index, $2 timeout seconds.
  _fake_provider_command "wait $1 $2"
}

fake_provider_release() {
  # $1 zero-based provider request index whose delayed response may proceed.
  _fake_provider_command "release $1"
}
