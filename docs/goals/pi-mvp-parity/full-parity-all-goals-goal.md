# Full Pi MVP Parity All-Goals Goal

## Objective

Bring the entire `docs/goals/pi-mvp-parity/` package to verified closure: audit already completed goals, finish any remaining backend/non-frontend goals first, then finish the frontend/TUI/editor/terminal goal last with real visual evidence from tmux/PTY captures and test logs.

This is an execution goal, not a planning-only goal. Work one area at a time, keep durable notes in the area files, run the relevant tests, inspect logs/captures, and do not mark complete until AVA is mechanically and visually verified for the MVP parity scope.

## Read First

Read these files before changing code or docs:

1. `docs/goals/pi-mvp-parity/index.md`
2. `docs/goals/pi-mvp-parity/codex-goal-workflow.md`
3. `docs/product/mvp-baseline.md`
4. `docs/product/mvp-coverage-ledger.md`
5. `goals/ava-mvp-baseline-pi-tui/plan.md`
6. `goals/ava-mvp-baseline-pi-tui/mvp-work-ledger.md`
7. All area files listed in the execution order below.

Use reference repos only for behavior comparison:

- Pi: `docs/reference-code/pi/`
- OpenCode: `docs/reference-code/opencode/`

Do not copy reference source or architecture into AVA. Do not include reference repos in AVA builds, formatting, broad searches, or tests unless explicitly doing reference analysis for a current area.

## Execution Order

Run the areas in this exact order. Do not work on multiple area goals at once.

| Order | Area file | Action |
| --- | --- | --- |
| 0 | `docs/goals/pi-mvp-parity/testing-release-quality.md` | Audit closure and evidence; fix stale evidence only if found. |
| 1 | `docs/goals/pi-mvp-parity/providers-models-auth.md` | Audit closure and credential-gated live-smoke disposition; fix stale evidence only if found. |
| 2 | `docs/goals/pi-mvp-parity/agent-tools-permissions.md` | Audit closure, OpenCode blocker notes, and permission/manual evidence; fix stale evidence only if found. |
| 3 | `docs/goals/pi-mvp-parity/backend-pending-goals-goal.md` | Execute the backend aggregate goal: context/extensions/MCP/LSP, CLI/sessions/import/export, settings/packages/resources. |
| 4 | `docs/goals/pi-mvp-parity/tui-editor-terminal.md` | Execute or re-audit the frontend/TUI/editor/terminal goal after backend is closed. Must include visual tmux/PTY evidence. |
| 5 | `docs/goals/pi-mvp-parity/index.md` plus product ledgers | Final consistency pass across every goal, baseline row, coverage row, validation log, and deferred/excluded item. |

If an earlier area is already closed with current evidence, do not reopen it for broad work. Add a short audit note only if the area lacks an explicit closure/audit note or if another area revealed stale evidence.

## Backend Phase Requirements

Before frontend/TUI work starts, all backend/non-frontend area criteria must be implemented, AVA-superior, deferred, or excluded with rationale.

Backend scope includes:

- Providers, models, auth, reasoning, image/model metadata, and live-smoke gating.
- Agent loop, tools, permissions, OpenCode-aligned permission behavior, tool cards as backend contracts, side-effect safety, and rule management.
- Context files, prompts, skills, plugins, MCP, LSP, project-resource trust, and command/RPC exposure.
- CLI flags, slash-command backends, sessions, import/export/share decisions, copy/logout dispositions, RPC/headless docs.
- Settings/config architecture, safe writes, reload diagnostics, package/resource deferrals, startup/offline/telemetry decisions.
- Release evidence, coverage ledgers, docs consistency, and safety checklists.

Use `docs/goals/pi-mvp-parity/backend-pending-goals-goal.md` as the detailed backend execution contract for remaining backend work.

Backend validation must include targeted CTest/RPC/headless checks for touched subsystems, followed by the full verification commands in this file. If a backend contract has terminal-visible output, deterministic renderer/RPC/headless evidence is acceptable during the backend phase; defer visual polish to the frontend phase.

## Frontend/TUI Phase Requirements

Start the frontend/TUI phase only after backend areas are closed.

Frontend/TUI scope includes:

- `src/ava/tui/` runtime, composer, editor, palettes, selectors, transcript, markdown/text rendering, tool cards, permission/question UI, terminal protocol handling, themes, keybindings, and display settings.
- TUI-facing command flows from `src/ava/app/` when they are required for visible terminal behavior.
- Tests under `tests/tui_composer_tests.cpp`, `tests/tui_tmux_smoke.py`, `tests/tui_kitty_image_smoke.py`, and `tests/tui_osc8_smoke.py`.
- Docs in `docs/USAGE.md`, `docs/CONFIG.md`, `docs/TESTING.md`, `docs/product/mvp-baseline.md`, and `docs/product/mvp-coverage-ledger.md` where they describe frontend/TUI behavior.

The frontend/TUI phase must not stop at code tests. It must visually verify AVA through tmux/PTY evidence and inspect the resulting logs/captures.

Required visual evidence:

