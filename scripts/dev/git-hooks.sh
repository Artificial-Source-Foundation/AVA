#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

cd "$repo_root"

run_step() {
  local label="$1"
  shift
  printf '[hooks] %s\n' "$label"
  "$@"
}

run_low_priority() {
  if command -v ionice >/dev/null 2>&1; then
    ionice -c 3 nice -n 15 "$@"
    return
  fi

  nice -n 15 "$@"
}

run_exact_rust_test() {
  local label="$1"
  shift
  local output_file

  output_file="$(mktemp "${TMPDIR:-/tmp}/ava-exact-test.XXXXXX")"
  printf '[hooks] %s\n' "$label"

  if ! bash scripts/dev/run-rust-throttled.sh "$@" 2>&1 | tee "$output_file"; then
    rm -f "$output_file"
    return 1
  fi

  if ! grep -Eq '^running [1-9][0-9]* tests?$' "$output_file"; then
    printf '[hooks] exact test matched zero tests: %s\n' "$label" >&2
    rm -f "$output_file"
    return 1
  fi

  rm -f "$output_file"
}

run_rust_gate() {
  run_step "cargo fmt --all --check" \
    bash scripts/dev/run-rust-throttled.sh cargo fmt --all --check

  run_step "cargo clippy --workspace -- -D warnings" \
    bash scripts/dev/run-rust-throttled.sh cargo clippy --workspace -- -D warnings

  run_step "cargo nextest run -p ava-agent --test agent_loop --test reflection_loop -j 4 --status-level fail" \
    bash scripts/dev/run-rust-throttled.sh cargo nextest run -p ava-agent --test agent_loop --test reflection_loop -j 4 --status-level fail

  run_exact_rust_test "cargo test -p ava-agent control_plane::tests::control_plane_contract_fixture_matches_current_wire_contract -- --exact" \
    cargo test -p ava-agent control_plane::tests::control_plane_contract_fixture_matches_current_wire_contract -- --exact

  run_exact_rust_test "cargo test -p ava-agent control_plane::sessions::tests::run_context_from_session_recovers_effective_run_settings -- --exact" \
    cargo test -p ava-agent control_plane::sessions::tests::run_context_from_session_recovers_effective_run_settings -- --exact

  run_exact_rust_test "cargo test -p ava-agent control_plane::sessions::tests::run_context_from_session_falls_back_to_routing_identity -- --exact" \
    cargo test -p ava-agent control_plane::sessions::tests::run_context_from_session_falls_back_to_routing_identity -- --exact

  run_step "cargo nextest run -p ava-agent-orchestration --test stack_test --test e2e_test -j 4 --status-level fail" \
    bash scripts/dev/run-rust-throttled.sh cargo nextest run -p ava-agent-orchestration --test stack_test --test e2e_test -j 4 --status-level fail

  run_step "cargo nextest run -p ava-control-plane -j 4 --status-level fail" \
    bash scripts/dev/run-rust-throttled.sh cargo nextest run -p ava-control-plane -j 4 --status-level fail

  run_exact_rust_test "cargo test -p ava-web tests::resolve_plan_route_requires_request_id_and_preserves_pending_state -- --exact" \
    cargo test -p ava-web tests::resolve_plan_route_requires_request_id_and_preserves_pending_state -- --exact

  run_exact_rust_test "cargo test -p ava-web tests::clear_message_queue_rejects_unsupported_follow_up_targets -- --exact" \
    cargo test -p ava-web tests::clear_message_queue_rejects_unsupported_follow_up_targets -- --exact

  run_exact_rust_test "cargo test -p ava-web api::tests::projected_backend_events_preserve_required_correlation_fields -- --exact" \
    cargo test -p ava-web api::tests::projected_backend_events_preserve_required_correlation_fields -- --exact

  run_exact_rust_test "cargo test -p ava-web tests::retry_route_reuses_persisted_run_context_metadata -- --exact" \
    cargo test -p ava-web tests::retry_route_reuses_persisted_run_context_metadata -- --exact

  run_exact_rust_test "cargo test -p ava-web tests::edit_resend_route_reuses_persisted_run_context_metadata -- --exact" \
    cargo test -p ava-web tests::edit_resend_route_reuses_persisted_run_context_metadata -- --exact

  run_exact_rust_test "cargo test -p ava-web tests::regenerate_route_reuses_persisted_run_context_metadata -- --exact" \
    cargo test -p ava-web tests::regenerate_route_reuses_persisted_run_context_metadata -- --exact

  run_exact_rust_test "cargo test -p ava-tui app::tests::foreground_required_control_plane_events_are_visible_in_tui -- --exact" \
    cargo test -p ava-tui app::tests::foreground_required_control_plane_events_are_visible_in_tui -- --exact

  run_exact_rust_test "cargo test -p ava-tui app::tests::question_requests_use_shared_timeout_and_clear_lifecycle -- --exact" \
    cargo test -p ava-tui app::tests::question_requests_use_shared_timeout_and_clear_lifecycle -- --exact

  run_exact_rust_test "cargo test -p ava-tui app::tests::cancelling_tui_run_clears_pending_approval_via_shared_lifecycle -- --exact" \
    cargo test -p ava-tui app::tests::cancelling_tui_run_clears_pending_approval_via_shared_lifecycle -- --exact

  run_exact_rust_test "cargo test --manifest-path src-tauri/Cargo.toml events::tests::desktop_control_plane_event_shapes_follow_shared_requirements -- --exact" \
    cargo test --manifest-path src-tauri/Cargo.toml events::tests::desktop_control_plane_event_shapes_follow_shared_requirements -- --exact

  run_exact_rust_test "cargo test --manifest-path src-tauri/Cargo.toml commands::agent_commands::tests::submit_goal_returns_immediate_accepted_envelope_shape -- --exact" \
    cargo test --manifest-path src-tauri/Cargo.toml commands::agent_commands::tests::submit_goal_returns_immediate_accepted_envelope_shape -- --exact

  run_exact_rust_test "cargo test --manifest-path src-tauri/Cargo.toml commands::agent_commands::tests::replay_commands_share_submit_accepted_envelope_semantics -- --exact" \
    cargo test --manifest-path src-tauri/Cargo.toml commands::agent_commands::tests::replay_commands_share_submit_accepted_envelope_semantics -- --exact

  run_exact_rust_test "cargo test --manifest-path src-tauri/Cargo.toml commands::agent_commands::tests::take_matching_pending_reply_only_consumes_matching_request_ids -- --exact" \
    cargo test --manifest-path src-tauri/Cargo.toml commands::agent_commands::tests::take_matching_pending_reply_only_consumes_matching_request_ids -- --exact

  run_step "cargo check -p ava-tui --features web" \
    bash scripts/dev/run-rust-throttled.sh cargo check -p ava-tui --features web

  run_step "cargo nextest run -p ava-tools -p ava-review -j 4 --status-level fail" \
    bash scripts/dev/run-rust-throttled.sh cargo nextest run -p ava-tools -p ava-review -j 4 --status-level fail
}

