#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/git-hooks-regression.XXXXXX")"

cleanup() {
  rm -rf "$tmp_root"
}
trap cleanup EXIT

assert_contains() {
  local haystack="$1"
  local needle="$2"
  local label="$3"

  if [[ "$haystack" != *"$needle"* ]]; then
    printf 'assertion failed: %s\nexpected to find: %s\n' "$label" "$needle" >&2
    exit 1
  fi
}

assert_not_contains() {
  local haystack="$1"
  local needle="$2"
  local label="$3"

  if [[ "$haystack" == *"$needle"* ]]; then
    printf 'assertion failed: %s\ndid not expect to find: %s\n' "$label" "$needle" >&2
    exit 1
  fi
}

create_stub_bin() {
  local repo="$1"

  mkdir -p "$repo/bin"

  cat > "$repo/bin/cargo" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ -n "${AVA_TEST_HOOK_LOG:-}" ]; then
  printf 'cargo %s\n' "$*" >> "$AVA_TEST_HOOK_LOG"
fi

if [ "${1:-}" = 'fmt' ]; then
  check_mode=0

  for arg in "$@"; do
    if [ "$arg" = '--check' ]; then
      check_mode=1
      break
    fi
  done

  if [ "$check_mode" -eq 0 ]; then
    for arg in "$@"; do
      case "$arg" in
        *.rs)
          [ -f "$arg" ] || continue
          python3 - "$arg" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
path.write_text(path.read_text() + "\n// formatted by stub\n")
PY
          ;;
      esac
    done
  fi
fi

exit 0
EOF

  cat > "$repo/bin/pnpm" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ -n "${AVA_TEST_HOOK_LOG:-}" ]; then
  printf 'pnpm %s\n' "$*" >> "$AVA_TEST_HOOK_LOG"
fi

write_mode=0
for arg in "$@"; do
  if [ "$arg" = '--write' ]; then
    write_mode=1
    break
  fi
done

if [ "$write_mode" -eq 1 ]; then
  for arg in "$@"; do
    case "$arg" in
      *.ts|*.tsx|*.js|*.jsx|*.json|*.css)
        [ -f "$arg" ] || continue
        python3 - "$arg" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
path.write_text(path.read_text() + "\n// formatted by stub\n")
PY
        ;;
    esac
  done
fi

if [ -n "${AVA_PNPM_FAIL_IF_CONTAINS:-}" ]; then
  for arg in "$@"; do
    case "$arg" in
      *.ts|*.tsx|*.js|*.jsx|*.json|*.css)
        [ -f "$arg" ] || continue
        if grep -F -q "$AVA_PNPM_FAIL_IF_CONTAINS" "$arg"; then
          printf 'pnpm received content marker in: %s\n' "$arg" >&2
          exit 1
        fi
        ;;
    esac
  done
fi

exit 0
EOF

  cat > "$repo/bin/rustfmt" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ -n "${AVA_TEST_HOOK_LOG:-}" ]; then
  printf 'rustfmt %s\n' "$*" >> "$AVA_TEST_HOOK_LOG"
fi

check_mode=0
for arg in "$@"; do
  if [ "$arg" = '--check' ]; then
    check_mode=1
    break
  fi
done

for arg in "$@"; do
  case "$arg" in
    "${AVA_RUSTFMT_FAIL_ON_ARG:-}")
      printf 'rustfmt received blocked arg: %s\n' "$arg" >&2
      exit 1
      ;;
  esac
done

if [ -n "${AVA_RUSTFMT_FAIL_IF_CONTAINS:-}" ]; then
  for arg in "$@"; do
    case "$arg" in
      *.rs)
        [ -f "$arg" ] || continue
        if grep -F -q "$AVA_RUSTFMT_FAIL_IF_CONTAINS" "$arg"; then
          printf 'rustfmt received content marker in: %s\n' "$arg" >&2
          exit 1
        fi
        ;;
    esac
  done
fi

if [ "$check_mode" -eq 0 ]; then
  for arg in "$@"; do
    case "$arg" in
      *.rs)
        [ -f "$arg" ] || continue
        python3 - "$arg" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
path.write_text(path.read_text() + "\n// formatted by rustfmt stub\n")
PY
        ;;
    esac
  done
fi

exit 0
EOF

  cat > "$repo/bin/lefthook" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ -n "${AVA_TEST_HOOK_LOG:-}" ]; then
  printf 'lefthook %s\n' "$*" >> "$AVA_TEST_HOOK_LOG"
