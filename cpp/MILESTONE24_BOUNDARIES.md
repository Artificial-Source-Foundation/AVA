# C++ Milestone 24 Boundaries

M24 is a scoped runtime-control parity slice for the C++ backend/headless lane. It tightens cooperative cancellation propagation across parent and child runs and adds a headless signal-to-cancel bridge while keeping broad async scheduler, provider hard-kill, and background runtime parity explicitly deferred.

## In Scope

1. Add a parent-cancellation callback to `NativeTaskSpawnerOptions` and compose it with each child run's own `RunCancellationToken`.
2. Ensure native blocking child runs exit as cancelled when the parent run is cancelled, without requiring explicit `cancel_child_run(...)` lookup.
3. Add a headless `SIGINT`/`SIGTERM` bridge that records cancellation requests in a `sig_atomic_t` flag, restores prior handlers after the run, and folds that flag into the headless runtime `is_cancelled` callback. Both signals request cooperative cancellation for this scoped slice; they do not provide provider hard-kill or immediate process termination while a blocking provider/tool call is in flight.
4. Add focused `ava_orchestration_tests` and `ava_app_tests` coverage for parent-to-child cancellation propagation and the headless cancellation bridge state plus `SIGINT`/`SIGTERM` handler paths.

## Out of Scope

1. Full async/background scheduler parity.
2. Provider hard-kill/HTTP abort parity, second-signal escalation, and immediate OS signal termination semantics beyond existing cooperative cancellation.
3. Follow-up/post-complete CLI/TUI queue population parity.
4. Concurrent read-only tool execution and broader tool scheduler parity.
5. Full TUI runtime-control modal/widget parity.

## Validation

```bash
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner propagates parent cancellation into child run"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_tests "headless signal cancellation bridge records cancellation requests"
git --no-pager diff --check -- cpp/include/ava/orchestration/task.hpp cpp/src/orchestration/task.cpp cpp/apps/ava/headless_run.cpp cpp/apps/ava/signal_cancel.hpp cpp/apps/ava/signal_cancel.cpp cpp/apps/CMakeLists.txt cpp/tests/unit/orchestration_foundation.test.cpp cpp/tests/unit/ava_cli_headless.test.cpp cpp/MILESTONE24_BOUNDARIES.md CHANGELOG.md docs/project/backlog.md
```

## Follow-Up Green-Fix Notes

- The headless run path now clears the interactive bridge run id on runtime exceptions, matching the native child-run cleanup pattern.
- The headless signal-handler scope is non-copyable/non-movable because it owns process-global handler state.
- Parent-cancellation coverage now has a watchdog safety timeout and verifies cancelled child-run metadata is persisted to the child session.
- Signal bridge coverage now includes nested install/restore behavior in addition to direct `SIGINT`/`SIGTERM` cancellation requests.

## Decision Point

M24 intentionally closes only narrow cooperative control propagation gaps. Async/background scheduling, hard-kill provider interruption, and richer queue population remain deferred inventory for later milestones.
