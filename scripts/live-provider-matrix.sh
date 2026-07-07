#!/bin/sh
set -u

# Provider live matrix runner.
#
# Usage:
#   AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-provider-matrix.sh
#
# Optional knobs:
#   AVA_LIVE_PROVIDER_MATRIX_TARGET=provider-live-smoke|model-dogfood|coding-dogfood
#   AVA_BUILD_DIR=build
#   AVA_TESTS_EXE=/path/to/ava_tests
#   AVA_EXE=/path/to/ava
#   AVA_LIVE_PROVIDER_MATRIX_SUMMARY=/tmp/ava-live-provider-matrix.tsv
#   AVA_LIVE_PROVIDER_MATRIX_KEEP=1
#
# provider-live-smoke (the default) runs the targeted ava_tests
# provider_live_smoke suite once per credential env. The dogfood targets wrap
# the existing full-binary live scripts once per credential env.
#
# The runner checks only whether credential environment variables are present,
# then clears non-target provider credentials before each child run. It records
# provider ids, credential env names, model ids, exit statuses, and result
# classifications, but never prints credential values. Child output is captured
# under a temporary directory and removed unless AVA_LIVE_PROVIDER_MATRIX_KEEP=1;
# redact retained logs before sharing them. Syntax check this script with:
#   sh -n scripts/live-provider-matrix.sh

is_enabled()
{
  case "$1" in
    1|true|TRUE|yes|on) return 0 ;;
    *) return 1 ;;
  esac
}

usage()
{
  echo "usage: AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-provider-matrix.sh" >&2
  echo "set AVA_LIVE_PROVIDER_MATRIX_TARGET to provider-live-smoke, model-dogfood, or coding-dogfood" >&2
}

start_dir=$PWD
script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd) || exit 1
repo_root=$(CDPATH= cd "$script_dir/.." && pwd) || exit 1

if [ -n "${AVA_LIVE_PROVIDER_MATRIX_TARGET:-}" ]; then
  target=$AVA_LIVE_PROVIDER_MATRIX_TARGET
else
  target=${AVA_LIVE_MATRIX_TARGET:-provider-live-smoke}
fi
case "$target" in
  provider-live-smoke|model-dogfood|coding-dogfood) ;;
  *)
    usage
    exit 2
    ;;
esac

if [ -n "${AVA_BUILD_DIR:-}" ]; then
  build_dir=$AVA_BUILD_DIR
else
  build_dir=$repo_root/build