fi

if [ "${1:-}" != 'run' ]; then
  exit 0
fi

hook_name="${2:-}"
shift 2 || true

case "$hook_name" in
  pre-commit|pre-push)
    exec bash scripts/dev/git-hooks.sh "$hook_name" "$@"
    ;;
  *)
    exit 0
    ;;
esac
EOF

  chmod +x "$repo/bin/cargo" "$repo/bin/pnpm" "$repo/bin/rustfmt" "$repo/bin/lefthook"
}

create_repo() {
  local name="$1"
  local repo="$tmp_root/$name"

  mkdir -p "$repo/scripts/dev"
  cp "$repo_root/scripts/dev/git-hooks.sh" "$repo/scripts/dev/"
  cp "$repo_root/scripts/dev/run-rust-throttled.sh" "$repo/scripts/dev/"
  create_stub_bin "$repo"

  git init -q "$repo"
  git -C "$repo" config user.name 'AVA Tests'
  git -C "$repo" config user.email 'ava-tests@example.com'
  mkdir -p "$repo/.git/hooks"
  cp "$repo_root/.git/hooks/pre-push" "$repo/.git/hooks/pre-push"
  cp "$repo_root/.git/hooks/pre-commit" "$repo/.git/hooks/pre-commit"
  chmod +x "$repo/.git/hooks/pre-push" "$repo/.git/hooks/pre-commit"

  printf '%s\n' "$repo"
}

commit_all() {
  local repo="$1"
  local message="$2"

  git -C "$repo" add -A
  LEFTHOOK=0 git -C "$repo" commit -q -m "$message"
}

assert_equal() {
  local actual="$1"
  local expected="$2"
  local label="$3"

  if [ "$actual" != "$expected" ]; then
    printf 'assertion failed: %s\nexpected: %s\nactual: %s\n' "$label" "$expected" "$actual" >&2
    exit 1
  fi
}

run_hook() {
  local repo="$1"
  local stdin_payload="$2"
  local hook_log="$repo/hook.log"

  : > "$hook_log"

  if [ -n "$stdin_payload" ]; then
    (
      cd "$repo"
      export PATH="$repo/bin:$PATH"
      export AVA_TEST_HOOK_LOG="$hook_log"
      bash scripts/dev/git-hooks.sh pre-push <<EOF
$stdin_payload
EOF
    )
    return
  fi

  (
    cd "$repo"
    export PATH="$repo/bin:$PATH"
    export AVA_TEST_HOOK_LOG="$hook_log"
    bash scripts/dev/git-hooks.sh pre-push
  )
}

run_installed_hook() {
  local repo="$1"
  local stdin_payload="$2"
  local hook_log="$repo/hook.log"

  : > "$hook_log"

  if [ -n "$stdin_payload" ]; then
    (
      cd "$repo"
      export PATH="$repo/bin:$PATH"
      export AVA_TEST_HOOK_LOG="$hook_log"
      ./.git/hooks/pre-push <<EOF
$stdin_payload
EOF
    )
    return
  fi

  (
    cd "$repo"
    export PATH="$repo/bin:$PATH"
    export AVA_TEST_HOOK_LOG="$hook_log"
    ./.git/hooks/pre-push
  )
}

test_delete_only_docs_uses_docs_policy_from_stdin() {
  local repo output base head stdin_payload
  repo="$(create_repo docs-delete)"

  mkdir -p "$repo/docs"
  printf '# docs\n' > "$repo/docs/guide.md"
  commit_all "$repo" 'add docs file'
  base="$(git -C "$repo" rev-parse HEAD)"

  git -C "$repo" rm -q docs/guide.md
  LEFTHOOK=0 git -C "$repo" commit -q -m 'delete docs file'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'docs-only changes detected' 'docs delete-only push should stay docs-only'
  assert_not_contains "$output" '[hooks] pnpm typecheck' 'docs delete-only push should not run frontend gate'
  assert_not_contains "$output" '[hooks] cargo fmt --all --check' 'docs delete-only push should not run rust gate'
}

