#!/usr/bin/env bash
set -euo pipefail

tmp_dir="$(mktemp -d)"
backend_log="/tmp/ava-playwright-backend.log"
frontend_log="/tmp/ava-playwright-frontend.log"

cleanup() {
  if [[ -n "${backend_pid:-}" ]] && kill -0 "$backend_pid" 2>/dev/null; then
    kill "$backend_pid" 2>/dev/null || true
    wait "$backend_pid" 2>/dev/null || true
  fi
  if [[ -n "${frontend_pid:-}" ]] && kill -0 "$frontend_pid" 2>/dev/null; then
    kill "$frontend_pid" 2>/dev/null || true
    wait "$frontend_pid" 2>/dev/null || true
  fi
  rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

export XDG_DATA_HOME="$tmp_dir/data"
export XDG_STATE_HOME="$tmp_dir/state"
export XDG_CONFIG_HOME="$tmp_dir/config"

app_data_dir="$XDG_DATA_HOME/ava"
app_state_dir="$XDG_STATE_HOME/ava"
app_config_dir="$XDG_CONFIG_HOME/ava"

# Seed the preferred XDG paths so ava_config never falls back to legacy ~/.ava files.
mkdir -p "$app_data_dir" "$app_state_dir" "$app_config_dir"
: > "$app_data_dir/data.db"

cargo_bin="${HOME}/.cargo/bin/cargo"
if [[ -x "$cargo_bin" ]]; then
  export PATH="${HOME}/.cargo/bin:${PATH}"
else
  cargo_bin="cargo"
fi

: > "$backend_log"
: > "$frontend_log"

env CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-4}" "$cargo_bin" build --bin ava --features web >> "$backend_log" 2>&1

./target/debug/ava serve --port 18080 --token playwright-local-token >> "$backend_log" 2>&1 &
backend_pid=$!

for _ in {1..300}; do
  if curl -fsS http://127.0.0.1:18080/api/health >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$backend_pid" 2>/dev/null; then
    cat "$backend_log" >&2 || true
    exit 1
  fi
  sleep 1
done

if ! curl -fsS http://127.0.0.1:18080/api/health >/dev/null 2>&1; then
  cat "$backend_log" >&2 || true
  exit 1
fi

VITE_API_URL=http://localhost:18080 VITE_AVA_SERVER_TOKEN=playwright-local-token VITE_DISABLE_BACKEND_PROXY=1 npx vite --port 11420 >> "$frontend_log" 2>&1 &
frontend_pid=$!

for _ in {1..60}; do
  if curl -fsS http://127.0.0.1:11420 >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$frontend_pid" 2>/dev/null; then
    cat "$frontend_log" >&2 || true
    exit 1
  fi
  sleep 1
done

if ! curl -fsS http://127.0.0.1:11420 >/dev/null 2>&1; then
  cat "$frontend_log" >&2 || true
  exit 1
fi

wait "$backend_pid" "$frontend_pid"