run_workspace_compile_smoke() {
  run_step "cargo check --workspace --all-targets" \
    bash scripts/dev/run-rust-throttled.sh cargo check --workspace --all-targets
}

run_tauri_compile_smoke() {
  run_step "cargo check --manifest-path src-tauri/Cargo.toml --lib" \
    bash scripts/dev/run-rust-throttled.sh cargo check --manifest-path src-tauri/Cargo.toml --lib
}

run_web_compile_smoke() {
  run_step "cargo check -p ava-web" \
    bash scripts/dev/run-rust-throttled.sh cargo check -p ava-web
}

run_config_compile_smoke() {
  run_step "cargo check -p ava-config" \
    bash scripts/dev/run-rust-throttled.sh cargo check -p ava-config
}

run_frontend_gate() {
  run_step "pnpm typecheck" run_low_priority pnpm typecheck
  run_step "pnpm lint" run_low_priority pnpm lint
}

is_zero_oid() {
  local oid="$1"

  [ -n "$oid" ] || return 1

  case "$oid" in
    *[!0]*)
      return 1
      ;;
    *)
      return 0
      ;;
  esac
}

collect_staged_files() {
  staged_files=()

  while IFS= read -r -d '' file; do
    staged_files+=("$file")
  done < <(git diff --cached --name-only --diff-filter=ACMR -z)
}

create_staged_snapshot_root() {
  mktemp -d "${TMPDIR:-/tmp}/ava-hook-staged.XXXXXX"
}

materialize_staged_file() {
  local file="$1"
  local snapshot_path="$staged_snapshot_root/$file"

  mkdir -p "$(dirname -- "$snapshot_path")"
  git show ":$file" > "$snapshot_path"
  printf '%s\n' "$snapshot_path"
}

resolve_push_range() {
  if [ -n "${AVA_HOOK_RANGE:-}" ]; then
    printf '%s\n' "$AVA_HOOK_RANGE"
    return
  fi

  if upstream_ref="$(git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null)"; then
    printf '%s..HEAD\n' "$upstream_ref"
    return
  fi

  origin_head_ref="$(git symbolic-ref --quiet refs/remotes/origin/HEAD 2>/dev/null || true)"
  if [ -n "$origin_head_ref" ]; then
    printf '%s..HEAD\n' "${origin_head_ref#refs/remotes/}"
    return
  fi

  for candidate in origin/main origin/master main master; do
    if git rev-parse --verify --quiet "$candidate" >/dev/null; then
      printf '%s..HEAD\n' "$candidate"
      return
    fi
  done

  if git rev-parse --verify --quiet HEAD^ >/dev/null; then
    printf 'HEAD^..HEAD\n'
    return
  fi

  printf 'HEAD\n'
}