test_dotgithub_contributing_changes_treated_as_docs_from_stdin() {
  local repo output base head stdin_payload
  repo="$(create_repo github-contributing-docs)"

  mkdir -p "$repo/.github"
  printf '# AVA Contribution Policy\n' > "$repo/.github/CONTRIBUTING.md"
  commit_all "$repo" 'add .github contribution doc'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf '# AVA Contribution Policy\n\nUpdated contribution policy note.\n' > "$repo/.github/CONTRIBUTING.md"
  git -C "$repo" add .github/CONTRIBUTING.md
  LEFTHOOK=0 git -C "$repo" commit -q -m 'update .github contribution doc'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'docs-only changes detected' '.github/CONTRIBUTING.md changes should stay docs-only'
  assert_not_contains "$output" '[hooks] pnpm typecheck' '.github/CONTRIBUTING.md changes should not run frontend gate'
  assert_not_contains "$output" '[hooks] cargo fmt --all --check' '.github/CONTRIBUTING.md changes should not run rust gate'
}

test_dotgithub_pull_request_template_changes_treated_as_docs_from_stdin() {
  local repo output base head stdin_payload
  repo="$(create_repo github-pr-template-docs)"

  mkdir -p "$repo/.github"
  printf '# PR template\n' > "$repo/.github/pull_request_template.md"
  commit_all "$repo" 'add PR template'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf '# PR template\n\nUpdated instructions.\n' > "$repo/.github/pull_request_template.md"
  git -C "$repo" add .github/pull_request_template.md
  LEFTHOOK=0 git -C "$repo" commit -q -m 'update PR template'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'docs-only changes detected' '.github/pull_request_template.md changes should stay docs-only'
  assert_not_contains "$output" '[hooks] pnpm typecheck' '.github/pull_request_template.md changes should not run frontend gate'
  assert_not_contains "$output" '[hooks] cargo fmt --all --check' '.github/pull_request_template.md changes should not run rust gate'
}

test_delete_only_frontend_runs_frontend_gate_from_stdin() {
  local repo output base head stdin_payload
  repo="$(create_repo frontend-delete)"

  mkdir -p "$repo/src"
  printf 'export const value = 1\n' > "$repo/src/app.ts"
  commit_all "$repo" 'add frontend file'
  base="$(git -C "$repo" rev-parse HEAD)"

  git -C "$repo" rm -q src/app.ts
  LEFTHOOK=0 git -C "$repo" commit -q -m 'delete frontend file'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'frontend-sensitive changes detected' 'frontend delete-only push should trigger frontend gate'
  assert_contains "$output" '[hooks] pnpm typecheck' 'frontend delete-only push should run typecheck'
  assert_contains "$output" '[hooks] pnpm lint' 'frontend delete-only push should run lint'
  assert_not_contains "$output" '[hooks] cargo fmt --all --check' 'frontend delete-only push should not run rust gate'
}

test_delete_only_rust_runs_rust_gate_from_stdin() {
  local repo output base head stdin_payload
  repo="$(create_repo rust-delete)"

  mkdir -p "$repo/crates/demo/src"
  printf 'pub fn demo() {}\n' > "$repo/crates/demo/src/lib.rs"
  commit_all "$repo" 'add rust file'
  base="$(git -C "$repo" rev-parse HEAD)"

  git -C "$repo" rm -q crates/demo/src/lib.rs
  LEFTHOOK=0 git -C "$repo" commit -q -m 'delete rust file'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'Rust/general repo changes detected' 'rust delete-only push should trigger rust gate'
  assert_contains "$output" '[hooks] cargo fmt --all --check' 'rust delete-only push should run rust gate'
  assert_not_contains "$output" '[hooks] pnpm typecheck' 'rust delete-only push should not run frontend gate'
}

test_pre_push_falls_back_to_inferred_range_without_stdin() {
  local repo output base
  repo="$(create_repo inferred-range)"

  mkdir -p "$repo/src"
  printf 'export const value = 1\n' > "$repo/src/app.ts"
  commit_all "$repo" 'add frontend file'
  base="$(git -C "$repo" rev-parse HEAD)"

  git -C "$repo" update-ref refs/remotes/origin/master "$base"
  git -C "$repo" symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/master

  git -C "$repo" rm -q src/app.ts
  LEFTHOOK=0 git -C "$repo" commit -q -m 'delete frontend file'

  output="$(run_hook "$repo" '')"

  assert_contains "$output" 'pre-push range: origin/master..HEAD' 'missing stdin should fall back to inferred range'
  assert_contains "$output" 'frontend-sensitive changes detected' 'fallback range should still classify frontend deletes'
  assert_contains "$output" '[hooks] pnpm typecheck' 'fallback range should run frontend typecheck'
  assert_contains "$output" '[hooks] pnpm lint' 'fallback range should run frontend lint'
}

