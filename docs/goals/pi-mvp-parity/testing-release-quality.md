# Testing, Release Evidence, And Documentation Quality

## Goal Objective

Make Pi MVP parity mechanically verifiable: every claimed feature has tests, smokes, live-smoke criteria, docs, or an explicit deferred/excluded decision, and the release evidence can be rerun by another agent.

Suggested Codex command:

```text
/goal Bring AVA Pi parity testing/release evidence to documented closure using docs/goals/pi-mvp-parity/testing-release-quality.md. First reconcile docs/product/mvp-baseline.md and docs/product/mvp-coverage-ledger.md with existing tests, then fill missing evidence or mark explicit deferrals. Verify with targeted suites, full CTest, and git --no-pager diff --check. Stop if a feature claim cannot be supported by code, tests, docs, or a product decision.
```

## Pi References To Inspect First

| Topic | Pi paths |
| --- | --- |
| Test inventory | `docs/reference-code/pi/packages/ai/test/`, `packages/agent/test/`, `packages/coding-agent/test/`, `packages/tui/test/` |
| Virtual terminal | `docs/reference-code/pi/packages/tui/test/virtual-terminal.ts` |
| Provider tests | `docs/reference-code/pi/packages/ai/test/` |
| Settings/package tests | `docs/reference-code/pi/packages/coding-agent/test/settings-manager.test.ts`, `package-manager.test.ts` |
| Export tests | `docs/reference-code/pi/packages/coding-agent/test/export-html-xss.test.ts`, `export-html-whitespace.test.ts`, `export-html-skill-block.test.ts` |
| CI scripts | `docs/reference-code/pi/package.json`, `docs/reference-code/pi/test.sh`, `docs/reference-code/pi/scripts/` |

## AVA References To Inspect First

| Topic | AVA paths |
| --- | --- |
| CTest config | `tests/CMakeLists.txt` |
| Test docs | `docs/TESTING.md` |
| Product baseline | `docs/product/mvp-baseline.md`, `docs/product/mvp-coverage-ledger.md` |
| C++ tests | `tests/*.cpp` |
| CLI smokes | `tests/cli_headless_*.cmake` |
| TUI smokes | `tests/tui_tmux_smoke.py`, `tests/tui_kitty_image_smoke.py`, `tests/tui_osc8_smoke.py` |
| Golden fixtures | `tests/golden/` |
| CI | `.github/workflows/` |

## Current Gap Summary

AVA has strong integration testing, safety testing, fake providers/servers, CMake smokes, and real PTY smokes. Pi has many more provider/model and TUI component/virtual-terminal tests. The gap is release evidence organization and specific missing matrices: provider live-smoke results, virtual-terminal/screen-model decision, performance thresholds, and final docs consistency.

## 100 Percent Criteria

| Criterion | Required AVA State |
| --- | --- |
| Coverage ledger complete | Every checked row in `docs/product/mvp-baseline.md` maps to evidence in `docs/product/mvp-coverage-ledger.md`. |
| Partial rows classified | Every unchecked row is `Partial`, `Deferred`, or `Excluded`, with rationale and next action. |
| Area evidence matrix | Each area file in this package has verification commands and progress logs. |
| Provider live-smoke matrix | Current providers and any new providers have env var, default model, command, latest result, and skip reason when missing credentials. |
| TUI evidence matrix | Renderer tests, PTY smokes, and any virtual-terminal/screen-model decision are documented. |
| Performance thresholds | Startup, large transcript render, large tool output, search, and replay have deterministic tests or documented thresholds. |
| Security/safety checklist | New side-effect classes have review checklist coverage. |
| Docs consistency | `README.md`, `docs/USAGE.md`, `docs/CONFIG.md`, `docs/TESTING.md`, `docs/headless-protocol.md`, product docs, and this package agree with code. |
| Rerunnable release commands | Full build/test/diff-check commands are documented and pass or skip with clear prerequisites. |

## Implementation Slices

| Slice | Work |
| --- | --- |
| Q1. Evidence audit | Compare `docs/product/mvp-baseline.md` checked rows with `docs/product/mvp-coverage-ledger.md`; fix stale claims and missing evidence. |
| Q2. Provider smoke matrix | Add a table for OpenAI, Anthropic, DeepSeek, Gemini, Kimi, Moonshot, OpenRouter, and any new providers. Include env vars and skipped credentials. |
| Q3. TUI test decision | Document virtual-terminal vs renderer+PTY strategy. Add missing deterministic tests or smoke assertions. |
| Q4. Performance thresholds | Turn existing performance tests into named release thresholds where possible. |
| Q5. Docs consistency pass | Update user-facing docs to match implemented commands/config/features. |
| Q6. Final release pass | Run full build, full CTest, opt-in smokes when available, live smokes when credentials exist, and `diff --check`. |

