# Historical/Closed AVA MVP Baseline With Pi And TUI Verification

**Status: closed historical goal. Do not execute this file as current product or release direction.** Current authority is [`docs/product/principles.md`](../../docs/product/principles.md) and [`docs/product/release-readiness.md`](../../docs/product/release-readiness.md).

This goal drove AVA toward the then-current MVP baseline with TUI/frontend work in scope. It compared Pi.dev behavior while requiring AVA-native C++23 implementation, focused tests, and PTY/tmux evidence without copying reference source or architecture.

`facts.md` and `plan.md` preserve the shared acceptance rationale and execution record for that completed loop.

Done means every selected MVP checklist item is either implemented with deterministic tests plus required smoke evidence, or explicitly documented as deferred/excluded with rationale. Before handoff, run the relevant CMake build/tests, any required PTY/tmux or credential-gated smokes whose prerequisites are available, and `git --no-pager diff --check`.

Plan status: `plan.md` is closed and historical. `gate-note.md` records the unavailable Plannotator gate for provenance only.
