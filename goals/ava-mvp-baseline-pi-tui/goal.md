# AVA MVP Baseline With Pi And TUI Verification

Drive AVA toward the MVP baseline in `docs/product/mvp-baseline.md` with TUI/frontend work in scope. For each selected P0/P1 gap, compare Pi behavior from `docs/reference-code/pi`, implement the AVA-native C++23 version without copying reference source or architecture, add focused tests, and verify real terminal behavior through PTY/tmux smoke paths when the change affects the full-screen TUI.

Use `facts.md` as the shared acceptance facts for scope, reference policy, and verification expectations. Use `plan.md` as the execution plan for the MVP loop.

Done means every selected MVP checklist item is either implemented with deterministic tests plus required smoke evidence, or explicitly documented as deferred/excluded with rationale. Before handoff, run the relevant CMake build/tests, any required PTY/tmux or credential-gated smokes whose prerequisites are available, and `git --no-pager diff --check`.

Plan status: `plan.md` is self-set and active. `gate-note.md` records the unavailable Plannotator gate for provenance only.