## Checkpoint Plan

| Checkpoint | Status | Notes |
| --- | --- | --- |
| Q1. Evidence audit | Complete | Reconciled checked testing/release rows against `docs/product/mvp-coverage-ledger.md` and added coverage rows for provider live-smoke, performance thresholds, and package trust/signing policy evidence. |
| Q2. Provider smoke matrix | Complete | `docs/TESTING.md` records env vars, default model overrides, opt-in command, skip behavior, and result classifications for OpenAI, Anthropic API key/OAuth bearer, DeepSeek, Gemini, Kimi, Moonshot, and OpenRouter. |
| Q3. TUI test decision | Complete | AVA keeps deterministic renderer/editor tests plus gated PTY/tmux/Kitty/OSC8 smokes for MVP instead of adding a Pi-style virtual-terminal parser now. |
| Q4. Performance thresholds | Complete | `docs/TESTING.md` names the current large-render, 20,000-line tool-output, very-long transcript, tail-renderer, and headless startup/search/replay thresholds. |
| Q5. Security/safety checklist | Complete | Added `docs/engineering/side-effect-safety-checklist.md` and linked it from release evidence. |
| Q6. Docs consistency pass | Complete | Final whole-goal docs consistency and full verification were repeated after the remaining area files closed. |

## Non-Goals Unless Approved

| Item | Reason |
| --- | --- |
| Making optional live-provider credentials mandatory | Local CI and agents may not have secrets. Use clear skip evidence. |
| Requiring real Kitty/tmux in all environments | Keep PTY tests gated; deterministic unit tests should cover most logic. |
| Claiming 100 percent from docs alone | Feature claims need implementation evidence or explicit deferral/exclusion. |

## Verification

Release baseline:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

Opt-in terminal evidence:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R ava_tui.tmux_smoke --output-on-failure
AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R ava_tui.kitty_image_smoke --output-on-failure
AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R ava_tui.osc8_smoke --output-on-failure
```

Credential-gated provider evidence:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
```

## Progress Log

- 2026-07-03: Inspected Pi test inventory shape under `docs/reference-code/pi/packages/ai/test/`, `packages/coding-agent/test/`, and `packages/tui/test/`, including provider, settings/package, export, key/input, markdown, and virtual-terminal references. Inspected AVA `tests/CMakeLists.txt`, `docs/TESTING.md`, product docs, CLI/TUI smoke registrations, provider live smoke tests, performance smoke, and CI workflow.
- 2026-07-03: Added release-evidence organization to `docs/TESTING.md`: provider live-smoke matrix, AVA virtual-terminal decision, TUI evidence strategy, performance thresholds, and docs consistency rules. Updated `docs/product/mvp-baseline.md` and `docs/product/mvp-coverage-ledger.md` so checked release rows now map to evidence.
- 2026-07-03: Added `docs/engineering/side-effect-safety-checklist.md` covering filesystem, shell, network, provider/auth, plugin, MCP, LSP, session, config, and package/resource side effects. Decision: this is release evidence for future side-effect classes; it does not weaken existing AVA permissions.
- 2026-07-04: Final completion audit reclassified broad provider/TUI/settings follow-up items as implemented MVP rows or explicit deferrals, updated `docs/product/mvp-coverage-ledger.md` evidence for newly checked rows, and closed the docs-consistency checkpoint.
- 2026-07-04: Testing/release closure re-audit found one coverage-ledger omission for the checked package trust/signing/source policy row. Added the missing evidence row; `docs/product/mvp-baseline.md` now has 86 checked rows mapped to 86 product evidence rows, and all 14 unchecked rows carry an explicit deferred disposition.

## Changed Files

- `docs/TESTING.md`
- `docs/engineering/side-effect-safety-checklist.md`
- `docs/product/mvp-baseline.md`
- `docs/product/mvp-coverage-ledger.md`
- `docs/goals/pi-mvp-parity/testing-release-quality.md`
- `tests/CMakeLists.txt`
- `tests/core_tests.cpp`
- `tests/provider_live_smoke_tests.cpp`
- `tests/support/test_harness.cpp`
- `tests/support/test_harness.h`

## Decisions And Deferrals

