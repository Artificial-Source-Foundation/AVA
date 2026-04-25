# C++ Milestone 17 Boundaries (Interactive Terminal-State Closure)

This note records a deliberately narrow Milestone 17 slice on top of Milestone 16.

## Implemented in this M17 pass

1. Kept backend/orchestration ownership intact:
   - `InteractiveRequestStore` remains the control-plane request lifecycle owner
   - `InteractiveBridge` remains the orchestration-owned resolver/settlement seam
   - TUI adapter action handling remains deferred to M18
2. Closed focused interactive request-store evidence for stale/non-existent request handling:
   - non-existent approval request IDs return no terminal transition
   - stale unknown request IDs do not consume pending requests
   - already-terminal request IDs cannot be re-resolved, cancelled, or timed out through the store
3. Confirmed terminal-state correlation preservation:
   - resolved terminal handles preserve `request_id`
   - terminal lookup preserves `run_id`
   - the next pending request remains current after stale/terminal settlement attempts
4. Added focused Catch2 coverage in `cpp/tests/unit/control_plane_contracts.test.cpp`:
   - `interactive request store rejects stale and non-existent requests`

## Follow-up green-fix notes

The follow-up pass kept the same control-plane-only ownership boundary and tightened the request-store contract evidence:

1. Made `InteractiveRequestStore::register_request(...)` roll back pending-map insertion if pending-order insertion throws, avoiding internally orphaned queue state under allocation failure.
2. Added direct string-conversion coverage for interactive request kinds/states, including unknown enum rejection.
3. Added default null `run_id` registration coverage and stronger terminal lookup assertions for preserved `request_id`, `kind`, and `run_id` correlation.
4. Expanded stale/already-terminal coverage across resolved, cancelled, and timed-out requests, and verified pending-order preservation for resolve, cancel, and timeout transitions.

## Explicitly Deferred (still out of this M17 pass)

1. TUI adapter action harness for approve/reject/answer/accept-plan flows.
2. Full FTXUI modal UX for approval/question/plan resolution.
3. Child-run cancellation/listing and watchdog/arbitration semantics.
4. Full async/background runtime parity.
5. Web/desktop/MCP/plugin/provider/auth/config breadth expansion.

This pass is intentionally contract-focused: it strengthens terminal-state evidence without moving lifecycle ownership into app or TUI layers.
