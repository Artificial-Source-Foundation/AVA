#!/usr/bin/env bash
set -euo pipefail

log_file="/tmp/ava-playwright-web-backend.log"
{
  echo "=== $(date -Iseconds) ==="
  echo "pwd: $(pwd)"
  echo "PATH: $PATH"
} >> "$log_file"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

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

echo "cargo_bin: $cargo_bin" >> "$log_file"

env CARGO_BUILD_JOBS="${CARGO_BUILD_JOBS:-4}" "$cargo_bin" run --bin ava --features web -- serve --port 18080 --token playwright-local-token >> "$log_file" 2>&1
status=$?
echo "cargo_exit: $status" >> "$log_file"
exit "$status"
