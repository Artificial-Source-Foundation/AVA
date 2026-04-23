# C++ Milestone 12 Boundaries

This note records a bounded **FTXUI TUI parity validation + cleanup pass** on top of Milestone 11.

## Implemented in Milestone 12

1. Tightened `ava_tui` state/event behavior without broadening the feature surface:
   - richer `AgentEvent` -> status/message mapping for run start, turn start, tool call/result, and run error paths
   - clearer tool-result log formatting (`tool_result[call_id]: ok|error` when call id is available)
   - improved error status readability (`Run error: <message>` when present)
2. Added focused `AppState` coverage for M11 residual edge behavior:
   - empty/whitespace submission rejection
   - backspace on empty buffer safety
   - page-up/page-down clamping behavior
   - multiline submission + trailing newline trim behavior
   - expanded runtime event mapping assertions beyond completion-only coverage
3. Applied one low-risk runtime/TUI seam cleanup by moving `post_custom_event()` out of the state lock in the TUI runtime callback to reduce unnecessary lock coupling.
4. Updated the `ava_tui` CLI description string to Milestone 12 wording.

## Explicitly Deferred (still out of Milestone 12 scope)

1. Modal/sidebar/task-pane frameworks.
2. Mouse support.
3. Voice/watch mode.
4. Async/streaming parity with Rust TUI.
5. Interactive approval dock and richer approval UX.
6. Autocomplete / @mentions / slash-command UX expansion.
7. Major TUI architecture reshaping beyond this bounded cleanup pass.

Milestone 12 is intentionally narrow: it validates and tightens the current blocking FTXUI slice, improves correctness coverage, and makes a small boundary cleanup without claiming broad feature parity.