- AVA does not add a Pi-style virtual-terminal parser for MVP. Renderer/editor CTest coverage plus real PTY/tmux/Kitty/OSC8 smokes are the accepted evidence path; revisit only if a protocol needs stable screen-model assertions that current smokes cannot provide.
- Live provider credentials remain optional. Release notes classify each live case as passed, skipped/no credential, credential/auth-blocked, provider/rate-limited, network-blocked, or AVA regression.
- Final whole-repo docs consistency was completed after all later area files updated user-facing behavior and deferrals. Final docs audit found stale README `/reload` wording and missing headless-protocol Pi session aliases/import note; both were fixed.

## Validation Log

- `cmake --preset dev` — passed.
- `cmake --build --preset dev --target ava_tests` — passed.
- `ctest --test-dir build -R 'ava_tests\.(provider_live_smoke|tui_composer)$|ava_cli\.headless_performance_smoke' --output-on-failure` — passed; `ava_tests.provider_live_smoke` now reports CTest skipped when `AVA_LIVE_PROVIDER_SMOKE` is not set, while TUI/performance tests passed.
- `ctest --test-dir build -R 'ava_tui\.(tmux_smoke|kitty_image_smoke|osc8_smoke)' --output-on-failure` — passed with all three opt-in terminal smokes skipped by their gates.
- `git --no-pager diff --check` — passed.
- Final full-goal verification after all area files: `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev --output-on-failure`, and `git --no-pager diff --check` passed; default CTest skipped only credential/PTY-gated opt-in smokes.
- 2026-07-04 completion audit verification: `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev --output-on-failure`, `AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R ava_tui.tmux_smoke --output-on-failure`, `AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R ava_tui.kitty_image_smoke --output-on-failure`, `AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R ava_tui.osc8_smoke --output-on-failure`, `AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure` with no provider credentials set, and `git --no-pager diff --check` passed or skipped as expected.
- 2026-07-04 testing/release closure re-audit verification: reconciliation helper reported 86 checked baseline rows, 86 product evidence rows, zero unchecked rows without explicit disposition, and zero low-confidence checked-row ledger matches. `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev --output-on-failure`, focused `ctest --test-dir build -R 'ava_tests\.(provider_live_smoke|tui_composer)$|ava_cli\.headless_performance_smoke' --output-on-failure`, `AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R ava_tui.tmux_smoke --output-on-failure`, `AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R ava_tui.kitty_image_smoke --output-on-failure`, `AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R ava_tui.osc8_smoke --output-on-failure`, and `git --no-pager diff --check` passed. `AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure` skipped as expected because no provider credential environment variables were set.
- 2026-07-04 full all-goals final verification: baseline/coverage helper reported 86 checked rows, 86 coverage rows, 14 unchecked rows, and zero unchecked rows without explicit disposition. `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev --output-on-failure`, the full-goal focused CTest regex, opt-in `AVA_TUI_TMUX_SMOKE=1`, `AVA_TUI_KITTY_IMAGE_SMOKE=1`, and `AVA_TUI_OSC8_SMOKE=1` smokes, `AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure`, and `git --no-pager diff --check` passed or skipped with documented prerequisites. The tmux smoke regenerated inspected captures under `build/tui-tmux-smoke/evidence/`; the live-provider smoke skipped because no supported credential env vars were present.

## Review Findings

- Material review found that `provider_live_smoke` previously returned success instead of CTest skip when credentials/gate were absent. Fixed by adding a test-harness skip signal, returning exit code 77 for skipped single-suite runs, and setting `SKIP_RETURN_CODE 77` for `ava_tests.provider_live_smoke`.
- Material review found that `docs/TESTING.md` misdescribed the large-render threshold as 120 messages. Fixed wording to the actual 900-item transcript at 120-column width across four redraw passes.
- Final docs audit found stale README `/reload` wording and missing `docs/headless-protocol.md` Pi session aliases plus `/import` note. Fixed both and updated the coverage ledger/baseline to record final documentation consistency.
- Testing/release closure review found that the checked package trust/signing/source policy row had supporting docs and disabled-package tests, but no product evidence row in `docs/product/mvp-coverage-ledger.md`. Added the explicit ledger row so the baseline-to-ledger count and review trace are mechanical.

## Residual Risks

- Live provider results cannot be proven without credentials; the matrix records skip/classification rules instead of inventing success.
- The virtual-terminal decision trades a separate screen parser for renderer tests plus PTY evidence; if future terminal behavior becomes flaky, revisit a normalized screen-model parser.

## Pending Questions

- None.
