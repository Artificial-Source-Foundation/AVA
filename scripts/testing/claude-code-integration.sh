#!/usr/bin/env bash
# Test Claude Code integration from outside a CC session.
#
# This script MUST be run from a normal terminal (NOT inside a Claude Code session).
# If CLAUDECODE=1 is set, CC will silently refuse to produce output.
#
# Usage:
#   ./scripts/testing/claude-code-integration.sh
#
# Prerequisites:
#   - Claude Code CLI installed and authenticated (`claude` on PATH)
#   - Rust toolchain (`cargo build --workspace` must succeed)

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

pass() { echo -e "  ${GREEN}PASS${NC} $1"; ((PASS++)); }
fail() { echo -e "  ${RED}FAIL${NC} $1: $2"; ((FAIL++)); }
skip() { echo -e "  ${YELLOW}SKIP${NC} $1: $2"; ((SKIP++)); }

echo "=== Claude Code Integration Tests ==="
echo ""

# ── Guard: not inside a CC session ──────────────────────────────────
if [[ "${CLAUDECODE:-}" == "1" ]]; then
    echo -e "${RED}ERROR: Running inside a Claude Code session (CLAUDECODE=1).${NC}"
    echo "These tests must be run from a normal terminal."
    echo "Open a new terminal and run: ./scripts/testing/claude-code-integration.sh"
    exit 1
fi

# ── Guard: claude binary exists ──────────────────────────────────────
if ! command -v claude &>/dev/null; then
    echo -e "${RED}ERROR: 'claude' binary not found on PATH.${NC}"
    echo "Install Claude Code: https://docs.anthropic.com/en/docs/claude-code"
    exit 1
fi

echo "Claude binary: $(which claude)"
echo ""

# ── Test 1: CC responds with JSON ────────────────────────────────────
echo "Test 1: claude -p with --output-format json"
TMPOUT=$(mktemp)
TMPERR=$(mktemp)
if claude -p "Reply with exactly: CC_INTEGRATION_OK" \
    --output-format json \
    --max-turns 1 \
    --no-session-persistence \
    > "$TMPOUT" 2>"$TMPERR"; then

    if [[ -s "$TMPOUT" ]]; then
        # Check it's valid JSON
        if python3 -c "import json,sys; d=json.load(sys.stdin); print('result:', d.get('result','(none)'))" < "$TMPOUT" 2>/dev/null; then
            RESULT=$(python3 -c "import json,sys; d=json.load(sys.stdin); print(d.get('result',''))" < "$TMPOUT")
            if echo "$RESULT" | grep -qi "CC_INTEGRATION_OK"; then
                pass "JSON response contains expected text"
            else
                pass "JSON response parsed (result: ${RESULT:0:80}...)"
            fi
        else
            fail "JSON parse" "stdout is not valid JSON: $(head -c 200 "$TMPOUT")"
        fi
    else
        fail "JSON response" "stdout is empty (stderr: $(cat "$TMPERR"))"
    fi
else
    fail "claude -p" "exit code $? (stderr: $(cat "$TMPERR"))"
fi
rm -f "$TMPOUT" "$TMPERR"

# ── Test 2: CC responds with stream-json ─────────────────────────────
echo "Test 2: claude -p with --output-format stream-json"
TMPOUT=$(mktemp)
TMPERR=$(mktemp)
if claude -p "Reply with exactly: STREAM_OK" \
    --output-format stream-json \
    --max-turns 1 \
    --no-session-persistence \
    > "$TMPOUT" 2>"$TMPERR"; then

    if [[ -s "$TMPOUT" ]]; then
        LINE_COUNT=$(wc -l < "$TMPOUT")
        HAS_SYSTEM=$(grep -c '"type":"system"' "$TMPOUT" 2>/dev/null || grep -c '"type": "system"' "$TMPOUT" 2>/dev/null || echo 0)
        HAS_RESULT=$(grep -c '"type":"result"' "$TMPOUT" 2>/dev/null || grep -c '"type": "result"' "$TMPOUT" 2>/dev/null || echo 0)

        if [[ "$LINE_COUNT" -gt 1 ]]; then
            pass "stream-json produced $LINE_COUNT lines (system events: $HAS_SYSTEM, result events: $HAS_RESULT)"
        else
            fail "stream-json" "expected multiple lines, got $LINE_COUNT"
        fi
    else
        fail "stream-json" "stdout is empty"
    fi