fi
case "$build_dir" in
  /*) ;;
  *) build_dir=$start_dir/$build_dir ;;
esac

if [ -n "${AVA_TESTS_EXE:-}" ]; then
  ava_tests_exe=$AVA_TESTS_EXE
else
  ava_tests_exe=$build_dir/tests/ava_tests
fi
case "$ava_tests_exe" in
  /*) ;;
  *) ava_tests_exe=$start_dir/$ava_tests_exe ;;
esac

if [ -n "${AVA_EXE:-}" ]; then
  ava_exe=$AVA_EXE
else
  ava_exe=$build_dir/ava
fi
case "$ava_exe" in
  /*) ;;
  *) ava_exe=$start_dir/$ava_exe ;;
esac

if [ -n "${AVA_LIVE_PROVIDER_MATRIX_SUMMARY:-}" ]; then
  summary=$AVA_LIVE_PROVIDER_MATRIX_SUMMARY
else
  summary=${TMPDIR:-/tmp}/ava-live-provider-matrix-summary.$$.tsv
fi
case "$summary" in
  /*) ;;
  *) summary=$start_dir/$summary ;;
esac

run_root=$(mktemp -d "${TMPDIR:-/tmp}/ava-live-provider-matrix.XXXXXX") || exit 1
cleanup()
{
  if [ -z "${AVA_LIVE_PROVIDER_MATRIX_KEEP:-}" ]; then
    rm -rf "$run_root"
  fi
}
trap cleanup EXIT INT TERM

if ! : > "$summary"; then
  echo "unable to write summary: $summary" >&2
  exit 1
fi
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
  "case" "provider" "credential_env" "model" "target" "classification" "exit_status" "log_path" >> "$summary"

matrix_openai_api_key=${OPENAI_API_KEY:-}
matrix_anthropic_oauth_token=${ANTHROPIC_OAUTH_TOKEN:-}
matrix_anthropic_auth_token=${ANTHROPIC_AUTH_TOKEN:-}
matrix_anthropic_api_key=${ANTHROPIC_API_KEY:-}
matrix_deepseek_api_key=${DEEPSEEK_API_KEY:-}
matrix_gemini_api_key=${GEMINI_API_KEY:-}
matrix_kimi_api_key=${KIMI_API_KEY:-}
matrix_moonshot_api_key=${MOONSHOT_API_KEY:-}
matrix_openrouter_api_key=${OPENROUTER_API_KEY:-}

provider_for_case()
{
  case "$1" in
    openai) printf '%s' "openai" ;;
    anthropic_oauth|anthropic_auth|anthropic_api) printf '%s' "anthropic" ;;
    deepseek) printf '%s' "deepseek" ;;
    gemini) printf '%s' "gemini" ;;
    kimi) printf '%s' "kimi" ;;
    moonshot) printf '%s' "moonshot" ;;
    openrouter) printf '%s' "openrouter" ;;
    *) printf '%s' "unknown" ;;
  esac
}

label_for_case()
{
  case "$1" in
    openai) printf '%s' "OpenAI" ;;
    anthropic_oauth) printf '%s' "Anthropic OAuth token" ;;
    anthropic_auth) printf '%s' "Anthropic auth token" ;;
    anthropic_api) printf '%s' "Anthropic API key" ;;
    deepseek) printf '%s' "DeepSeek" ;;
    gemini) printf '%s' "Gemini" ;;
    kimi) printf '%s' "Kimi" ;;
    moonshot) printf '%s' "Moonshot" ;;
    openrouter) printf '%s' "OpenRouter" ;;
    *) printf '%s' "$1" ;;
  esac
}

credential_env_for_case()
{
  case "$1" in
    openai) printf '%s' "OPENAI_API_KEY" ;;
    anthropic_oauth) printf '%s' "ANTHROPIC_OAUTH_TOKEN" ;;
    anthropic_auth) printf '%s' "ANTHROPIC_AUTH_TOKEN" ;;
    anthropic_api) printf '%s' "ANTHROPIC_API_KEY" ;;
    deepseek) printf '%s' "DEEPSEEK_API_KEY" ;;
    gemini) printf '%s' "GEMINI_API_KEY" ;;
    kimi) printf '%s' "KIMI_API_KEY" ;;
    moonshot) printf '%s' "MOONSHOT_API_KEY" ;;
    openrouter) printf '%s' "OPENROUTER_API_KEY" ;;
    *) printf '%s' "unknown" ;;
  esac
}

model_for_case()
{
  case "$1" in
    openai) printf '%s' "${AVA_LIVE_OPENAI_MODEL:-gpt-4.1-mini}" ;;
    anthropic_oauth|anthropic_auth|anthropic_api) printf '%s' "${AVA_LIVE_ANTHROPIC_MODEL:-claude-sonnet-4-5}" ;;
    deepseek) printf '%s' "${AVA_LIVE_DEEPSEEK_MODEL:-deepseek-v4-flash}" ;;
    gemini) printf '%s' "${AVA_LIVE_GEMINI_MODEL:-gemini-2.5-pro}" ;;
    kimi) printf '%s' "${AVA_LIVE_KIMI_MODEL:-kimi-k2-thinking}" ;;
    moonshot) printf '%s' "${AVA_LIVE_MOONSHOT_MODEL:-kimi-k2.6}" ;;
    openrouter) printf '%s' "${AVA_LIVE_OPENROUTER_MODEL:-moonshotai/kimi-k2.6}" ;;
    *) printf '%s' "" ;;
  esac
}

credential_present()
{
  case "$1" in
    openai) [ -n "$matrix_openai_api_key" ] ;;
    anthropic_oauth) [ -n "$matrix_anthropic_oauth_token" ] ;;
    anthropic_auth) [ -n "$matrix_anthropic_auth_token" ] ;;
    anthropic_api) [ -n "$matrix_anthropic_api_key" ] ;;
    deepseek) [ -n "$matrix_deepseek_api_key" ] ;;
    gemini) [ -n "$matrix_gemini_api_key" ] ;;
    kimi) [ -n "$matrix_kimi_api_key" ] ;;
    moonshot) [ -n "$matrix_moonshot_api_key" ] ;;
    openrouter) [ -n "$matrix_openrouter_api_key" ] ;;
    *) return 1 ;;
  esac
}

export_model_for_case()
{
  case "$1" in
    openai)
      AVA_LIVE_OPENAI_MODEL=$(model_for_case "$1")
      export AVA_LIVE_OPENAI_MODEL
      ;;
    anthropic_oauth|anthropic_auth|anthropic_api)
      AVA_LIVE_ANTHROPIC_MODEL=$(model_for_case "$1")
      export AVA_LIVE_ANTHROPIC_MODEL
      ;;
    deepseek)
      AVA_LIVE_DEEPSEEK_MODEL=$(model_for_case "$1")
      export AVA_LIVE_DEEPSEEK_MODEL
      ;;
    gemini)
      AVA_LIVE_GEMINI_MODEL=$(model_for_case "$1")
      export AVA_LIVE_GEMINI_MODEL
      ;;
    kimi)
      AVA_LIVE_KIMI_MODEL=$(model_for_case "$1")
      export AVA_LIVE_KIMI_MODEL
      ;;
    moonshot)
      AVA_LIVE_MOONSHOT_MODEL=$(model_for_case "$1")
      export AVA_LIVE_MOONSHOT_MODEL
      ;;
    openrouter)
      AVA_LIVE_OPENROUTER_MODEL=$(model_for_case "$1")
      export AVA_LIVE_OPENROUTER_MODEL
      ;;
  esac
}

export_case_environment()
{
  unset OPENAI_API_KEY
  unset ANTHROPIC_OAUTH_TOKEN
  unset ANTHROPIC_AUTH_TOKEN
  unset ANTHROPIC_API_KEY
  unset DEEPSEEK_API_KEY
  unset GEMINI_API_KEY
  unset KIMI_API_KEY
  unset MOONSHOT_API_KEY
  unset OPENROUTER_API_KEY

  AVA_LIVE_PROVIDER_SMOKE=1
  NO_COLOR=1
  export AVA_LIVE_PROVIDER_SMOKE NO_COLOR
  export_model_for_case "$1"

  case "$1" in
    openai)
      OPENAI_API_KEY=$matrix_openai_api_key
      export OPENAI_API_KEY
      ;;
    anthropic_oauth)
      ANTHROPIC_OAUTH_TOKEN=$matrix_anthropic_oauth_token
      export ANTHROPIC_OAUTH_TOKEN
      ;;
    anthropic_auth)
      ANTHROPIC_AUTH_TOKEN=$matrix_anthropic_auth_token
      export ANTHROPIC_AUTH_TOKEN
      ;;
    anthropic_api)
      ANTHROPIC_API_KEY=$matrix_anthropic_api_key
      export ANTHROPIC_API_KEY
      ;;
    deepseek)
      DEEPSEEK_API_KEY=$matrix_deepseek_api_key
      export DEEPSEEK_API_KEY
      ;;
    gemini)
      GEMINI_API_KEY=$matrix_gemini_api_key
      export GEMINI_API_KEY
      ;;
    kimi)
      KIMI_API_KEY=$matrix_kimi_api_key
      export KIMI_API_KEY
      ;;
    moonshot)
      MOONSHOT_API_KEY=$matrix_moonshot_api_key
      export MOONSHOT_API_KEY
      ;;
    openrouter)
      OPENROUTER_API_KEY=$matrix_openrouter_api_key
      export OPENROUTER_API_KEY
      ;;
  esac
}

run_case_target()
{
  matrix_case=$1
  matrix_output=$2
  (
    cd "$repo_root" || exit 127
    export_case_environment "$matrix_case"
    case "$target" in
      provider-live-smoke)
        if [ ! -x "$ava_tests_exe" ]; then
          echo "target executable is not runnable: $ava_tests_exe"
          exit 127
        fi
        "$ava_tests_exe" provider_live_smoke
        ;;
      model-dogfood)
        if [ ! -f "$script_dir/live-model-dogfood.sh" ]; then
          echo "live model dogfood script is missing: $script_dir/live-model-dogfood.sh"
          exit 127
        fi
        AVA_EXE=$ava_exe
        AVA_LIVE_DOGFOOD_ROOT=$run_root/$matrix_case/model-dogfood
        export AVA_EXE AVA_LIVE_DOGFOOD_ROOT
        if [ -n "${AVA_LIVE_PROVIDER_MATRIX_KEEP:-}" ]; then
          AVA_LIVE_DOGFOOD_KEEP=1
          export AVA_LIVE_DOGFOOD_KEEP
        fi
        sh "$script_dir/live-model-dogfood.sh"
        ;;
      coding-dogfood)
        if [ ! -f "$script_dir/live-coding-dogfood.sh" ]; then
          echo "live coding dogfood script is missing: $script_dir/live-coding-dogfood.sh"
          exit 127
        fi
        AVA_EXE=$ava_exe
        AVA_LIVE_CODING_DOGFOOD_ROOT=$run_root/$matrix_case/coding-dogfood
        export AVA_EXE AVA_LIVE_CODING_DOGFOOD_ROOT
        if [ -n "${AVA_LIVE_PROVIDER_MATRIX_KEEP:-}" ]; then
          AVA_LIVE_CODING_DOGFOOD_KEEP=1
          export AVA_LIVE_CODING_DOGFOOD_KEEP
        fi
        sh "$script_dir/live-coding-dogfood.sh"
        ;;
    esac
  ) > "$matrix_output" 2>&1
}

first_child_classification()
{
  sed -n 's/^classification=//p' "$1" | sed -n '1p'
}

contains_output()
{
  grep -qi "$1" "$2" 2>/dev/null
}

classify_output()
{
  matrix_status=$1
  matrix_output=$2

  child_classification=$(first_child_classification "$matrix_output")
  if [ -n "$child_classification" ]; then
    printf '%s' "$child_classification"
    return 0
  fi

  if [ "$matrix_status" -eq 0 ]; then
    if contains_output "tests skipped" "$matrix_output"; then
      printf '%s' "skipped/no credential"
    else
      printf '%s' "passed"
    fi
    return 0
  fi

  if [ "$matrix_status" -eq 77 ]; then
    if contains_output "not opted in" "$matrix_output"; then
      printf '%s' "skipped/not opted in"
    else
      printf '%s' "skipped/no credential"
    fi
    return 0
  fi

  if contains_output "401" "$matrix_output" || \
     contains_output "403" "$matrix_output" || \
     contains_output "authentication" "$matrix_output" || \
     contains_output "authorization" "$matrix_output" || \
     contains_output "auth error" "$matrix_output" || \
     contains_output "auth failed" "$matrix_output" || \
     contains_output "invalid auth" "$matrix_output" || \
     contains_output "credential" "$matrix_output" || \
     contains_output "api_key" "$matrix_output" || \
     contains_output "api-key" "$matrix_output" || \
     contains_output "invalid api key" "$matrix_output" || \
     contains_output "invalid_api_key" "$matrix_output" || \
     contains_output "unauthorized" "$matrix_output" || \
     contains_output "forbidden" "$matrix_output" || \
     contains_output "invalid key" "$matrix_output" || \
     contains_output "expired token" "$matrix_output" || \
     contains_output "permission denied" "$matrix_output" || \
     contains_output "does not have access" "$matrix_output"; then
    printf '%s' "credential/auth-blocked"
    return 0
  fi

  if contains_output "429" "$matrix_output" || \
     contains_output "rate limit" "$matrix_output" || \
     contains_output "rate_limit" "$matrix_output" || \
     contains_output "quota" "$matrix_output" || \
     contains_output "too many requests" "$matrix_output" || \
     contains_output "billing" "$matrix_output"; then
    printf '%s' "provider/rate-limited"
    return 0
  fi

  if contains_output "timeout" "$matrix_output" || \
     contains_output "timed out" "$matrix_output" || \
     contains_output "could not resolve" "$matrix_output" || \
     contains_output "couldn't connect" "$matrix_output" || \
     contains_output "failed to connect" "$matrix_output" || \
     contains_output "connection" "$matrix_output" || \
     contains_output "network" "$matrix_output" || \
     contains_output "curl" "$matrix_output" || \
     contains_output "dns" "$matrix_output" || \
     contains_output "proxy" "$matrix_output" || \
     contains_output "ssl" "$matrix_output" || \
     contains_output "tls" "$matrix_output"; then
    printf '%s' "network-blocked"
    return 0
  fi

  printf '%s' "AVA regression"
}

append_summary_row()
{
  matrix_case=$1
  matrix_provider=$2
  matrix_credential_env=$3
  matrix_model=$4
  matrix_classification=$5
  matrix_status=$6
  matrix_output=$7
  matrix_log_path=
  if [ -n "${AVA_LIVE_PROVIDER_MATRIX_KEEP:-}" ] && [ -f "$matrix_output" ]; then
    matrix_log_path=$matrix_output
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$matrix_case" "$matrix_provider" "$matrix_credential_env" "$matrix_model" "$target" "$matrix_classification" "$matrix_status" "$matrix_log_path" >> "$summary"
  printf '%s\t%s\t%s\t%s\t%s\n' \
    "$matrix_case" "$matrix_provider" "$matrix_credential_env" "$matrix_classification" "$matrix_status"
}

case_ids="openai anthropic_oauth anthropic_auth anthropic_api deepseek gemini kimi moonshot openrouter"
configured_count=0
passed_count=0
failed_count=0

echo "provider_live_matrix target=$target"
echo "summary=$summary"
printf '%s\t%s\t%s\t%s\t%s\n' "case" "provider" "credential_env" "classification" "exit_status"

for case_id in $case_ids; do
  provider=$(provider_for_case "$case_id")
  label=$(label_for_case "$case_id")
  credential_env=$(credential_env_for_case "$case_id")
  model=$(model_for_case "$case_id")
  output=$run_root/$case_id.out

  if ! is_enabled "${AVA_LIVE_PROVIDER_SMOKE:-}"; then
    append_summary_row "$case_id" "$provider" "$credential_env" "$model" "skipped/not opted in" "-" "$output"
    continue
  fi

  if ! credential_present "$case_id"; then
    append_summary_row "$case_id" "$provider" "$credential_env" "$model" "skipped/no credential" "-" "$output"
    continue
  fi

  configured_count=$((configured_count + 1))
  echo "running $label with $credential_env"
  run_case_target "$case_id" "$output"
  status=$?
  classification=$(classify_output "$status" "$output")
  append_summary_row "$case_id" "$provider" "$credential_env" "$model" "$classification" "$status" "$output"

  case "$classification" in
    passed)
      passed_count=$((passed_count + 1))
      ;;
    "AVA regression")
      failed_count=$((failed_count + 1))
      ;;
  esac
done

if ! is_enabled "${AVA_LIVE_PROVIDER_SMOKE:-}"; then
  matrix_classification="skipped/not opted in"
elif [ "$configured_count" -eq 0 ]; then
  matrix_classification="skipped/no credential"
elif [ "$failed_count" -ne 0 ]; then
  matrix_classification="AVA regression"
elif [ "$passed_count" -ne 0 ]; then
  matrix_classification="passed"
else
  matrix_classification="completed/non-passing"
fi

echo "matrix_classification=$matrix_classification"
echo "summary=$summary"
if [ -n "${AVA_LIVE_PROVIDER_MATRIX_KEEP:-}" ]; then
  echo "log_root=$run_root"
else
  echo "set AVA_LIVE_PROVIDER_MATRIX_KEEP=1 to retain child logs"
fi

if [ "$failed_count" -ne 0 ]; then
  exit 1
fi
exit 0
