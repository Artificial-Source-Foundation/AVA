#!/usr/bin/env bash
# Release build verification and performance benchmarks for AVA.
#
# Usage: ./scripts/benchmarks/release-benchmark.sh
#
# Measures:
#   1. Release binary size (target < 25 MB)
#   2. --help latency (target < 200ms median over 10 runs)
#   3. Clean environment error handling (no panics)
#   4. Cargo test benchmarks from bench_performance.rs

set -euo pipefail

BINARY="target/release/ava"
PASS=0
FAIL=0
RESULTS=()

pass() {
    PASS=$((PASS + 1))
    RESULTS+=("PASS  $1")
    echo "  ✓ $1"
}

fail() {
    FAIL=$((FAIL + 1))
    RESULTS+=("FAIL  $1")
    echo "  ✗ $1"
}

echo "═══════════════════════════════════════════════════"
echo "  AVA Release Benchmark"
echo "═══════════════════════════════════════════════════"
echo ""

# ─── Step 1: Release build ───────────────────────────────────────────
echo "▸ Building release binary..."
cargo build --release --bin ava 2>&1 | tail -1
echo ""

# ─── Step 2: Binary size ─────────────────────────────────────────────
echo "▸ Checking binary size..."
if [ ! -f "$BINARY" ]; then
    fail "Binary not found at $BINARY"
else
    SIZE_BYTES=$(stat --format=%s "$BINARY" 2>/dev/null || stat -f%z "$BINARY" 2>/dev/null)
    SIZE_MB=$(echo "scale=1; $SIZE_BYTES / 1048576" | bc)
    SIZE_HUMAN=$(ls -lh "$BINARY" | awk '{print $5}')

    if (( $(echo "$SIZE_MB < 25" | bc -l) )); then
        pass "Binary size: ${SIZE_HUMAN} (${SIZE_MB} MB < 25 MB)"
    else
        fail "Binary size: ${SIZE_HUMAN} (${SIZE_MB} MB >= 25 MB target)"
    fi
fi

# ─── Step 3: --help latency ──────────────────────────────────────────
echo "▸ Measuring --help latency (10 runs)..."
TIMES=()
for i in $(seq 1 10); do
    START=$(date +%s%N)
    "$BINARY" --help > /dev/null 2>&1 || true
    END=$(date +%s%N)
    MS=$(( (END - START) / 1000000 ))
    TIMES+=("$MS")
done

# Sort and take median
IFS=$'\n' SORTED=($(sort -n <<<"${TIMES[*]}")); unset IFS
MEDIAN=${SORTED[4]}  # 0-indexed 5th element = median of 10
MIN=${SORTED[0]}
MAX=${SORTED[9]}

if [ "$MEDIAN" -lt 200 ]; then
    pass "--help latency: median=${MEDIAN}ms min=${MIN}ms max=${MAX}ms (< 200ms)"
else
    fail "--help latency: median=${MEDIAN}ms min=${MIN}ms max=${MAX}ms (>= 200ms target)"
fi

# ─── Step 4: --help output correctness ───────────────────────────────
echo "▸ Verifying --help output..."
HELP_OUTPUT=$("$BINARY" --help 2>&1 || true)
if echo "$HELP_OUTPUT" | grep -qi "ava"; then
    pass "--help output contains 'AVA'"
else
    fail "--help output missing 'AVA' identifier"
fi

# ─── Step 5: Clean environment (no panic) ────────────────────────────
echo "▸ Testing clean environment (no provider configured)..."
CLEAN_DIR=$(mktemp -d)
trap "rm -rf $CLEAN_DIR" EXIT

OUTPUT=$("$BINARY" "test" --headless 2>&1 || true)
# The key check: it should NOT panic
if echo "$OUTPUT" | grep -q "panic"; then
    fail "Clean environment: binary panicked"
else
    pass "Clean environment: no panic (graceful error handling)"
fi
rm -rf "$CLEAN_DIR"

# ─── Step 6: Rust benchmark tests ────────────────────────────────────
echo "▸ Running Rust performance benchmarks..."
echo ""
if cargo test -p ava-tui --test bench_performance -- --nocapture 2>&1; then
    pass "Rust benchmark tests passed"
else
    fail "Rust benchmark tests failed"
fi

# ─── Summary ─────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════"
echo "  Summary: ${PASS} passed, ${FAIL} failed"
echo "═══════════════════════════════════════════════════"
for r in "${RESULTS[@]}"; do
    echo "  $r"
done
echo ""

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
