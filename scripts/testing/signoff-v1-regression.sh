#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/signoff-v1-regression.XXXXXX")"

readonly REQUIRED_TASK_FILTER="small_coding_http_status_class,normal_coding_retry_backoff,tool_reliability_timeout,tool_reliability_log_filter,tool_reliability_normalize,stress_coding_log_pipeline,product_smoke_session_config_discovery"
readonly DEFAULT_PROVIDER="openai"
readonly DEFAULT_MODEL="gpt-4.1"
stub_log="$tmp_root/cargo-invocations.log"

cleanup() {
  rm -rf "$tmp_root"
}
trap cleanup EXIT

assert_equal() {
  local actual="$1"
  local expected="$2"
  local label="$3"

  if [[ "$actual" != "$expected" ]]; then
    printf 'assertion failed: %s\nexpected: %s\nactual: %s\n' "$label" "$expected" "$actual" >&2
    exit 1
  fi
}

write_stub_cargo() {
  mkdir -p "$tmp_root/bin"

cat > "$tmp_root/bin/cargo" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${AVA_SIGNOFF_STUB_LOG:-}" ]]; then
  printf '%s\n' "$*" >> "$AVA_SIGNOFF_STUB_LOG"
fi

report_path=""
previous=""
for arg in "$@"; do
  if [[ "$previous" == "--benchmark-output" ]]; then
    report_path="$arg"
    break
  fi
  previous="$arg"
done

if [[ -z "$report_path" ]]; then
  echo "stub cargo: missing --benchmark-output" >&2
  exit 1
fi

python3 - "$report_path" <<'PY'
import json
import os
import sys

report_path = sys.argv[1]
task_names = [
    "small_coding_http_status_class",
    "normal_coding_retry_backoff",
    "tool_reliability_timeout",
    "tool_reliability_log_filter",
    "tool_reliability_normalize",
    "stress_coding_log_pipeline",
    "product_smoke_session_config_discovery",
]

report_case = os.environ.get("AVA_SIGNOFF_TEST_REPORT_CASE", "pass")
provider = os.environ.get("AVA_SIGNOFF_TEST_PROVIDER", "openai")
model = os.environ.get("AVA_SIGNOFF_TEST_MODEL", "gpt-4.1")
task_filter = os.environ.get("AVA_SIGNOFF_TEST_TASK_FILTER", ",".join(task_names))
run_count = int(os.environ.get("AVA_SIGNOFF_TEST_RUN_COUNT", "1"))
suite_name = os.environ.get("AVA_SIGNOFF_TEST_SUITE", "all")

results = [
    {
        "task_name": name,
        "quality_pass": True,
        "quality_details": "ok",
        "error": None,
        "provider": provider,
        "model": model,
    }
    for name in task_names
]

if report_case == "task_fail":
    results[0]["quality_pass"] = False
    results[0]["quality_details"] = "forced failure"

if report_case == "metadata_fail":
    provider = "anthropic"
    model = "claude-sonnet-4"
    run_count = 2
    task_filter = "small_coding_http_status_class"

report = {
    "schema_version": 2,
    "suite_name": suite_name,
    "task_filter": task_filter,
    "provider": provider,
    "model": model,
    "run_count": run_count,
    "runner_mode": "benchmark",
    "timestamp": "2026-04-19T00:00:00Z",
    "results": results,
}

with open(report_path, "w", encoding="utf-8") as handle:
    json.dump(report, handle)
PY
EOF

  chmod +x "$tmp_root/bin/cargo"
}

run_signoff_case() {
  local expected_status="$1"
  shift

  set +e
  (
    export PATH="$tmp_root/bin:$PATH"
    export AVA_SIGNOFF_STUB_LOG="$stub_log"
    for kv in "$@"; do
      export "$kv"
    done
    bash "$repo_root/scripts/testing/signoff-v1-headless.sh"
  ) >/dev/null 2>&1
  local status="$?"
  set -e

  if [[ "$status" -ne "$expected_status" ]]; then
    printf 'unexpected exit code: expected=%s actual=%s\n' "$expected_status" "$status" >&2
    exit 1
  fi
}

stub_invocation_count() {
  if [[ ! -f "$stub_log" ]]; then
    echo "0"
    return
  fi

  python3 - "$stub_log" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
print(len([line for line in text.splitlines() if line.strip()]))
PY
}

test_success_with_explicit_provider_model() {
  run_signoff_case 0 \
    "AVA_OPENAI_API_KEY=test-key" \
    "AVA_V1_SIGNOFF_PROVIDER=$DEFAULT_PROVIDER" \
    "AVA_V1_SIGNOFF_MODEL=$DEFAULT_MODEL" \
    "AVA_SIGNOFF_TEST_REPORT_CASE=pass" \
    "AVA_SIGNOFF_TEST_PROVIDER=$DEFAULT_PROVIDER" \
    "AVA_SIGNOFF_TEST_MODEL=$DEFAULT_MODEL" \
    "AVA_SIGNOFF_TEST_RUN_COUNT=1" \
    "AVA_SIGNOFF_TEST_TASK_FILTER=$REQUIRED_TASK_FILTER"

  assert_equal "$(stub_invocation_count)" "1" "success path should invoke benchmark once"
}

test_fails_when_provider_model_are_not_set_together() {
  run_signoff_case 1 \
    "AVA_V1_SIGNOFF_PROVIDER=$DEFAULT_PROVIDER"

  assert_equal "$(stub_invocation_count)" "1" "provider/model pair gate should fail before benchmark"
}

test_fails_when_provider_auth_missing() {
  run_signoff_case 1 \
    "AVA_V1_SIGNOFF_PROVIDER=openrouter" \
    "AVA_V1_SIGNOFF_MODEL=anthropic/claude-sonnet-4"

  assert_equal "$(stub_invocation_count)" "1" "provider auth gate should fail before benchmark"
}

test_fails_for_unsupported_provider() {
  run_signoff_case 1 \
    "AVA_V1_SIGNOFF_PROVIDER=unknown" \
    "AVA_V1_SIGNOFF_MODEL=unknown-model"

  assert_equal "$(stub_invocation_count)" "1" "unsupported provider gate should fail before benchmark"
}

test_fails_on_report_metadata_mismatch() {
  run_signoff_case 1 \
    "AVA_OPENAI_API_KEY=test-key" \
    "AVA_SIGNOFF_TEST_REPORT_CASE=metadata_fail"

  assert_equal "$(stub_invocation_count)" "2" "metadata mismatch should still execute benchmark"
}

test_fails_on_task_failure() {
  run_signoff_case 1 \
    "AVA_OPENAI_API_KEY=test-key" \
    "AVA_SIGNOFF_TEST_REPORT_CASE=task_fail"

  assert_equal "$(stub_invocation_count)" "3" "task failure should still execute benchmark"
}

main() {
  : > "$stub_log"
  write_stub_cargo
  test_success_with_explicit_provider_model
  test_fails_when_provider_model_are_not_set_together
  test_fails_when_provider_auth_missing
  test_fails_for_unsupported_provider
  test_fails_on_report_metadata_mismatch
  test_fails_on_task_failure
  echo "[signoff:v1:regression] all checks passed"
}

main "$@"
