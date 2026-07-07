# AVA MVP Baseline With Pi And TUI Verification Plan

## Plan Status

This is the active self-set execution plan. The user explicitly directed the agent to set the plan directly, so Plannotator gate approval is not required before launching the goal.

## Solution Approach

Drive AVA toward `docs/product/mvp-baseline.md` with a repeatable gap loop: pick a P0/P1 unchecked item, compare the behavior against Pi under `docs/reference-code/pi`, decide the AVA-native behavior, implement the smallest C++23 change, add focused tests, run real verification, and update the MVP checklist/docs with evidence. The current priority is TUI/frontend maturity, but backend/RPC/session/provider/tool work remains in scope when a TUI feature needs safe data or runtime contracts.

Reference code is behavior input only. Do not copy Pi source or architecture into AVA.

## Ordered Steps

1. Establish the MVP work ledger.

   Touches: `docs/product/mvp-baseline.md`, possibly a new progress note under `docs/roadmap/` or `goals/ava-mvp-baseline-pi-tui/`.

   Work:
   - Normalize the baseline now that TUI/frontend is in scope by removing stale ownership language from the checklist text.
   - For every unchecked P0/P1 item, record one of: `implement`, `needs-decision`, `defer`, or `exclude`.
   - Link each selected item to the Pi area reviewed, usually `docs/reference-code/pi/packages/coding-agent/docs/*`, `docs/reference-code/pi/packages/coding-agent/src/*`, or `docs/reference-code/pi/packages/tui/*`.
   - Keep P2/research items documented as deferred unless they unblock a P0/P1 item.

   Verification:
   - `git --no-pager diff --check`
   - Manual review that no reference repo files are included in build/test/source-search changes.

2. Add the terminal smoke harness before broad TUI claims.

   Touches: `tests/`, `tests/CMakeLists.txt`, and TUI testing docs as needed.

   Work:
   - Add a gated PTY/tmux smoke runner that can start `ava` in a real terminal, set rows/columns and `TERM`, send keys/escape sequences, resize, capture visible screen text, and terminate cleanly.
   - Prefer Python standard `pty`/`termios`/`fcntl` plus installed `pexpect` where useful; use `tmux capture-pane` for an alternate-screen capture path because `tmux` is available in this environment.
   - Keep the smoke deterministic and opt-in or prerequisite-gated if it cannot be reliable in all CI environments.
   - Assert stable visible behavior, not raw full escape-sequence snapshots: visible text, dimensions, cursor/cancel state where observable, and absence of leaked paste markers/control sequences.

   Verification:
   - `ctest --test-dir build -R 'tui|pty|tmux' --output-on-failure` after the harness exists.
   - If prerequisites are missing, record the exact skipped prerequisite and command in the handoff.

3. Prioritize the TUI maturity slice from the MVP baseline.

   Touches: `src/ava/tui/composer*.{cpp,h}`, `src/ava/tui/runtime.cpp`, `src/ava/tui/keybindings.*`, `src/ava/app/command_palette.*`, `src/ava/app/commands.*`, `tests/tui_composer_tests.cpp`, `tests/app_runtime_tests.cpp`.

   Work loop for each selected TUI item:
   - Compare Pi behavior for the same area, especially Pi editor, input, select-list, virtual-terminal, overlay, markdown, keybinding, and terminal docs/tests.
   - Implement the AVA-native behavior in existing TUI seams: `ComposerSnapshot`, render helpers, input reducer helpers, runtime select-list/modal flow, command catalog slash items, and tool-card rendering.
   - Keep runtime code presentation-focused. Permission decisions, session mutation, provider state, tools, and persistence stay in backend modules.
   - Start with high-value MVP gaps: multiline editor polish and history/paste/cursor behavior; slash-command palette autocomplete and disabled-command diagnostics; file/path autocomplete and `@` references; session tree/fork/clone workflow over existing backend/RPC contracts; permission/tool-card UX; markdown/code/diff rendering; keyboard discovery; terminal resize/performance/accessibility checks.

   Verification:
   - Add deterministic tests in `tests/tui_composer_tests.cpp` or focused new suites for renderer/editor/state behavior.
   - Add app command/runtime tests in `tests/app_runtime_tests.cpp` for slash commands, model/session selectors, command catalog metadata, and backend contracts.
   - Run affected suites with `ctest --test-dir build -R 'tui_composer|app_runtime' --output-on-failure`.
   - Run PTY/tmux smoke for full-screen behavior touched by the change.

