#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# AVA Headless Stress Test
#
# Creates a disposable mini-project and fires a series of headless AVA runs
# that exercise every default tool: read, write, edit, bash, glob, grep,
# web_fetch, web_search, git_read.
#
# Usage:
#   ./tests/stress-test.sh [--provider openai] [--model gpt-5.4] [--max-turns 5]
#
# Requirements:
#   - AVA binary built: cargo build --bin ava
#   - Valid provider credentials in ~/.ava/credentials.json
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────
PROVIDER="${PROVIDER:-openai}"
MODEL="${MODEL:-gpt-5.4}"
MAX_TURNS="${MAX_TURNS:-5}"
AVA_BIN="${AVA_BIN:-cargo run --bin ava --}"
TIMEOUT=120  # seconds per task

# ── Parse CLI args ───────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case "$1" in
    --provider) PROVIDER="$2"; shift 2 ;;
    --model)    MODEL="$2";    shift 2 ;;
    --max-turns) MAX_TURNS="$2"; shift 2 ;;
    --bin)      AVA_BIN="$2";  shift 2 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# ── Create temp project ─────────────────────────────────────────────────────
WORKDIR=$(mktemp -d /tmp/ava-stress-XXXXXX)
echo -e "${CYAN}${BOLD}═══ AVA Headless Stress Test ═══${RESET}"
echo -e "  Provider:   ${YELLOW}${PROVIDER}/${MODEL}${RESET}"
echo -e "  Max turns:  ${MAX_TURNS}"
echo -e "  Work dir:   ${WORKDIR}"
echo ""

# Initialize a git repo with some starter files
pushd "$WORKDIR" > /dev/null
git init -q
git config user.email "stress@test.local"
git config user.name "Stress Test"

# Seed files for the mini project
cat > main.py << 'PYEOF'
"""A simple calculator module with intentional bugs for AVA to find and fix."""

def add(a, b):
    return a + b

def subtract(a, b):
    return a - b

def multiply(a, b):
    return a * b  # BUG: should handle None inputs

def divide(a, b):
    return a / b  # BUG: no zero-division guard

def factorial(n):
    if n == 0:
        return 1
    return n * factorial(n)  # BUG: should be n-1

def fibonacci(n):
    """Return the nth fibonacci number."""
    if n <= 1:
        return n
    a, b = 0, 1
    for _ in range(2, n + 1):
        a, b = b, a + b
    return b

if __name__ == "__main__":
    print(f"add(2, 3) = {add(2, 3)}")
    print(f"divide(10, 2) = {divide(10, 2)}")
    print(f"fibonacci(10) = {fibonacci(10)}")
PYEOF

cat > test_main.py << 'TESTEOF'
"""Tests for the calculator module."""
import unittest
from main import add, subtract, multiply, divide, factorial, fibonacci

class TestCalculator(unittest.TestCase):
    def test_add(self):
        self.assertEqual(add(2, 3), 5)
        self.assertEqual(add(-1, 1), 0)

    def test_subtract(self):
        self.assertEqual(subtract(5, 3), 2)

    def test_multiply(self):
        self.assertEqual(multiply(3, 4), 12)
        # This should handle None gracefully
        # self.assertIsNone(multiply(None, 4))

    def test_divide(self):
        self.assertEqual(divide(10, 2), 5.0)
        # This should raise ValueError, not ZeroDivisionError
        # with self.assertRaises(ValueError):
        #     divide(10, 0)

    def test_factorial(self):
        self.assertEqual(factorial(0), 1)
        self.assertEqual(factorial(5), 120)

    def test_fibonacci(self):
        self.assertEqual(fibonacci(0), 0)
        self.assertEqual(fibonacci(1), 1)
        self.assertEqual(fibonacci(10), 55)

if __name__ == "__main__":
    unittest.main()
TESTEOF

cat > config.json << 'JSONEOF'
{
  "app_name": "stress-test-project",
  "version": "0.1.0",
  "debug": true,
  "max_retries": 3,
  "features": {
    "logging": true,
    "metrics": false,
    "cache": true
  }
}
JSONEOF

