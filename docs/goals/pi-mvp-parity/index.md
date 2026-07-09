# Pi MVP Parity Goal Index

## Objective

Make AVA a Pi-style local terminal coding agent implemented in C++23, while preserving AVA-specific advantages: granular permissions and audit, MCP, LSP, web tools, the `question` tool, `apply_patch`, safer process boundaries, and real terminal smoke coverage.

This package is the planning surface for pushing the Pi parity score toward 100 percent. It is intentionally split into area files so Codex can run one focused `/goal` at a time instead of trying to finish the entire product backlog in one run.

## Current Source Of Truth

Read these before starting any area goal:

1. `docs/product/mvp-baseline.md`
2. `docs/product/mvp-coverage-ledger.md`
3. `goals/ava-mvp-baseline-pi-tui/plan.md`
4. `goals/ava-mvp-baseline-pi-tui/mvp-work-ledger.md`
5. This file, then the specific area file for the goal.

If the area file conflicts with `docs/product/mvp-baseline.md`, update the area file or ask for a product decision before implementation.

## Codex Goal Rules

Use `codex-goal-workflow.md` for the goal command contract. The short version:

- Use `/goal <objective>` only for one durable area objective with clear evidence.
- Use `/goal` to inspect the active goal.
- Use `/goal pause`, `/goal resume`, or `/goal clear` for lifecycle control.
- A goal must include one outcome, one stop condition, paths to inspect first, and verification evidence.
- Do not mark a goal complete until the area file's 100 percent criteria are met or every remaining item is explicitly deferred or excluded with rationale.

## Area Files

| Area | File | Why It Matters |
| --- | --- | --- |
| Provider, model, auth, image generation | `providers-models-auth.md` | Largest gap. Pi has broad provider/model coverage; AVA has strong foundations but narrower breadth. |
| CLI, slash commands, sessions, import/share/export | `cli-commands-sessions-share-import.md` | Pi's product workflow depends on command completeness and session mobility. |
| TUI/editor/terminal product UX | `tui-editor-terminal.md` | Pi has a reusable TUI library and polished coding-agent UX; AVA has strong runtime behavior but needs product closure. |
| Settings, packages, resources, startup | `settings-packages-resources.md` | Pi's settings and package/resource manager are a major product surface. |
| Agent loop, tools, permissions, safety | `agent-tools-permissions.md` | AVA is already strong here; the goal is consistency, UX closure, and no regressions while adding Pi parity. |
| Context, prompts, extensions, MCP, LSP | `context-extensions-mcp-lsp.md` | Pi's extensibility and prompt resources need AVA-native equivalents without weakening isolation. |
| Testing, release, documentation evidence | `testing-release-quality.md` | 100 percent parity needs mechanical evidence, not just feature claims. |

## Follow-Up Goal Files

| Goal | File | Why It Matters |
| --- | --- | --- |
| End-to-end AVA tool smoke | `end-to-end-ava-tool-smoke-goal.md` | Adds a dedicated full-binary, provider-backed dogfood smoke so release evidence proves AVA works as one complete coding-agent tool, not only as subsystem slices. |

## Definition Of 100 Percent

100 percent does not mean AVA literally copies Pi. It means every Pi baseline capability has one of these dispositions:

| Disposition | Requirement |
| --- | --- |
| Implemented | AVA has a C++23 implementation, tests, docs, and smoke or manual evidence where relevant. |
| AVA-superior | AVA implements the user outcome with a stronger safety or native architecture than Pi, with tests and docs. |
| Deferred | The product intentionally delays the capability, and the area file states the blocker and revisit trigger. |
| Excluded | The capability is not an AVA product goal, and the exclusion is documented in `docs/product/mvp-baseline.md` or this package. |

Rows left as vague `partial` are not 100 percent. Each area goal must reduce partials into implemented, deferred, or excluded rows.

## Execution Order

The highest leverage order is:

1. `testing-release-quality.md`: establish a release evidence matrix and Codex progress log pattern for the area.
2. `providers-models-auth.md`: close the biggest Pi parity gap or narrow it to explicit deferrals.
3. `cli-commands-sessions-share-import.md`: implement `/import`, `/share`, `/copy`, `/logout`, and CLI flag parity where accepted.
4. `settings-packages-resources.md`: define unified settings and package/resource installation scope.
5. `tui-editor-terminal.md`: close product polish gaps and virtual-terminal-style deterministic coverage.
6. `context-extensions-mcp-lsp.md`: finish prompt/resource/plugin/MCP/LSP parity decisions.
7. `agent-tools-permissions.md`: final consistency pass over safety, tool cards, permissions, and agent control language.

This order can change if a smaller area is needed to unblock an active branch, but do not mix unrelated areas in one `/goal`.

## Required Per-Area Output

Each completed area goal must leave behind:

- Code changes or a documented no-code decision.
- Tests or explicit reason tests cannot be automated.
- Updated `docs/product/mvp-baseline.md` rows.
- Updated `docs/product/mvp-coverage-ledger.md` evidence for newly checked rows.
- Updated area file if criteria, deferrals, or references changed.
- Verification log in the final response with commands run and skips.

## Standard Verification Commands

Run targeted commands during implementation, then use the full release path before declaring a broad area complete:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
git --no-pager diff --check
```

For terminal-visible behavior, also run the relevant opt-in smoke when prerequisites exist:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R ava_tui.tmux_smoke --output-on-failure
AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R ava_tui.kitty_image_smoke --output-on-failure
AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R ava_tui.osc8_smoke --output-on-failure
```

For live providers, run only when credentials exist:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
```

## Full-Goal Closure Audit

2026-07-04 local re-audit: all area files in this package are closed with evidence or explicit deferrals/exclusions, backend areas were verified before the final TUI pass, and the product baseline/coverage ledger reconcile to 86 checked rows with 86 coverage rows plus 14 unchecked rows carrying explicit deferred disposition.

The follow-up `end-to-end-ava-tool-smoke-goal.md` was added after this closure audit to fill an integrated full-binary dogfood evidence gap. It does not reopen the area-file closure result; it defines a post-closure release-hardening smoke.

2026-07-08 follow-up after M6/M7 plugin resource/install work and the Carlo debug print-members merge: no checkbox totals were recalculated. Product/parity docs now treat enabled static plugin prompt/skill resource autoload plus local/offline `/plugins install <path>` and `/plugins remove <id>` as implemented, while preserving deferrals for `/packages`, remote marketplaces, git/npm installs, self-update, source signing, provenance, and custom provider packages. Default validation passed 62/62 with expected provider-live and opt-in TUI smoke skips; libcwd/ctags-ON debug print-members codegen remains unvalidated locally because `ctags` and `ccache` are missing.

Final validation passed locally:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
ctest --test-dir build -R 'ava_tests\.(config_context_auth_oauth|provider_openai|provider_anthropic|agent_loop|agent_loop_resilience|agent_tool_dispatcher|tools|permission_rules|plugin|mcp|lsp|session|app_runtime|app_rpc|app_command_registry|tui_composer)$' --output-on-failure
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R '^ava_tui\.tmux_smoke$' --output-on-failure
AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R '^ava_tui\.kitty_image_smoke$' --output-on-failure
AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R '^ava_tui\.osc8_smoke$' --output-on-failure
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
git --no-pager diff --check
```

The live-provider smoke skipped because no supported provider credential variables were present. The final tmux smoke regenerated inspected visual captures under `build/tui-tmux-smoke/evidence/`.