append_unique_push_file() {
  local file="$1"
  local existing

  [ -n "$file" ] || return

  for existing in "${push_files[@]}"; do
    if [ "$existing" = "$file" ]; then
      return
    fi
  done

  push_files+=("$file")
}

append_push_files_from_stream() {
  local file

  while IFS= read -r file; do
    append_unique_push_file "$file"
  done
}

append_push_files_from_range() {
  local range="$1"

  append_push_files_from_stream < <(git diff --name-only --diff-filter=ACMRD "$range")
}

append_push_files_from_new_ref() {
  local local_sha="$1"
  local commit
  local have_commit=0

  while IFS= read -r commit; do
    [ -n "$commit" ] || continue
    have_commit=1
    append_push_files_from_stream < <(git diff-tree --root --no-commit-id --name-only --diff-filter=ACMRD -r "$commit")
  done < <(git rev-list "$local_sha" --not --remotes)

  if [ "$have_commit" -eq 1 ]; then
    return 0
  fi

  return 1
}

collect_push_updates_from_stdin() {
  stdin_updates=()
  local local_ref local_sha remote_ref remote_sha

  if [ -t 0 ]; then
    return
  fi

  while IFS=' ' read -r local_ref local_sha remote_ref remote_sha; do
    [ -n "$local_ref$local_sha$remote_ref$remote_sha" ] || continue
    stdin_updates+=("$local_ref $local_sha $remote_ref $remote_sha")
  done
}

collect_push_files_from_stdin_updates() {
  local update local_ref local_sha remote_ref remote_sha
  local used_stdin=0

  collect_push_updates_from_stdin

  if [ "${#stdin_updates[@]}" -eq 0 ]; then
    return 1
  fi

  for update in "${stdin_updates[@]}"; do
    IFS=' ' read -r local_ref local_sha remote_ref remote_sha <<EOF
$update
EOF

    if is_zero_oid "$local_sha"; then
      continue
    fi

    used_stdin=1

    if is_zero_oid "$remote_sha"; then
      if append_push_files_from_new_ref "$local_sha"; then
        continue
      fi

      return 1
    fi

    append_push_files_from_range "$remote_sha..$local_sha"
  done

  if [ "$used_stdin" -eq 0 ]; then
    return 1
  fi

  return 0
}

collect_push_files() {
  push_files=()
  push_range=''

  if [ -n "${AVA_HOOK_FILES:-}" ]; then
    while IFS= read -r file; do
      [ -n "$file" ] || continue
      push_files+=("$file")
    done <<EOF
$AVA_HOOK_FILES
EOF
    return
  fi

  if collect_push_files_from_stdin_updates; then
    return
  fi

  push_range="$(resolve_push_range)"

  if [ "$push_range" = 'HEAD' ]; then
    append_push_files_from_stream < <(git diff-tree --root --no-commit-id --name-only --diff-filter=ACMRD -r HEAD)
    return
  fi

  append_push_files_from_range "$push_range"
}

is_docs_path() {
  case "$1" in
    docs/*|.github/CONTRIBUTING.md|.github/pull_request_template.md|AGENTS.md|CHANGELOG.md|CLAUDE.md|README.md)
      return 0
      ;;
  esac

  return 1
}

is_frontend_path() {
  case "$1" in
    src/*|e2e/*|plugins/*|package.json|pnpm-lock.yaml|tsconfig*.json|vite.config.*|vitest.config.*|playwright.config.*|biome.json|biome.jsonc|eslint.config.*|tailwind.config.*|postcss.config.*|index.html)
      return 0
      ;;
  esac

  return 1
}

is_workspace_rust_path() {
  case "$1" in
    Cargo.toml|Cargo.lock|rust-toolchain|rust-toolchain.toml|crates/*/Cargo.toml)
      return 0
      ;;
  esac

  return 1
}