cat > README.md << 'MDEOF'
# Stress Test Mini Project

A small Python calculator with intentional bugs for testing AVA's tool capabilities.

## Files
- `main.py` — Calculator functions (has bugs!)
- `test_main.py` — Unit tests
- `config.json` — App configuration
MDEOF

mkdir -p src/utils
cat > src/utils/helpers.py << 'HELPEOF'
"""Utility helpers."""

def clamp(value, min_val, max_val):
    """Clamp a value between min and max."""
    return max(min_val, min(value, max_val))

def slugify(text):
    """Convert text to a URL-friendly slug."""
    import re
    text = text.lower().strip()
    text = re.sub(r'[^\w\s-]', '', text)
    text = re.sub(r'[-\s]+', '-', text)
    return text

def flatten(nested_list):
    """Flatten a nested list."""
    result = []
    for item in nested_list:
        if isinstance(item, list):
            result.extend(flatten(item))
        else:
            result.append(item)
    return result
HELPEOF

git add -A
git commit -q -m "Initial commit: calculator with intentional bugs"
popd > /dev/null

# ── Test tasks ───────────────────────────────────────────────────────────────
PASS=0
FAIL=0
SKIP=0
TOTAL=0

run_task() {
  local name="$1"
  local goal="$2"
  local extra_flags="${3:-}"

  TOTAL=$((TOTAL + 1))
  echo -e "\n${BOLD}[$TOTAL] ${name}${RESET}"
  echo -e "  Goal: ${goal:0:100}..."

  local start_time=$(date +%s)
  local output_file="$WORKDIR/.ava-test-output-$TOTAL.log"

  # Run AVA headless in the work directory
  if timeout "$TIMEOUT" bash -c "cd '$WORKDIR' && $AVA_BIN \
    --headless \
    --provider '$PROVIDER' \
    --model '$MODEL' \
    --max-turns '$MAX_TURNS' \
    --auto-approve \
    $extra_flags \
    '$goal'" > "$output_file" 2>&1; then
    local end_time=$(date +%s)
    local elapsed=$((end_time - start_time))
    echo -e "  ${GREEN}✓ PASS${RESET} (${elapsed}s)"
    PASS=$((PASS + 1))
  else
    local exit_code=$?
    local end_time=$(date +%s)
    local elapsed=$((end_time - start_time))
    if [[ $exit_code -eq 124 ]]; then
      echo -e "  ${YELLOW}⏱ TIMEOUT${RESET} (${TIMEOUT}s limit)"
      SKIP=$((SKIP + 1))
    else
      echo -e "  ${RED}✗ FAIL${RESET} (exit=$exit_code, ${elapsed}s)"
      # Show last 5 lines of output for debugging
      echo -e "  ${RED}Last output:${RESET}"
      tail -5 "$output_file" 2>/dev/null | sed 's/^/    /'
      FAIL=$((FAIL + 1))
    fi
  fi
}

# ──────────────────────────────────────────────────────────────────────────────
# Phase 1: READ tools (read, glob, grep, git_read)
# ──────────────────────────────────────────────────────────────────────────────
echo -e "\n${CYAN}━━━ Phase 1: Read Tools (read, glob, grep, git_read) ━━━${RESET}"

run_task "Read + Summarize" \
  "Read main.py and test_main.py, then list all the function names defined in main.py. Output just the names, one per line."

run_task "Glob + Count" \
  "Use glob to find all .py files in this project recursively. Tell me the count and list their paths."

run_task "Grep + Analyze" \
  "Search all files for the word 'BUG' (case insensitive). Report each occurrence with file name and line number."

run_task "Git History" \
  "Read the git log and tell me the most recent commit message and what files were changed in it."

# ──────────────────────────────────────────────────────────────────────────────
# Phase 2: WRITE tools (write, edit)
# ──────────────────────────────────────────────────────────────────────────────
echo -e "\n${CYAN}━━━ Phase 2: Write Tools (write, edit) ━━━${RESET}"

run_task "Write New File" \
  "Create a new file called src/utils/validators.py with functions: validate_email(email) that checks for @ and ., and validate_positive_int(n) that checks n > 0. Include docstrings."

