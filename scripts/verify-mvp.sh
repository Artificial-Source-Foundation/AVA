#!/usr/bin/env bash
set -euo pipefail

echo "[verify:mvp] Running lint"
npm run lint

echo "[verify:mvp] Running typecheck"
npx tsc --noEmit

echo "[verify:mvp] Running test suite"
npm run test:run

echo "[verify:mvp] All MVP checks passed"
