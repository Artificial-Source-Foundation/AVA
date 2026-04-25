# C++ Milestone 26 Boundaries

Milestone 26 adds a narrow C++ TUI interactive-request dock slice. It improves the terminal operator workflow for existing approval/question/plan request handles while preserving the adapter-first rule: orchestration and the control plane own request lifecycle; the TUI displays pending state and sends request-id-bearing adapter actions.

## In Scope

1. **Interactive dock state in `AppState`:** track one active approval/question/plan dock from backend pending snapshots, ignore terminal/mismatched/empty handles, and use sticky-current selection while the currently visible request remains pending. Priority (`approval` -> `question` -> `plan`) applies when selecting a new dock after the current request resolves, cancels, or is dismissed.
2. **Adapter-backed dock actions:** build approve/reject/answer/accept-plan/reject-plan/cancel-question adapter actions from the active dock and apply `InteractiveActionAdapter` results back to TUI state only after the bridge accepts them.
3. **FTXUI dock rendering:** replace the composer with a bounded inline interactive request dock when a pending approval/question/plan is active, with keyboard hints for approve/reject/answer/accept/cancel flows. Approval is disabled when the projected tool payload is truncated; the operator must reject rather than blindly approve incomplete details.
4. **Live blocking resolver bridge:** TUI composition now supplies approval/question/plan resolvers that wait for adapter settlements, project minimal request previews into the dock, and return cancelled outcomes when run cancellation is requested. Adapter settlements observed before cancellation is requested are honored; after cancellation is requested, the resolver returns cancelled unless a settlement was already recorded.
5. **Plan/question cancellation adapter seams:** add the narrow `reject_plan_from_adapter` and `cancel_question_from_adapter` bridge paths needed for TUI rejection/cancellation evidence.
6. **Focused tests:** extend `ava_tui_tests` coverage for dock priority, terminal-handle filtering, request-detail projection, action construction, result application, dismiss-without-resolution state behavior, question cancellation, and plan rejection through the orchestration bridge.

## Out of Scope

1. Full Rust TUI modal/widget parity: command palette, session list, model selector, provider connect, theme selector, rewind, diff preview, copy picker, task list, markdown rendering, mouse interactions, and toast notifications remain deferred.
2. TUI lifecycle ownership: the TUI still does not own approval/question/plan request creation, terminal transitions, child-run lifecycle, runtime queue population, or backend cancellation semantics.
3. Rich request payload rendering: M26 docks display request kind, request ID, run ID, and minimal/truncated payload previews only; polished risk badges, structured full tool argument rendering, question option selection, scrolling payload inspection, and plan-step previews require future payload projection work.
4. MCP/plugin/custom-tool TUI UX and desktop/web UX remain deferred.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_tui_tests
git --no-pager diff --check -- \
  cpp/apps/ava_tui/main.cpp \
  cpp/apps/ava_tui/interactive_detail_projection.hpp \
  cpp/apps/ava_tui/interactive_detail_projection.cpp \
  cpp/apps/ava_tui/state.hpp \
  cpp/apps/ava_tui/state.cpp \
  cpp/apps/ava_tui/interactive_action_adapter.hpp \
  cpp/apps/ava_tui/interactive_action_adapter.cpp \
  cpp/include/ava/orchestration/interactive.hpp \
  cpp/src/orchestration/interactive.cpp \
  cpp/tests/unit/ava_tui_state.test.cpp \
  cpp/MILESTONE26_BOUNDARIES.md \
  CHANGELOG.md \
  docs/project/backlog.md \
  docs/architecture/cpp-backend-tui-migration-completion-gap-audit-m16.md \
  docs/architecture/cpp-backend-tui-parity-contract-audit-post-m16.md
```

## Follow-Up Green-Fix Notes

- Dock detail projection is now testable outside the FTXUI executable, and focused `ava_tui_tests` cover approval-preview completeness/truncation plus UTF-8-safe preview truncation.
- The Question dock hint no longer advertises `q=cancel run`; `q` is intentionally accepted as answer text in question mode while `Esc` remains the question-cancel key.
- Additional state tests cover sticky-current priority in both directions, dismissing one of multiple pending requests, empty-answer submissions, and dismissed-request pruning after backend clears.

## Decision Point

Future TUI parity work should decide whether to add request-payload projection for richer approval/question/plan docks, promote a reusable select-list/command-palette foundation, or continue backend migration work outside the TUI. M26 does not claim full TUI parity.
