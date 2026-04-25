# C++ Milestone 19 Boundaries

M19 is a narrow child-run visibility/cancellation and watchdog slice. It keeps orchestration as the owner of child-run lifecycle state and keeps the TUI as an observer of child-run metadata.

## In Scope

1. Extend `RunController` cancellation tokens with an optional per-run deadline. Deadline expiry is cooperative and maps to a deterministic watchdog timeout summary.
2. Extend `NativeBlockingTaskSpawner` with active child-run listing, child-run lookup, explicit child cancellation by `run_id`, and terminal summary lookup.
3. Persist child-run terminal metadata under `metadata.orchestration.subagent_run`, including `run_id`, `completion_reason`, `watchdog_timed_out`, and existing lineage fields.
4. Add TUI observer projection state for active and terminal child-run summaries without letting `AppState` settle child lifecycle locally.
5. Add focused C++ tests for active child listing/cancellation, watchdog timeout summaries, and TUI child-run observer projection.

## Out of Scope

1. Full async/background runtime parity or a scheduler rewrite.
2. Hard-kill provider cancellation for blocking provider calls that do not cooperate with stream callbacks.
3. Full child-run modal/widget UX or TUI-owned child lifecycle settlement.
4. Web/desktop parity expansion.
5. MCP/plugin/provider/auth/config breadth expansion.

## Validation

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug
./build/cpp/debug/tests/ava_orchestration_tests "run controller issues unique run leases and cooperative cancellation state"
./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled before tool execution"
./build/cpp/debug/tests/ava_agent_tests "agent runtime preserves tool-call-only assistant message when cancelled during streaming"
./build/cpp/debug/tests/ava_agent_tests "agent runtime emits streaming assistant deltas with run_id"
./build/cpp/debug/tests/ava_agent_tests "agent runtime exits cooperatively when cancelled during streaming"
./build/cpp/debug/tests/ava_agent_tests "agent runtime cancels before tool execution after streamed assistant text"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner exposes active child runs for cancellation"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner watchdog timeout surfaces deterministic terminal summary"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner runs child sessions"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner enforces spawn budget"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner threads interactive resolvers into child composition"
./build/cpp/debug/tests/ava_tui_tests "tui state projects child-run terminal metadata without owning lifecycle"
just cpp-test cpp-debug --output-on-failure
```

## Decision Point

DP-3 outcome: the M19 scoped child-run cancellation/watchdog evidence is sufficient for the current backend/headless/TUI completion roadmap. Broad async scheduler parity, hard provider interruption, and richer child-run UI remain deferred.

## Follow-up Green-Fix Notes

- Exception-path child runs now best-effort persist `metadata.orchestration.subagent_run` terminal metadata and record in-memory summaries without masking the original runtime exception.
- `RunCancellationHandle` truthiness now represents explicit cancellability rather than deadline observability, and tests cover default/no-op handles plus future deadline transitions.
- Watchdog terminal classification now records deadline-driven cancellation at the runtime polling point instead of re-checking wall-clock time after return, avoiding explicit-cancel misclassification if a deadline expires during teardown.
- TUI child-run observer state now preserves terminal `error` details, carries watchdog-timeout projection, avoids duplicate-terminal status clobbering, and strengthens bounded active-run eviction assertions.
