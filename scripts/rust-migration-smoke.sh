#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AVA_SCRIPT="$ROOT_DIR/cli/src/index.ts"
AVA=(npx tsx "$AVA_SCRIPT")
TMP_DIR="$ROOT_DIR/.tmp/rust-migration-smoke-$$"
FIXTURE_DIR="$TMP_DIR/repo"
TEST_HOME="$TMP_DIR/home"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

assert_contains() {
  local haystack="$1"
  local needle="$2"

  if [[ "$haystack" != *"$needle"* ]]; then
    echo "Assertion failed: expected output to contain: $needle"
    echo "---- output ----"
    echo "$haystack"
    exit 1
  fi
}

run_cli() {
  (cd "$ROOT_DIR" && HOME="$TEST_HOME" "${AVA[@]}" "$@")
}

run_cli_in() {
  local dir="$1"
  shift
  (cd "$dir" && HOME="$TEST_HOME" "${AVA[@]}" "$@")
}

if [[ ! -f "$ROOT_DIR/packages/platform-node/dist/v2.js" ]]; then
  echo "Building workspace packages for CLI smoke tests..."
  (cd "$ROOT_DIR" && pnpm build:packages >/dev/null)
fi

if ! run_cli tool list >/dev/null 2>&1; then
  echo "CLI bootstrap failed. Run 'pnpm install && pnpm build:packages' and retry."
  exit 1
fi

mkdir -p "$FIXTURE_DIR/src" "$FIXTURE_DIR/nested"
mkdir -p "$TEST_HOME"

cat > "$FIXTURE_DIR/src/a.ts" <<'EOF'
export function add(a, b) {
  return a + b;
}
EOF

cat > "$FIXTURE_DIR/src/b.ts" <<'EOF'
export const greeting = "hello world"
EOF

cat > "$FIXTURE_DIR/nested/c.md" <<'EOF'
notes
EOF

echo "[1/7] glob tool correctness"
OUT=$(run_cli tool glob --pattern "**/*.ts" --path "$FIXTURE_DIR")
assert_contains "$OUT" "src/a.ts"
assert_contains "$OUT" "src/b.ts"

echo "[2/7] grep tool correctness"
OUT=$(run_cli tool grep --pattern "hello" --path "$FIXTURE_DIR" --include "*.ts")
assert_contains "$OUT" "src/b.ts"
assert_contains "$OUT" "hello world"

echo "[3/7] write -> read round-trip"
run_cli tool write_file --path "$FIXTURE_DIR/src/generated.txt" --content $'alpha\nbeta\n' >/dev/null
OUT=$(run_cli tool read_file --path "$FIXTURE_DIR/src/generated.txt")
assert_contains "$OUT" "alpha"
assert_contains "$OUT" "beta"

echo "[4/7] edit fuzzy replacement"
run_cli tool edit \
  --filePath "$FIXTURE_DIR/src/a.ts" \
  --oldString $'export function add(a, b) {\nreturn a + b;\n}' \
  --newString $'function add(a, b) {\n  return a - b;\n}' >/dev/null
OUT=$(run_cli tool read_file --path "$FIXTURE_DIR/src/a.ts")
assert_contains "$OUT" "return a - b;"

echo "[5/7] apply_patch correctness"
PATCH=$(cat <<'EOF'
*** Begin Patch
*** Update File: src/b.ts
@@ -1 +1 @@
-export const greeting = "hello world"
+export const greeting = "hello rust"
*** End Patch
EOF
)
run_cli_in "$FIXTURE_DIR" tool apply_patch --patch "$PATCH" >/dev/null
OUT=$(run_cli tool read_file --path "$FIXTURE_DIR/src/b.ts")
assert_contains "$OUT" "hello rust"

echo "[6/7] mock agent regression"
run_cli_in "$FIXTURE_DIR" run "List TypeScript files in this repo and stop." --mock --max-turns 4 >/dev/null

echo "[7/7] optional real-provider sanity"
if [[ -n "${AVA_ANTHROPIC_API_KEY:-}" || -n "${AVA_OPENAI_API_KEY:-}" || -n "${AVA_OPENROUTER_API_KEY:-}" ]]; then
  PROVIDER="openrouter"
  if [[ -n "${AVA_ANTHROPIC_API_KEY:-}" ]]; then
    PROVIDER="anthropic"
  elif [[ -n "${AVA_OPENAI_API_KEY:-}" ]]; then
    PROVIDER="openai"
  fi

  run_cli_in "$FIXTURE_DIR" run "Reply exactly with smoke-pass and stop." --provider "$PROVIDER" --max-turns 2 >/dev/null
else
  echo "No provider API key present, skipping live-provider sanity test"
fi

echo "All smoke checks passed."