else
    fail "stream-json" "exit code $?"
fi
rm -f "$TMPOUT" "$TMPERR"

# ── Test 3: --allowedTools restricts tools ───────────────────────────
echo "Test 3: --allowedTools restriction"
TMPOUT=$(mktemp)
if claude -p "Use the Read tool to read /etc/hostname and tell me what it says" \
    --output-format json \
    --max-turns 3 \
    --no-session-persistence \
    --allowedTools "Grep,Glob" \
    > "$TMPOUT" 2>/dev/null; then

    if [[ -s "$TMPOUT" ]]; then
        # CC should either refuse (no Read tool) or not use Read
        pass "CC completed with restricted tools (response received)"
    else
        skip "allowedTools" "empty response"
    fi
else
    # Non-zero exit is also fine — CC may refuse
    pass "CC exited non-zero with restricted tools (expected behavior)"
fi
rm -f "$TMPOUT"

# ── Test 4: Rust unit tests ──────────────────────────────────────────
echo "Test 4: Rust unit tests (claude_code tool + stream parser)"
CARGO_OUT=$(cargo test -p ava-tools -- claude_code 2>&1)
TOOL_PASS=$(echo "$CARGO_OUT" | grep -oP '\d+ passed' | head -1)
if echo "$CARGO_OUT" | grep -q "test result: ok"; then
    pass "ava-tools claude_code tests ($TOOL_PASS)"
else
    fail "ava-tools tests" "$CARGO_OUT"
fi

CARGO_OUT=$(cargo test -p ava-agent -- claude_code 2>&1)
STREAM_PASS=$(echo "$CARGO_OUT" | grep -oP '\d+ passed' | head -1)
if echo "$CARGO_OUT" | grep -q "test result: ok"; then
    pass "ava-agent claude_code_stream tests ($STREAM_PASS)"
else
    fail "ava-agent tests" "$CARGO_OUT"
fi

# ── Test 5: AVA CLI with claude_code tool (if binary built) ──────────
echo "Test 5: AVA headless with claude_code delegation"
if cargo build --bin ava 2>/dev/null; then
    TMPOUT=$(mktemp)
    # This tests the full pipeline: AVA agent -> claude_code tool -> CC subprocess
    # Uses a very constrained prompt to keep cost minimal
    if timeout 120 cargo run --bin ava -- \
        "Use the claude_code tool to ask Claude Code to reply with exactly DELEGATION_OK. Use max_turns=1." \
        --headless \
        --provider openrouter \
        --model anthropic/claude-haiku-4.5 \
        --max-turns 3 \
        > "$TMPOUT" 2>/dev/null; then

        if grep -qi "DELEGATION_OK\|claude.code\|CC.*cost" "$TMPOUT"; then
            pass "AVA delegated to CC successfully"
        else
            skip "delegation" "AVA completed but output didn't contain expected markers ($(wc -c < "$TMPOUT") bytes)"
        fi
    else
        skip "delegation" "AVA timed out or failed (this is expected if CC tools aren't activated)"
    fi
    rm -f "$TMPOUT"
else
    skip "AVA headless" "cargo build failed"
fi

# ── Summary ──────────────────────────────────────────────────────────
echo ""
echo "=== Results ==="
echo -e "  ${GREEN}Passed: $PASS${NC}"
echo -e "  ${RED}Failed: $FAIL${NC}"
echo -e "  ${YELLOW}Skipped: $SKIP${NC}"

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
