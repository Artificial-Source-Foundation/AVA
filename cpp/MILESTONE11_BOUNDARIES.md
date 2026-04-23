# C++ Milestone 11 Boundaries

This note records the smallest honest **FTXUI-based interactive TUI slice** added on top of Milestone 10.

## Implemented in Milestone 11

1. Added a real `ava_tui` executable under `cpp/apps/ava_tui/` using FTXUI when available (`AVA_WITH_FTXUI=ON` + resolved linkage).
2. Added minimal TUI app state (`AppState`) with only:
   - scrollable message list state
   - composer/input buffer
   - status line
   - quit request state
3. Added a blocking terminal event loop that supports:
   - keyboard text entry
   - Enter submit
   - Up/Down/PgUp/PgDn message scrolling
   - `q` quit
4. Wired submit path into the existing C++ runtime stack (`ava_session`, `ava_llm`, `ava_tools`, `ava_agent`) by reusing the current blocking runtime composition pattern and callback/event-sink updates.
5. Added focused Milestone 11 test coverage for TUI state/event/scroll behavior (`ava_tui_tests`).
6. Enabled clean FTXUI dependency activation for this milestone (package lookup with fetch fallback when `AVA_WITH_FTXUI=ON`).

## Explicitly Deferred (out of Milestone 11 scope)

1. Full modal system.
2. Sidebar/task panels.
3. Mouse support.
4. Voice/watch mode.
5. Subagent views/background task UI.
6. Autocomplete / @mentions / slash-command UX.
7. Full async/streaming parity with Rust TUI.
8. Full theme system and advanced layout parity.
9. Interactive approval dock.

Milestone 11 is intentionally narrow: it establishes a real interactive terminal lane over the current blocking C++ runtime foundations without claiming Rust TUI parity.
