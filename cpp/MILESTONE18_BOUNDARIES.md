# C++ Milestone 18 Boundaries

M18 is a narrow TUI adapter-action harness slice on top of the M17 interactive terminal-state closure.

## In Scope

- Add a small `ava_tui` adapter layer for request-id-bearing approve, reject, answer, cancel-question, accept-plan, and reject-plan actions.
- Keep lifecycle settlement in orchestration/control-plane by dispatching adapter actions through `InteractiveBridge` methods.
- Reject missing, stale, and already-terminal request IDs without consuming newer pending requests.
- Preserve backend-origin clear boundaries: `AppState` visibility is not cleared optimistically by local adapter dispatch; it updates from bridge/backend pending snapshots.
- Add focused `ava_tui_tests` coverage for approve/reject/answer/cancel-question/accept-plan/reject-plan actions, stale/missing ID rejection, and backend-originated clearing.

## Follow-up green-fix notes

The follow-up pass tightened the display-only boundary and documented the already-implemented question-cancel/plan-reject actions:

1. `AppState::apply_interactive_action_result(...)` now updates only status text and no longer clears pending request visibility optimistically; bridge/backend pending snapshots remain the only clearing source.
2. The backend-clear validation test now uses the documented exact name: `tui state clears interactive request only on backend clear event`.
3. Focused tests cover default adapter fallbacks for approval rejection reason and empty question answers.

## Out of Scope

- Full FTXUI modal UX for approval/question/plan workflows.
- Child-run listing/cancellation, terminal child summaries, and watchdog/arbitration semantics; these remain M19 scope.
- Broad async/background runtime parity.
- Web/desktop/MCP/plugin/provider/auth/config breadth expansion.

## Validation

```bash
just cpp-build cpp-debug
./build/cpp/debug/tests/ava_tui_tests "tui adapter action approve resolves pending approval via bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action reject cancels pending approval via bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action answer carries request_id to bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action cancel-question cancels pending question via bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action accept-plan delegates to orchestration bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action reject-plan cancels pending plan via bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects stale or missing request id through bridge"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects unknown action kind"
./build/cpp/debug/tests/ava_tui_tests "tui adapter action rejects unavailable bridge"
./build/cpp/debug/tests/ava_tui_tests "tui state clears interactive request only on backend clear event"
just cpp-test cpp-debug
```

DP-2 outcome: the adapter-action harness is sufficient for scoped RP-2 evidence because it proves request-id-bearing actions settle through orchestration-owned stores while UI state remains display-only. Full modal/widget parity remains deferred.