4. Implement backend contracts needed by TUI work.

   Touches as needed: `src/ava/app/rpc/*`, `src/ava/app/command_*`, `src/ava/session/*`, `src/ava/permissions/*`, `src/ava/context/*`, `src/ava/config/*`, `src/ava/agent/*`, tests for those modules.

   Work:
   - For session tree UI, reuse or extend existing backend/RPC tree, fork, clone, name, label, and switch-session contracts before adding TUI presentation.
   - For file references, prompt templates, context freshness, settings reload, and project trust, keep parsing, validation, writes, and security decisions in backend/config/context modules.
   - For permission UX, expose denial reasons, risk labels, audit IDs, diff summaries, and rule-management data through backend-safe APIs rather than embedding policy in TUI code.
   - For tool-card maturity, ensure runtime events carry progress, truncation, spill path, changed path, diff, permission, and failure metadata.

   Verification:
   - Add focused CTest coverage for each backend contract touched.
   - Run the affected module suite plus any relevant CLI/RPC CMake smoke tests.

5. Validate provider and optional live-smoke items only when prerequisites exist.

   Touches as needed: provider tests, docs, smoke scripts.

   Work:
   - Keep credential-gated live provider smoke paths for OpenAI, Anthropic, Moonshot, OpenRouter-compatible, and other providers selected by the baseline.
   - Do not block ordinary TUI work on missing provider credentials; document skipped credential-gated checks.
   - For external terminal artifacts such as VHS or asciinema, use them only when installed and useful as review evidence. They supplement PTY/tmux assertions rather than replacing them.

   Verification:
   - `ctest --test-dir build -R provider_live_smoke --output-on-failure` when credentials exist.
   - Explicit skip note when credentials or tools are absent.

6. Keep docs and checklist evidence synchronized after every slice.

   Touches: `docs/product/mvp-baseline.md`, `docs/USAGE.md`, `docs/CONFIG.md`, `docs/headless-protocol.md`, version/roadmap docs as applicable.

   Work:
   - Mark checklist items complete only when implementation, tests, and smoke evidence agree.
   - If an item is deferred or excluded, document the product rationale and any Pi comparison that informed it.
   - Document terminal-smoke commands and residual risk for TUI items.

   Verification:
   - `git --no-pager diff --check`
   - Documentation review against implemented behavior and CLI/RPC/TUI contracts.

7. Run final verification for each completed slice.

   Commands:
   - `cmake -S . -B build -DAVA_BUILD_TESTS=ON`
   - `cmake --build build`
   - `ctest --test-dir build --output-on-failure`
   - Targeted PTY/tmux smoke command or script for TUI behavior touched by the slice.
   - Credential/tool-gated live smokes when prerequisites exist.
   - `git --no-pager diff --check`

## Risks And Open Questions

- The MVP checklist is broad. Keep each `/goal` execution slice small enough to complete with tests and evidence instead of attempting the entire baseline in one code pass.
- Real terminal testing can be flaky if it relies on timing. The harness must normalize timing-sensitive output, wait for stable visible state, and clean up child processes/sessions.
- `pyte` is not installed in this environment. A future terminal-screen parser should either avoid that dependency, vendor a tiny parser deliberately, or document installation as an optional prerequisite.
- TUI work may reveal backend contract gaps. Implement those contracts in backend modules first, then render them in the TUI.
- Pi supports a broad extension/theme/custom UI model. AVA should borrow behavior shape only where it supports the MVP; package manager, marketplace, remote install, and broad extension surfaces remain deferred unless explicitly pulled into P0/P1.
