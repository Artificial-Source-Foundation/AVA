#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

# Minimal required-now V1-evals signoff slice from docs/project/v1-evals.md.
readonly REQUIRED_TASK_FILTER="small_coding_http_status_class,normal_coding_retry_backoff,tool_reliability_timeout,tool_reliability_log_filter,tool_reliability_normalize,stress_coding_log_pipeline,product_smoke_session_config_discovery"

run_step() {
  local label="$1"
  shift

  printf '[signoff:v1] %s\n' "$label"
  (cd "$repo_root" && "$@")
}

detect_provider_model() {
  local provider="${AVA_V1_SIGNOFF_PROVIDER:-}"
  local model="${AVA_V1_SIGNOFF_MODEL:-}"

  if [[ -n "$provider" || -n "$model" ]]; then
    if [[ -z "$provider" || -z "$model" ]]; then
      echo "[signoff:v1] AVA_V1_SIGNOFF_PROVIDER and AVA_V1_SIGNOFF_MODEL must be set together"
      exit 1
    fi

    V1_SIGNOFF_PROVIDER="$provider"
    V1_SIGNOFF_MODEL="$model"
    return
  fi

  if [[ -n "${AVA_OPENAI_API_KEY:-}" ]]; then
    V1_SIGNOFF_PROVIDER="openai"
    V1_SIGNOFF_MODEL="gpt-4.1"
    return
  fi

  if [[ -n "${AVA_ANTHROPIC_API_KEY:-}" ]]; then
    V1_SIGNOFF_PROVIDER="anthropic"
    V1_SIGNOFF_MODEL="claude-sonnet-4"
    return
  fi

  if [[ -n "${AVA_OPENROUTER_API_KEY:-}" ]]; then
    V1_SIGNOFF_PROVIDER="openrouter"
    V1_SIGNOFF_MODEL="anthropic/claude-sonnet-4"
    return
  fi

  echo "[signoff:v1] cannot run benchmark-backed V1 signoff: no provider auth found"
  echo "[signoff:v1] set one of AVA_OPENAI_API_KEY / AVA_ANTHROPIC_API_KEY / AVA_OPENROUTER_API_KEY"
  echo "[signoff:v1] or set AVA_V1_SIGNOFF_PROVIDER and AVA_V1_SIGNOFF_MODEL explicitly"
  exit 1
}

require_provider_auth() {
  local provider="$1"

  case "$provider" in
    openai)
      [[ -n "${AVA_OPENAI_API_KEY:-}" ]] || {
        echo "[signoff:v1] AVA_V1_SIGNOFF_PROVIDER=openai requires AVA_OPENAI_API_KEY"
        exit 1
      }
      ;;
    anthropic)
      [[ -n "${AVA_ANTHROPIC_API_KEY:-}" ]] || {
        echo "[signoff:v1] AVA_V1_SIGNOFF_PROVIDER=anthropic requires AVA_ANTHROPIC_API_KEY"
        exit 1
      }
      ;;
    openrouter)
      [[ -n "${AVA_OPENROUTER_API_KEY:-}" ]] || {
        echo "[signoff:v1] AVA_V1_SIGNOFF_PROVIDER=openrouter requires AVA_OPENROUTER_API_KEY"
        exit 1
      }
      ;;
    *)
      echo "[signoff:v1] unsupported AVA_V1_SIGNOFF_PROVIDER: $provider"
      echo "[signoff:v1] supported providers: openai, anthropic, openrouter"
      exit 1
      ;;
  esac
}

enforce_report_success() {
  local report_path="$1"
  local expected_provider="$2"
  local expected_model="$3"
  local expected_task_filter="$4"
  local expected_run_count="$5"
  local expected_suite="$6"

  if ! command -v python3 >/dev/null 2>&1; then
    echo "[signoff:v1] python3 is required to enforce benchmark report pass/fail"
    exit 1
  fi

  python3 - "$report_path" "$expected_task_filter" "$expected_provider" "$expected_model" "$expected_run_count" "$expected_suite" <<'PY'
import json
import sys

report_path = sys.argv[1]
expected = [name for name in sys.argv[2].split(",") if name]
expected_provider = sys.argv[3]
expected_model = sys.argv[4]
expected_run_count = int(sys.argv[5])
expected_suite = sys.argv[6]

with open(report_path, "r", encoding="utf-8") as handle:
    report = json.load(handle)

results = report.get("results") or []
names = [result.get("task_name") for result in results]
metadata_failures = []

provider = report.get("provider")
if provider != expected_provider:
    metadata_failures.append(
        f"provider mismatch: expected {expected_provider!r}, got {provider!r}"
    )

model = report.get("model")
if model != expected_model:
    metadata_failures.append(
        f"model mismatch: expected {expected_model!r}, got {model!r}"
    )

task_filter = report.get("task_filter")
if task_filter != sys.argv[2]:
    metadata_failures.append(
        f"task_filter mismatch: expected {sys.argv[2]!r}, got {task_filter!r}"
    )

suite_name = report.get("suite_name")
if suite_name != expected_suite:
    metadata_failures.append(
        f"suite_name mismatch: expected {expected_suite!r}, got {suite_name!r}"
    )

run_count = report.get("run_count")
if run_count != expected_run_count:
    metadata_failures.append(
        f"run_count mismatch: expected {expected_run_count}, got {run_count!r}"
    )

missing = [name for name in expected if name not in names]
extra = [name for name in names if name not in expected]
failed = [
    result
    for result in results
    if (not bool(result.get("quality_pass"))) or result.get("error") is not None
]

if metadata_failures or len(results) != len(expected) or missing or extra or failed:
    print("[signoff:v1] benchmark-backed signoff failed")
    if metadata_failures:
        print("[signoff:v1] report metadata failures:")
        for failure in metadata_failures:
            print(f"  - {failure}")
    if len(results) != len(expected):
        print(f"[signoff:v1] expected {len(expected)} results but got {len(results)}")
    if missing:
        print("[signoff:v1] missing tasks:", ", ".join(missing))
    if extra:
        print("[signoff:v1] unexpected tasks:", ", ".join(extra))
    if failed:
        print("[signoff:v1] failed tasks:")
        for result in failed:
            name = result.get("task_name", "<unknown>")
            details = result.get("quality_details") or "no quality details"
            error = result.get("error")
            if error:
                print(f"  - {name}: {details}; error={error}")
            else:
                print(f"  - {name}: {details}")
    sys.exit(1)

print(f"[signoff:v1] benchmark-backed signoff passed ({len(expected)}/{len(expected)} required tasks)")
PY
}

main() {
  local provider model report_file

  V1_SIGNOFF_PROVIDER=""
  V1_SIGNOFF_MODEL=""
  detect_provider_model
  provider="$V1_SIGNOFF_PROVIDER"
  model="$V1_SIGNOFF_MODEL"

  require_provider_auth "$provider"

  report_file="$(mktemp -t ava-v1-signoff-report-XXXXXX.json)"

  run_step "running benchmark-backed headless required-now signoff slice (provider=${provider}, model=${model})" \
    cargo run -p ava-tui --bin ava --features benchmark -- \
      --benchmark \
      --provider "$provider" \
      --model "$model" \
      --suite all \
      --task-filter "$REQUIRED_TASK_FILTER" \
      --max-turns 25 \
      --benchmark-output "$report_file"

  run_step "enforcing benchmark pass/fail from report" \
    enforce_report_success "$report_file" "$provider" "$model" "$REQUIRED_TASK_FILTER" "1" "all"

  echo "[signoff:v1] report saved: $report_file"
  echo "[signoff:v1] V1 headless benchmark signoff complete"
}

main "$@"