is_tauri_path() {
  case "$1" in
    src-tauri/*)
      return 0
      ;;
  esac

  return 1
}

is_web_path() {
  case "$1" in
    crates/ava-web/*)
      return 0
      ;;
  esac

  return 1
}

is_config_path() {
  case "$1" in
    crates/ava-config/*)
      return 0
      ;;
  esac

  return 1
}

run_pre_commit() {
  collect_staged_files

  if [ "${#staged_files[@]}" -eq 0 ]; then
    printf '[hooks] pre-commit: no staged files\n'
    return
  fi

  staged_snapshot_root="$(create_staged_snapshot_root)"
  trap 'rm -rf "$staged_snapshot_root"' EXIT

  rust_files=()
  biome_files=()
  ts_files=()
  snapshot_rust_files=()
  snapshot_biome_files=()
  snapshot_ts_files=()

  for file in "${staged_files[@]}"; do
    case "$file" in
      *.rs)
        rust_files+=("$file")
        snapshot_rust_files+=("$(materialize_staged_file "$file")")
        ;;
    esac

    case "$file" in
      *.ts|*.tsx|*.js|*.jsx|*.json|*.css)
        if is_frontend_path "$file"; then
          biome_files+=("$file")
          snapshot_biome_files+=("$(materialize_staged_file "$file")")
        fi
        ;;
    esac

    case "$file" in
      *.ts|*.tsx)
        ts_files+=("$file")
        snapshot_ts_files+=("$(materialize_staged_file "$file")")
        ;;
    esac
  done

  if [ "${#rust_files[@]}" -gt 0 ]; then
    run_step "pre-commit: rustfmt --check on ${#rust_files[@]} staged Rust file(s)" \
      rustfmt --check --config-path "$repo_root/rustfmt.toml" --config skip_children=true "${snapshot_rust_files[@]}"
  fi

  if [ "${#biome_files[@]}" -gt 0 ]; then
    run_step "pre-commit: biome check on ${#biome_files[@]} staged frontend file(s)" \
      pnpm exec biome check "${snapshot_biome_files[@]}"
  fi

  if [ "${#ts_files[@]}" -gt 0 ]; then
    run_step "pre-commit: oxlint on ${#ts_files[@]} staged TypeScript file(s)" \
      pnpm exec oxlint "${snapshot_ts_files[@]}"
  fi

  printf '[hooks] pre-commit complete\n'
}

run_pre_push() {
  collect_push_files

  if [ -n "$push_range" ]; then
    printf '[hooks] pre-push range: %s\n' "$push_range"
  fi

  if [ "${#push_files[@]}" -eq 0 ]; then
    printf '[hooks] pre-push: no changed files detected for push\n'
    return
  fi

  docs_only=1
  needs_frontend=0
  needs_rust=0
  needs_workspace_compile=0
  needs_tauri_compile=0
  needs_web_compile=0
  needs_config_compile=0

  for file in "${push_files[@]}"; do
    if is_docs_path "$file"; then
      continue
    fi

    docs_only=0

    if is_frontend_path "$file"; then
      needs_frontend=1
      continue
    fi

    needs_rust=1

    if is_workspace_rust_path "$file"; then
      needs_workspace_compile=1
    fi

    if is_tauri_path "$file"; then
      needs_tauri_compile=1
    fi

    if is_web_path "$file"; then
      needs_web_compile=1
    fi

    if is_config_path "$file"; then
      needs_config_compile=1
    fi
  done

  if [ "$docs_only" -eq 1 ]; then
    printf '[hooks] pre-push: docs-only changes detected; skipping code validation gates\n'
    return
  fi

  if [ "$needs_frontend" -eq 1 ]; then
    printf '[hooks] pre-push: frontend-sensitive changes detected\n'
    run_frontend_gate
  fi

  if [ "$needs_rust" -eq 1 ]; then
    printf '[hooks] pre-push: Rust/general repo changes detected\n'
    run_rust_gate

    if [ "$needs_workspace_compile" -eq 1 ]; then
      printf '[hooks] pre-push: workspace wiring changes detected\n'
      run_workspace_compile_smoke
    fi

    if [ "$needs_tauri_compile" -eq 1 ]; then
      printf '[hooks] pre-push: desktop/Tauri changes detected\n'
      run_tauri_compile_smoke
    fi

    if [ "$needs_web_compile" -eq 1 ]; then
      printf '[hooks] pre-push: ava-web changes detected\n'
      run_web_compile_smoke
    fi

    if [ "$needs_config_compile" -eq 1 ]; then
      printf '[hooks] pre-push: ava-config changes detected\n'
      run_config_compile_smoke
    fi
  fi

  printf '[hooks] pre-push complete\n'
}

usage() {
  printf 'usage: %s <check|check-frontend|pre-commit|pre-push>\n' "$0" >&2
}

case "${1:-}" in
  check)
    run_rust_gate
    ;;
  check-frontend)
    run_frontend_gate
    ;;
  pre-commit)
    run_pre_commit
    ;;
  pre-push)
    run_pre_push
    ;;
  *)
    usage
    exit 64
    ;;
esac