test_runtime_markdown_inside_crate_triggers_rust_gate() {
  local repo output base head stdin_payload
  repo="$(create_repo runtime-markdown)"

  mkdir -p "$repo/crates/demo/src"
  printf 'prompt v1\n' > "$repo/crates/demo/src/system_prompt.md"
  commit_all "$repo" 'add runtime markdown'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf 'prompt v2\n' > "$repo/crates/demo/src/system_prompt.md"
  commit_all "$repo" 'update runtime markdown'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'Rust/general repo changes detected' 'runtime markdown under crates/src should trigger rust gate'
  assert_contains "$output" '[hooks] cargo fmt --all --check' 'runtime markdown under crates/src should not bypass rust gate'
  assert_not_contains "$output" 'docs-only changes detected' 'runtime markdown under crates/src should not be treated as docs-only'
}

test_plugin_typescript_triggers_frontend_gate() {
  local repo output base head stdin_payload
  repo="$(create_repo plugin-typescript)"

  mkdir -p "$repo/plugins/sdk/src"
  printf 'export const value = 1\n' > "$repo/plugins/sdk/src/index.ts"
  commit_all "$repo" 'add plugin ts file'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf 'export const value = 2\n' > "$repo/plugins/sdk/src/index.ts"
  commit_all "$repo" 'update plugin ts file'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'frontend-sensitive changes detected' 'plugin TypeScript should trigger frontend gate'
  assert_contains "$output" '[hooks] pnpm typecheck' 'plugin TypeScript should run frontend typecheck'
  assert_contains "$output" '[hooks] pnpm lint' 'plugin TypeScript should run frontend lint'
  assert_not_contains "$output" '[hooks] cargo fmt --all --check' 'plugin TypeScript should not trigger rust gate by itself'
}

test_workspace_manifest_triggers_workspace_compile_smoke() {
  local repo output base head stdin_payload
  repo="$(create_repo workspace-manifest)"

  printf '[workspace]\nmembers = []\n' > "$repo/Cargo.toml"
  commit_all "$repo" 'add workspace manifest'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf '[workspace]\nmembers = []\nresolver = "2"\n' > "$repo/Cargo.toml"
  git -C "$repo" add Cargo.toml
  LEFTHOOK=0 git -C "$repo" commit -q -m 'update workspace manifest'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'workspace wiring changes detected' 'workspace manifest should trigger workspace compile smoke'
  assert_contains "$output" '[hooks] cargo check --workspace --all-targets' 'workspace manifest should run workspace compile smoke'
}

test_tauri_changes_trigger_tauri_compile_smoke() {
  local repo output base head stdin_payload
  repo="$(create_repo tauri-compile)"

  mkdir -p "$repo/src-tauri/src"
  printf 'pub fn desktop() {}\n' > "$repo/src-tauri/src/lib.rs"
  commit_all "$repo" 'add tauri source'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf 'pub fn desktop() { println!("hi"); }\n' > "$repo/src-tauri/src/lib.rs"
  git -C "$repo" add src-tauri/src/lib.rs
  LEFTHOOK=0 git -C "$repo" commit -q -m 'update tauri source'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'desktop/Tauri changes detected' 'tauri path should trigger desktop compile smoke'
  assert_contains "$output" '[hooks] cargo check --manifest-path src-tauri/Cargo.toml --lib' 'tauri path should run desktop compile smoke'
}

test_ava_web_changes_trigger_web_compile_smoke() {
  local repo output base head stdin_payload
  repo="$(create_repo ava-web-compile)"

  mkdir -p "$repo/crates/ava-web/src"
  printf 'pub fn web() {}\n' > "$repo/crates/ava-web/src/lib.rs"
  commit_all "$repo" 'add ava-web source'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf 'pub fn web() { println!("hi"); }\n' > "$repo/crates/ava-web/src/lib.rs"
  git -C "$repo" add crates/ava-web/src/lib.rs
  LEFTHOOK=0 git -C "$repo" commit -q -m 'update ava-web source'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'ava-web changes detected' 'ava-web path should trigger web compile smoke'
  assert_contains "$output" '[hooks] cargo check -p ava-web' 'ava-web path should run web compile smoke'
}

