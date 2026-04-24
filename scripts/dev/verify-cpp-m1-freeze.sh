#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

cd "$repo_root"

resolve_range() {
  if [ -n "${1:-}" ]; then
    printf '%s\n' "$1"
    return
  fi

  if [ -n "${GITHUB_BASE_REF:-}" ] && git rev-parse --verify --quiet "origin/${GITHUB_BASE_REF}" >/dev/null; then
    printf 'origin/%s...HEAD\n' "$GITHUB_BASE_REF"
    return
  fi

  if git rev-parse --verify --quiet HEAD^ >/dev/null; then
    printf 'HEAD^..HEAD\n'
    return
  fi

  printf 'HEAD\n'
}

is_frozen_path() {
  case "$1" in
    crates/ava-control-plane/src/commands.rs|\
    crates/ava-control-plane/src/events.rs|\
    crates/ava-control-plane/src/interactive.rs|\
    crates/ava-control-plane/src/sessions.rs|\
    crates/ava-control-plane/src/queue.rs|\
    crates/ava-control-plane/src/orchestration.rs|\
    crates/ava-control-plane/src/lib.rs|\
    crates/ava-types/src/lib.rs|\
    crates/ava-types/src/message.rs|\
    crates/ava-types/src/tool.rs|\
    crates/ava-types/src/session.rs|\
    crates/ava-tools/src/registry.rs|\
    crates/ava-agent/src/control_plane/mod.rs|\
    crates/ava-agent/src/control_plane/events.rs|\
    crates/ava-agent/src/control_plane/sessions.rs|\
    crates/ava-session/src/lib.rs|\
    crates/ava-session/src/manager.rs|\
    crates/ava-session/src/tree.rs|\
    crates/ava-session/src/search.rs|\
    crates/ava-session/src/helpers.rs|\
    crates/ava-session/src/diff_tracking.rs|\
    crates/ava-config/src/lib.rs|\
    crates/ava-config/src/credentials.rs|\
    crates/ava-config/src/keychain.rs|\
    crates/ava-config/src/trust.rs|\
    crates/ava-config/src/agents.rs|\
    crates/ava-config/src/routing.rs|\
    crates/ava-config/src/thinking.rs|\
    crates/ava-config/src/credential_commands.rs|\
    crates/ava-config/src/model_catalog/mod.rs|\
    crates/ava-config/src/model_catalog/registry.rs|\
    crates/ava-config/src/model_catalog/types.rs|\
    crates/ava-config/src/model_catalog/fallback.rs|\
    crates/ava-tui/src/lib.rs|\
    crates/ava-tui/src/config/cli.rs|\
    crates/ava-tui/src/headless/mod.rs|\
    crates/ava-tui/src/headless/single.rs|\
    crates/ava-tui/src/headless/common.rs|\
    crates/ava-tui/src/headless/input.rs|\
    crates/ava-tui/src/main.rs|\
    crates/ava-permissions/src/lib.rs|\
    crates/ava-permissions/src/inspector.rs|\
    crates/ava-permissions/src/policy.rs|\
    crates/ava-permissions/src/tags.rs|\
    crates/ava-tools/src/permission_middleware.rs|\
    crates/ava-agent-orchestration/src/stack/mod.rs|\
    crates/ava-agent/src/run_context.rs|\
    docs/architecture/cpp-contract-freeze-m1.md|\
    docs/architecture/cpp-m1-event-stream-parity-checklist.md|\
    scripts/dev/verify-cpp-m1-freeze.sh)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

range="$(resolve_range "${1:-}")"
changed_files=()

if [ "$range" = "HEAD" ]; then
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    changed_files+=("$file")
  done < <(git diff-tree --root --no-commit-id --name-only -r HEAD)
else
  while IFS= read -r file; do
    [ -n "$file" ] || continue
    changed_files+=("$file")
  done < <(git diff --name-only --diff-filter=ACMRD "$range")
fi

if [ "${#changed_files[@]}" -eq 0 ]; then
  printf '[freeze-m1] No changed files in range (%s); skipping.\n' "$range"
  exit 0
fi

frozen_changes=()
has_freeze_doc_update=0

for file in "${changed_files[@]}"; do
  if is_frozen_path "$file"; then
    frozen_changes+=("$file")
  fi

  case "$file" in
    docs/architecture/cpp-contract-freeze-m1.md|docs/architecture/cpp-m1-event-stream-parity-checklist.md)
      has_freeze_doc_update=1
      ;;
  esac
done

if [ "${#frozen_changes[@]}" -eq 0 ]; then
  printf '[freeze-m1] No C++ Milestone 1 freeze-governed files changed.\n'
  exit 0
fi

printf '[freeze-m1] Freeze-governed files changed (%s):\n' "${#frozen_changes[@]}"
for file in "${frozen_changes[@]}"; do
  printf '  - %s\n' "$file"
done

if [ "${AVA_CPP_M1_FREEZE_LIFT:-0}" != "1" ]; then
  printf '[freeze-m1] Missing required freeze-lift approval signal.\n'
  printf '[freeze-m1] Add the PR label `freeze-lift` (or set AVA_CPP_M1_FREEZE_LIFT=1 for local/manual verification).\n'
  exit 1
fi

if [ "$has_freeze_doc_update" -ne 1 ]; then
  printf '[freeze-m1] Freeze docs were not updated in this change.\n'
  printf '[freeze-m1] Update docs/architecture/cpp-contract-freeze-m1.md and/or docs/architecture/cpp-m1-event-stream-parity-checklist.md in the same freeze-lift PR.\n'
  exit 1
fi

printf '[freeze-m1] Freeze-lift checks passed.\n'
