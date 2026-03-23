#!/usr/bin/env bash
set -euo pipefail

echo "[verify:mvp] Running lint"
pnpm lint

echo "[verify:mvp] Running typecheck"
pnpm typecheck

echo "[verify:mvp] Running test suite"
pnpm test:run

echo "[verify:mvp] All MVP checks passed"