test_ava_config_changes_trigger_config_compile_smoke() {
  local repo output base head stdin_payload
  repo="$(create_repo ava-config-compile)"

  mkdir -p "$repo/crates/ava-config/src"
  printf 'pub fn config() {}\n' > "$repo/crates/ava-config/src/lib.rs"
  commit_all "$repo" 'add ava-config source'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf 'pub fn config() { println!("hi"); }\n' > "$repo/crates/ava-config/src/lib.rs"
  git -C "$repo" add crates/ava-config/src/lib.rs
  LEFTHOOK=0 git -C "$repo" commit -q -m 'update ava-config source'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'ava-config changes detected' 'ava-config path should trigger config compile smoke'
  assert_contains "$output" '[hooks] cargo check -p ava-config' 'ava-config path should run config compile smoke'
}

test_installed_pre_push_wrapper_invokes_lefthook_and_forwards_stdin() {
  local repo output base head stdin_payload hook_log
  repo="$(create_repo lefthook-wrapper)"
  hook_log="$repo/hook.log"

  mkdir -p "$repo/src"
  printf 'export const value = 1\n' > "$repo/src/app.ts"
  commit_all "$repo" 'add frontend file'
  base="$(git -C "$repo" rev-parse HEAD)"

  printf 'export const value = 2\n' > "$repo/src/app.ts"
  git -C "$repo" add src/app.ts
  LEFTHOOK=0 git -C "$repo" commit -q -m 'update frontend file'
  head="$(git -C "$repo" rev-parse HEAD)"
  stdin_payload="refs/heads/topic $head refs/remotes/origin/topic $base"

  output="$(run_installed_hook "$repo" "$stdin_payload")"

  assert_contains "$output" 'frontend-sensitive changes detected' 'installed wrapper should still reach pre-push classification'
  assert_contains "$(<"$hook_log")" 'lefthook run pre-push' 'installed wrapper should invoke lefthook'
}

test_pre_commit_rust_is_file_scoped() {
  local repo output hook_log
  repo="$(create_repo rust-file-scoped)"
  hook_log="$repo/hook.log"

  mkdir -p "$repo/src"
  cat > "$repo/src/lib.rs" <<'EOF'
pub fn staged() {
    println!("staged");
}
EOF
  cat > "$repo/src/unrelated.rs" <<'EOF'
pub fn unrelated() {
    println!("unrelated");
}
EOF
  commit_all "$repo" 'add rust files'

  cat > "$repo/src/lib.rs" <<'EOF'
pub fn staged() {
    println!("staged changed");
}
EOF
  cat > "$repo/src/unrelated.rs" <<'EOF'
pub fn unrelated() { println!("unstaged bad formatting"); }
EOF
  git -C "$repo" add src/lib.rs

  : > "$hook_log"
  output="$(
    cd "$repo"
    export PATH="$repo/bin:$PATH"
    export AVA_TEST_HOOK_LOG="$hook_log"
    export AVA_RUSTFMT_FAIL_ON_ARG='src/unrelated.rs'
    bash scripts/dev/git-hooks.sh pre-commit
  )"

  assert_contains "$output" 'rustfmt --check on 1 staged Rust file(s)' 'pre-commit should use rustfmt check for staged Rust files'
  assert_contains "$(<"$hook_log")" 'rustfmt --check --config-path' 'pre-commit should invoke rustfmt directly'
  assert_not_contains "$(<"$hook_log")" 'src/unrelated.rs' 'pre-commit should not pass unrelated unstaged Rust files to rustfmt'
  assert_not_contains "$(<"$hook_log")" 'cargo fmt' 'pre-commit should not use cargo fmt workspace checks'
}

test_pre_commit_keeps_partial_rust_stage_intact() {
  local repo output before_cached after_cached before_worktree after_worktree hook_log
  repo="$(create_repo pre-commit-rust-partial)"
  hook_log="$repo/hook.log"

  cat > "$repo/demo.rs" <<'EOF'
fn main() {
    println!("base");
}
EOF
  commit_all "$repo" 'add rust file'

  cat > "$repo/demo.rs" <<'EOF'
fn main() {
    println!("stage_me");
}

fn helper() {
    println!("keep_unstaged");
}
EOF

  cat > "$repo/partial.patch" <<'EOF'
diff --git a/demo.rs b/demo.rs
index 1950c93..ab31557 100644
--- a/demo.rs
+++ b/demo.rs
@@ -1,3 +1,3 @@
 fn main() {
-    println!("base");
+    println!("stage_me");
 }
EOF

  git -C "$repo" apply --cached partial.patch

  before_cached="$(git -C "$repo" diff --cached -- demo.rs)"
  before_worktree="$(git -C "$repo" diff -- demo.rs)"
  : > "$hook_log"
  output="$(
    cd "$repo"
    export PATH="$repo/bin:$PATH"
    export AVA_TEST_HOOK_LOG="$hook_log"
    export AVA_RUSTFMT_FAIL_IF_CONTAINS='keep_unstaged'
    bash scripts/dev/git-hooks.sh pre-commit
  )"
  after_cached="$(git -C "$repo" diff --cached -- demo.rs)"
  after_worktree="$(git -C "$repo" diff -- demo.rs)"

  assert_contains "$output" 'rustfmt --check on 1 staged Rust file(s)' 'rust pre-commit should use check mode'
  assert_contains "$(<"$hook_log")" 'ava-hook-staged' 'rust pre-commit should validate a staged snapshot path'
  assert_not_contains "$(<"$hook_log")" "$repo/.tmp/ava-hook-staged" 'rust pre-commit snapshot path should stay outside ignored repo .tmp paths'
  assert_equal "$after_cached" "$before_cached" 'rust pre-commit should not restage whole file'
  assert_equal "$after_worktree" "$before_worktree" 'rust pre-commit should not mutate worktree file'
}

