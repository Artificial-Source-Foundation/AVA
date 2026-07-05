# Backend Pending Goals Goal

## Objective

Close every remaining backend/non-frontend Pi MVP parity goal in `docs/goals/pi-mvp-parity/`, one area at a time, without doing TUI/editor/terminal frontend implementation work. Treat this file as the durable `/goal` objective and use each referenced area file as the progress log for that area.

Pi remains the primary product baseline. OpenCode remains a secondary reference only where an area file explicitly names it. Preserve AVA's C++23 safety boundaries, especially permissions, session integrity, process execution, provider auth, plugin/MCP isolation, LSP launch rules, and local-first behavior.

## Read First

Before making changes, read these files in order:

1. `docs/goals/pi-mvp-parity/index.md`
2. `docs/goals/pi-mvp-parity/codex-goal-workflow.md`
3. `docs/product/mvp-baseline.md`
4. `docs/product/mvp-coverage-ledger.md`
5. `goals/ava-mvp-baseline-pi-tui/plan.md`
6. `goals/ava-mvp-baseline-pi-tui/mvp-work-ledger.md`
7. The current backend area file from the ordered list below.

Use `docs/reference-code/pi/` and `docs/reference-code/opencode/` only for product-behavior comparison. Do not copy reference source or architecture into AVA, and do not include reference repos in AVA builds, formatting, broad searches, or tests unless the current area file explicitly asks for reference analysis.

## Backend Area Order

Run the backend areas in this order. Finish or explicitly close one before starting the next.

| Order | Area file | Backend scope |
| --- | --- | --- |
| 1 | `docs/goals/pi-mvp-parity/context-extensions-mcp-lsp.md` | Context files, prompts, skills, plugins, MCP, LSP, project-resource trust, command/RPC/docs evidence. |
| 2 | `docs/goals/pi-mvp-parity/cli-commands-sessions-share-import.md` | CLI flags, slash-command backends, sessions, import/export/share decisions, copy/logout dispositions, RPC/headless docs. |
| 3 | `docs/goals/pi-mvp-parity/settings-packages-resources.md` | Config architecture, safe writes, reload diagnostics, project settings trust, package/resource deferrals, startup/offline/telemetry decisions. |

Already closed backend areas (`testing-release-quality.md`, `providers-models-auth.md`, and `agent-tools-permissions.md`) should not be reopened unless a current backend area exposes a real regression or stale product evidence. If that happens, make the smallest correction, record it in both the current area file and the affected completed area file, then continue.

Explicitly exclude `docs/goals/pi-mvp-parity/tui-editor-terminal.md` from this goal. Do not implement frontend/TUI/editor/terminal polish here.

## Frontend Exclusions

This goal must not take on frontend work. Excluded unless needed only to fix a compile break caused by backend changes:

- `src/ava/tui/` product UX changes.
- TUI editor behavior, palettes, markdown rendering, terminal images, resize behavior, mouse behavior, color/theme visuals, or virtual-terminal-style coverage.
- New tmux/PTY visual smoke requirements.
- Pi TUI component-library parity.
- Broader accessibility/screen-reader review.

If a backend area has a criterion that depends on terminal-visible UX, record a deferred frontend follow-up in that area file and `docs/product/mvp-baseline.md` instead of implementing it here. Backend/headless/RPC evidence is enough for this goal.

## Per-Area Process

For each backend area file:

1. Inspect the area file's `100 Percent Criteria`, progress log, validation evidence, decisions, deferrals, and residual risks.
2. Decide whether the area is already closed with evidence. If yes, add a short audit note only if the area file lacks a clear closure statement, then move to the next area.
3. If not closed, inspect the listed Pi/OpenCode/AVA references for that area.
4. Implement the smallest AVA-native C++23/backend/doc/test changes required to close criteria or convert vague partials into explicit `Implemented`, `AVA-superior`, `Deferred`, or `Excluded` dispositions.
5. Keep changes behind the proper subsystem boundary. Do not centralize unrelated behavior into a god file, duplicate policy logic, bypass permission checks, or add backward-compatibility shims without concrete persisted/user-facing need.
6. Use subagents only for non-overlapping research, implementation, or review inside the current area. Do not run multiple area goals in parallel.
7. Update the current area file as the durable progress log with completed work, changed files, decisions, deferrals/exclusions, validation commands, manual/headless evidence, review findings, residual risks, and pending questions.
8. Update `docs/product/mvp-baseline.md` and `docs/product/mvp-coverage-ledger.md` for any newly checked row or changed disposition.
9. If an area file becomes too large, create `docs/goals/pi-mvp-parity/<area>.phase-XX.md`, link it from the original area file, and continue logging there.
10. Do not advance to the next area until the current one is closed or every remaining item is explicitly deferred/excluded with rationale.

## Backend Validation

Start with targeted validation for the current area, then run the full backend-safe release path before declaring the aggregate goal complete.

Recommended targeted checks by area:

```sh
ctest --test-dir build -R 'ava_tests\.(config_context_auth_oauth|plugin|mcp|lsp|agent_tool_dispatcher|app_runtime|app_command_registry)$|ava_cli\.headless_rpc_(plugin_commands|sample_plugin|mcp_commands|mcp_config_errors|command_registry|context_export)' --output-on-failure
ctest --test-dir build -R 'ava_tests\.(session|app_runtime|app_rpc|app_command_registry)$|ava_cli\.headless_(print|rpc|context|tool|permission|mode|session|print_session_startup_options)' --output-on-failure
ctest --test-dir build -R 'ava_tests\.(config_context_auth_oauth|app_runtime|plugin|mcp|lsp|tui_composer)$|ava_cli\.package_manager_deferred' --output-on-failure
```

Full verification before completion:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

Do not require opt-in TUI smokes for this backend-only goal. If a changed backend contract has existing TUI smoke coverage and prerequisites are available, running it is allowed as supplemental evidence, but lack of a frontend smoke must not block backend closure when deterministic CTest/RPC/headless coverage exists.

Live provider smokes remain credential-gated. Run only if relevant credentials exist; otherwise document the missing credentials and continue.

## Review Requirements

Before closing each area, perform or request material review for:

- Correctness and regression risk.
- Permission bypass or trust-boundary weakening.
- Session/archive/config corruption risk.
- Provider/auth credential exposure risk.
- Plugin/MCP/LSP process execution and cancellation safety.
- DX and docs clarity.
- Code quality, subsystem ownership, and god-file risk.
- Test adequacy and evidence consistency.

Record material findings and fixes in the area file. Ignore pure nitpicks unless they indicate a real product, safety, or maintainability risk.

## Stop Rules

Do not stop between areas just because an area finished. Continue to the next backend area automatically.

Stop only if one of these is true:

- A real product/security decision is required before safe progress, such as enabling cloud sharing, remote package install, self-update, telemetry, model-writable project authority, custom provider plugins, persistent plugin/MCP daemons, or weaker permission defaults.
- Validation repeatedly fails and the root cause is not clear after focused debugging.
- A required destructive or externally visible action would happen outside the local workspace.
- Reference dependency installation or credentials are required and were not explicitly approved.

If stopping, write the blocker, exact pending question, current state, and next command into the current area file before pausing.

## Completion Criteria

Mark this backend aggregate goal complete only when:

- The three backend area files in this file's order are either closed with evidence or have every remaining item explicitly deferred/excluded with rationale.
- `docs/product/mvp-baseline.md` has no vague backend partials for these areas.
- `docs/product/mvp-coverage-ledger.md` has evidence for every newly checked backend row.
- Full verification has passed or every skip/blocker is explicitly documented and acceptable for local backend closure.
- `docs/reference-code/` was not modified except for already-approved reference checkouts.