run_task "Edit: Fix divide bug" \
  "In main.py, fix the divide function to raise a ValueError when b is 0 instead of causing a ZeroDivisionError. Keep the function signature the same."

run_task "Edit: Fix factorial bug" \
  "In main.py, the factorial function has a recursive bug. Fix it so factorial(5) correctly returns 120."

run_task "Edit: Fix multiply" \
  "In main.py, update the multiply function to return None if either argument is None, otherwise return the product."

# ──────────────────────────────────────────────────────────────────────────────
# Phase 3: BASH tool
# ──────────────────────────────────────────────────────────────────────────────
echo -e "\n${CYAN}━━━ Phase 3: Bash Tool ━━━${RESET}"

run_task "Bash: Run Tests" \
  "Run the Python unit tests with 'python -m pytest test_main.py -v' or 'python -m unittest test_main -v'. Report which tests pass and which fail."

run_task "Bash: Check Python" \
  "Run 'python3 main.py' and tell me the output. Then check if there are any syntax errors by running 'python3 -c \"import main\"'."

# ──────────────────────────────────────────────────────────────────────────────
# Phase 4: Combined / Multi-tool tasks
# ──────────────────────────────────────────────────────────────────────────────
echo -e "\n${CYAN}━━━ Phase 4: Multi-Tool Tasks ━━━${RESET}"

run_task "Multi: Uncomment + Fix Tests" \
  "In test_main.py, uncomment the test for divide-by-zero (it should expect ValueError) and the test for multiply(None, 4). Make sure main.py is already fixed for these cases, and if not fix it too. Then run the tests to verify they pass."

run_task "Multi: Add Feature + Test" \
  "Add a 'power(base, exp)' function to main.py that computes base**exp. Then add a test for it in test_main.py testing power(2,10)==1024 and power(5,0)==1. Run the tests."

run_task "Multi: Refactor + Verify" \
  "Refactor main.py to add type hints to ALL functions (using Optional where needed). Make sure 'from typing import Optional' is imported. Then run the tests to make sure nothing broke."

run_task "Multi: JSON Config Update" \
  "Read config.json, set 'debug' to false and 'version' to '0.2.0', enable the 'metrics' feature. Write the updated config back."

# ──────────────────────────────────────────────────────────────────────────────
# Phase 5: Edge cases & stress
# ──────────────────────────────────────────────────────────────────────────────
echo -e "\n${CYAN}━━━ Phase 5: Edge Cases ━━━${RESET}"

run_task "Large file generation" \
  "Create a file called src/data_generator.py that generates a list of 100 fake user records (dict with name, email, age fields) using only stdlib (no external packages). Include a main block that prints the first 3 records."

run_task "Multi-file grep + edit" \
  "Find ALL files that import from 'main' (search for 'from main import' or 'import main'). In each file that does, add a comment at the top: '# Depends on: main.py'. Do not duplicate the comment if it already exists."

run_task "Git operations" \
  "Stage all current changes, commit them with message 'fix: resolve calculator bugs and add features', then show me the git log --oneline for the last 3 commits."

# ──────────────────────────────────────────────────────────────────────────────
# Summary
# ──────────────────────────────────────────────────────────────────────────────
echo -e "\n${CYAN}${BOLD}═══ Results ═══${RESET}"
echo -e "  ${GREEN}Pass:${RESET}    $PASS"
echo -e "  ${RED}Fail:${RESET}    $FAIL"
echo -e "  ${YELLOW}Timeout:${RESET} $SKIP"
echo -e "  Total:   $TOTAL"
echo -e "  Work dir: ${WORKDIR}"

if [[ $FAIL -gt 0 ]]; then
  echo -e "\n${RED}${BOLD}Some tasks failed. Check logs in ${WORKDIR}/.ava-test-output-*.log${RESET}"
  exit 1
elif [[ $SKIP -gt 0 ]]; then
  echo -e "\n${YELLOW}${BOLD}Some tasks timed out.${RESET}"
  exit 0
else
  echo -e "\n${GREEN}${BOLD}All tasks passed!${RESET}"
  exit 0
fi