test_pre_commit_keeps_partial_frontend_stage_intact() {
  local repo output before_cached after_cached before_worktree after_worktree hook_log
  repo="$(create_repo pre-commit-frontend-partial)"
  hook_log="$repo/hook.log"

  mkdir -p "$repo/src"
  cat > "$repo/src/demo.ts" <<'EOF'
export const value = "base"
EOF
  commit_all "$repo" 'add frontend file'

  cat > "$repo/src/demo.ts" <<'EOF'
export const value = "stage-me"

export const helper = "keep-unstaged"
EOF

  cat > "$repo/partial.patch" <<'EOF'
diff --git a/src/demo.ts b/src/demo.ts
index 9ca7fe3..888793f 100644
--- a/src/demo.ts
+++ b/src/demo.ts
@@ -1 +1 @@
-export const value = "base"
+export const value = "stage-me"
EOF

  git -C "$repo" apply --cached partial.patch

  before_cached="$(git -C "$repo" diff --cached -- src/demo.ts)"
  before_worktree="$(git -C "$repo" diff -- src/demo.ts)"
  : > "$hook_log"
  output="$(
    cd "$repo"
    export PATH="$repo/bin:$PATH"
    export AVA_TEST_HOOK_LOG="$hook_log"
    export AVA_PNPM_FAIL_IF_CONTAINS='keep-unstaged'
    bash scripts/dev/git-hooks.sh pre-commit
  )"
  after_cached="$(git -C "$repo" diff --cached -- src/demo.ts)"
  after_worktree="$(git -C "$repo" diff -- src/demo.ts)"

  assert_contains "$output" 'biome check on 1 staged frontend file(s)' 'frontend pre-commit should use non-mutating biome check'
  assert_not_contains "$output" '--write' 'frontend pre-commit should not use write mode'
  assert_contains "$(<"$hook_log")" 'ava-hook-staged' 'frontend pre-commit should validate a staged snapshot path'
  assert_not_contains "$(<"$hook_log")" "$repo/.tmp/ava-hook-staged" 'frontend pre-commit snapshot path should stay outside ignored repo .tmp paths'
  assert_equal "$after_cached" "$before_cached" 'frontend pre-commit should not restage whole file'
  assert_equal "$after_worktree" "$before_worktree" 'frontend pre-commit should not mutate worktree file'
}

printf '[git-hooks-regression] running tests\n'
test_delete_only_docs_uses_docs_policy_from_stdin
test_dotgithub_contributing_changes_treated_as_docs_from_stdin
test_dotgithub_pull_request_template_changes_treated_as_docs_from_stdin
test_delete_only_frontend_runs_frontend_gate_from_stdin
test_delete_only_rust_runs_rust_gate_from_stdin
test_pre_push_falls_back_to_inferred_range_without_stdin
test_runtime_markdown_inside_crate_triggers_rust_gate
test_plugin_typescript_triggers_frontend_gate
test_workspace_manifest_triggers_workspace_compile_smoke
test_tauri_changes_trigger_tauri_compile_smoke
test_ava_web_changes_trigger_web_compile_smoke
test_ava_config_changes_trigger_config_compile_smoke
test_installed_pre_push_wrapper_invokes_lefthook_and_forwards_stdin
test_pre_commit_rust_is_file_scoped
test_pre_commit_keeps_partial_rust_stage_intact
test_pre_commit_keeps_partial_frontend_stage_intact
printf '[git-hooks-regression] all tests passed\n'
