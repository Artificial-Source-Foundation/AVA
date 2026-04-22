#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

run_step() {
  local label="$1"
  shift

  printf '[backend-gate] %s\n' "$label"
  (cd "$repo_root" && "$@")
}

run_required_no_secrets_checks() {
  run_step "required no-secrets check: ava-config focused coverage" \
    ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo test -p ava-config

  run_step "required no-secrets check: delegated runtime signoff" \
    ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo test -p ava-agent-orchestration agent_stack_run_dispatches_subagent_when_enabled -- --exact

  run_step "required no-secrets check: desktop tauri compile smoke" \
    ionice -c 3 nice -n 15 env CARGO_BUILD_JOBS=4 cargo check --manifest-path src-tauri/Cargo.toml --lib

  run_step "required no-secrets check: mock provider unattended approval smoke" \
    cargo run --quiet --bin ava-smoke

  run_step "required no-secrets check: headless runtime slash smoke" \
    cargo run --quiet --bin ava -- "/help" --headless --max-turns 1 --no-update-check >/dev/null
}

run_live_provider_smoke() {
  if [[ -n "${AVA_OPENAI_API_KEY:-}" || -n "${AVA_ANTHROPIC_API_KEY:-}" || -n "${AVA_OPENROUTER_API_KEY:-}" ]]; then
    local provider="openrouter"
    local model="anthropic/claude-sonnet-4"

    if [[ -n "${AVA_OPENAI_API_KEY:-}" ]]; then
      provider="openai"
      model="gpt-4.1"
    elif [[ -n "${AVA_ANTHROPIC_API_KEY:-}" ]]; then
      provider="anthropic"
      model="claude-sonnet-4"
    elif [[ -n "${AVA_OPENROUTER_API_KEY:-}" ]]; then
      provider="openrouter"
      model="anthropic/claude-sonnet-4"
    fi

    local output_file
    output_file="$(mktemp)"

    run_step "optional live-provider check: single headless smoke via --provider ${provider} --model ${model}" \
      cargo run --quiet --bin ava \
        "Reply exactly with BACKEND_GATE_OK and stop." \
        --headless \
        --provider "$provider" \
        --model "$model" \
        --max-turns 2 \
        --auto-approve \
        --no-update-check \
        >"$output_file" 2>&1

    if ! grep -qF "BACKEND_GATE_OK" "$output_file"; then
      echo "[backend-gate] optional live-provider check failed: marker BACKEND_GATE_OK not found"
      echo "[backend-gate] command output:"
      cat "$output_file"
      rm -f "$output_file"
      exit 1
    fi

    rm -f "$output_file"
    return
  fi

  echo "[backend-gate] optional live-provider check skipped: no AVA_* provider API key is present"
}

run_required_no_secrets_checks
run_live_provider_smoke

echo "[backend-gate] backend automation gate complete"