- Run the deterministic renderer/editor tests.
- Run the opt-in tmux smoke when `tmux` is available.
- Run Kitty image and OSC8 smokes when prerequisites exist; otherwise document exact missing prerequisites.
- Inspect the captured terminal output or smoke logs for visible prompts, editor/composer state, tool cards, permission/question prompts, selector behavior, terminal cleanup, and absence of leaked control sequences.
- Save or reference the log/capture paths in `docs/goals/pi-mvp-parity/tui-editor-terminal.md` or a linked phase file.
- If a smoke passes but does not provide enough visible evidence for a changed flow, add a focused Python/PTY/tmux script or extend the existing smoke with stable visible-text assertions.

Preferred visual commands:

```sh
ctest --test-dir build -R 'ava_tests\.tui_composer$' --output-on-failure
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R '^ava_tui\.tmux_smoke$' --output-on-failure
AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R '^ava_tui\.kitty_image_smoke$' --output-on-failure
AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R '^ava_tui\.osc8_smoke$' --output-on-failure
```

When additional manual visual inspection is needed, run AVA in a non-blocking tmux session, drive stable slash-command or prompt flows, capture the pane, inspect the capture, and kill the session explicitly. Put temporary captures under `/tmp/opencode/ava-visual-evidence/` unless the area file needs a permanent artifact path.

Manual tmux evidence should cover at least these visible states when relevant:

- Startup/ready composer.
- Slash command palette and disabled-command reason.
- Session/model/settings selector or documented equivalent.
- Permission prompt with request id/risk/reason and remembered-rule choices.
- Tool card running/success/failure/denied/canceled states.
- Diff/changed-path/truncation/spill display.
- Markdown output including code/list/table/link behavior if changed.
- Resize/narrow/no-color/plain fallback if changed.
- Clean terminal restoration after quit/cancel.

## Per-Area Logging

Each area file is the source of truth for its own progress. For every area touched, record:

- What was inspected.
- What changed.
- Changed files.
- Implementation decisions.
- Deferrals and exclusions with rationale.
- Tests and commands run.
- Smoke/manual evidence, including log or capture paths when available.
- Review findings and fixes.
- Residual risks and pending questions.

If an area file becomes too large, create `docs/goals/pi-mvp-parity/<area>.phase-XX.md`, link it from the original area file, and continue there.

## Validation Plan

Run targeted validation during each area. Before closing the full all-goals objective, run:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

Also run these full-goal focused checks when prerequisites exist:

```sh
ctest --test-dir build -R 'ava_tests\.(config_context_auth_oauth|provider_openai|provider_anthropic|agent_loop|agent_loop_resilience|agent_tool_dispatcher|tools|permission_rules|plugin|mcp|lsp|session|app_runtime|app_rpc|app_command_registry|tui_composer)$' --output-on-failure
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R '^ava_tui\.tmux_smoke$' --output-on-failure
AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R '^ava_tui\.kitty_image_smoke$' --output-on-failure
AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R '^ava_tui\.osc8_smoke$' --output-on-failure
```

Live provider smoke remains credential-gated:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
```

If live credentials, tmux, Kitty/iTerm2, or OSC8-capable terminal prerequisites are unavailable, document the exact missing prerequisite, the command that should be run next, and the deterministic evidence used instead.

## Review Requirements

Before closing each area and again before final completion, perform material review for:

- Correctness and regressions.
- Permission bypass, trust-boundary weakening, destructive operations, and credential exposure.
- Session/archive/config corruption risk.
- Provider/auth, plugin/MCP/LSP process execution, cancellation, and output-bound safety.
- TUI visual correctness, keyboard accessibility, no-color/plain fallback, and terminal cleanup.
- DX/docs clarity.
- Code quality, subsystem ownership, and god-file risk.
- Test adequacy, smoke stability, and evidence consistency.

Use subagents for reviews when useful, but keep one area active at a time. Findings must be material; do not churn on stylistic nitpicks.

## Stop Rules

Do not stop between areas just because one area completed. Continue automatically until the full all-goals objective is complete.

Stop only when one of these is true:

- A real product/security decision is required before safe progress, such as public/cloud sharing, remote package install, self-update, telemetry, model-writable project authority, custom provider plugins, persistent plugin/MCP daemons, weaker permission defaults, or a broad TUI architecture rewrite.
- Validation repeatedly fails and focused debugging does not identify a safe fix.
- A required destructive or externally visible action would happen outside the local workspace.
- Reference dependency installation, live provider credentials, or terminal prerequisites are required and were not approved.

Before pausing, write the blocker, exact pending question, current state, and next command into the current area file.

## Completion Criteria

Mark the full all-goals objective complete only when:

- Every area file in `docs/goals/pi-mvp-parity/` is closed with evidence or has every remaining item explicitly deferred/excluded with rationale.
- Backend areas are closed before frontend/TUI closure.
- `docs/product/mvp-baseline.md` has no vague partials for the MVP scope.
- `docs/product/mvp-coverage-ledger.md` has evidence for every checked row.
- Frontend/TUI closure includes inspected tmux/PTY visual evidence or exact documented prerequisite blockers.
- Full configure/build/CTest/diff-check verification passed.
- Reference repos were not modified except for already-approved checkouts.
