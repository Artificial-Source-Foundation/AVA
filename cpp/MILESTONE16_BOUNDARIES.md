# C++ Milestone 16 Boundaries (Narrow TUI Workflow Parity Basics)

This note records a deliberately narrow Milestone 16 slice on top of Milestone 15.

## Implemented in this M16 pass

1. Kept backend/orchestration ownership intact:
   - request/run lifecycle ownership remains in orchestration/runtime (`compose_runtime`, `RunController`, `InteractiveBridge`)
   - `ava_tui` remains an adapter/display layer and does not take ownership of run lifecycle semantics
2. Added minimal slash-command infrastructure in `ava_tui::AppState`:
   - `/help`, `/clear`, `/model`
   - graceful unsupported handling for `/compact`
3. Added narrow input history controls:
   - Up/Down history traversal with draft restore behavior
4. Added lightweight message-navigation/status visibility improvements:
   - explicit message range/total status line
   - top/bottom jump seams (`Home`/`End`) and retained page scrolling
5. Added adapter-facing interactive request visibility state in `AppState`:
   - approval/question/plan pending request tracking seams
   - explicit clearing seam for adapter-driven resets (`/clear`)
   - rendered interactive pending request summary in TUI status area
6. Added focused tests primarily in `ava_tui_state` coverage for the above seams.

## Explicitly Deferred (still out of this M16 pass)

1. Full FTXUI modal UX for approval/question/plan resolution.
2. Backend-interactive resolution controls initiated from TUI.
3. Full Rust TUI feature parity.
4. Broader async/background orchestration parity.

This pass is intentionally pragmatic: status/controller seams and parity basics without moving runtime ownership boundaries.
