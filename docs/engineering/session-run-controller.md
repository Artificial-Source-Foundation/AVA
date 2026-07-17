# M2 Session Run Controller Contract

`RuntimeSession` directly owns one `SessionRunController`. It owns no worker, executor, runtime-wide session map, or detached control thread. The frontend/RPC/TUI caller owns the worker invocation; controller admission only returns a move-only `ActiveRunGuard`.

## Ownership and locking

- Only one guard may be active for a `RuntimeSession`. Moving a guard transfers that proof. Completing it, destroying it after an error, requesting stop, and session teardown all leave one terminal `RunOutcome` and release admission.
- `RuntimeSession` destruction requests stop. Guard state is reference-counted so a caller that is unwinding cannot dereference a destroyed controller.
- The controller state mutex protects admission, the validated phase table, stop source, snapshots, and bounded command/append queue reservation. It is never held across `SessionStore::append`, provider/tool work, stop callbacks, observer callbacks, or condition-variable notifications.
- `append_mutex` has one FIFO driver at a time. If a synchronous append observer calls `shutdown()`, shutdown marks the controller closing and requests stop without recursively acquiring that mutex; the driver closes queued tickets and releases the immutable target only after the observer and current append return. Observer-side `reset_persistence_failure()` is rejected immediately, while cross-thread shutdown/reset still serialize behind the driver.
- `inspect_admission()` explicitly distinguishes `Admit`, same-correlation `JoinExistingOutcome`, and bounded different-request rejection. `wait_outcome()` joins only the same correlation. `wake` is bounded to 64 commands / 64 KiB per command. Overflow is rejected at the frontend adapter boundary; existing accepted steering is delivered and the run continues. Existing TUI and RPC correlation queues remain presentation adapters that queue follow-ups before `run_prompt`; they do not own backend lifecycle. `request_stop` fires the guard stop token immediately.

## State and terminal contract

`RunPhase` transitions are centralized in `session_run_controller.cpp`. A run is admitted in `Admitted`, transitions through context/provider/tool/compaction phases, and terminates exactly once as `Completed`, `UserCanceled`, `Deadline`, or a typed failure reason. Invalid transitions and duplicate completion calls return `InvalidArgument` errors. The destructor supplies a terminal failure outcome if an error path did not explicitly complete the guard.

M2 does not implement M3 deadlines or a new cancellation stack. It adapts the existing cancellation callback to the controller stop token and preserves current transport/tool behavior.

## Append-routing migration boundary

During an active `run_prompt`, AgentLoop user/replay-user, assistant, reasoning, tool call/result, permission decision, error, and cancellation records pass `AgentLoopOptions::append_entry` to the RuntimeSession-owned bounded append route. The route serializes each legacy JSONL append without changing record bytes or ordering.

| Append class | M2 route |
| --- | --- |
| Parent user/assistant/reasoning/tool/permission/error/cancel | Generation-bound active route |
| Runtime compaction (auto/overflow) and plugin/file-reference audits | Generation-bound active route |
| Metadata/model/reasoning/mode commands | Stable RuntimeSession owner route |
| Background child failure notification to parent | Stable owner route, valid across A→B and while inactive; no `RuntimeSession&` capture |
| Child history | Independent child append target and `SessionReadAuthority`; child options clear both parent routes and never inherit parent read authority |
| Import/export/open/session creation and branch copy | Direct `SessionStore` only while inactive; M5 replaces this with the locked writer |

This is an additive M2 boundary: it does not rewrite JSONL history, change session/RPC/TUI/print bytes, add a second agent runtime, or claim M5 durable-writer semantics.

## Runtime read authority

`SessionReadAuthority` is the copyable history-read capability. A persistent authority owns a copied `SessionStore` and a duplicated matching `SessionLease`; an ephemeral authority owns only copied shared in-memory state. Its public data operations are `load`, `load_bounded`, and `inspect_bounded`. Binding validates the exact store/lease pathname and inode before duplicating the descriptor, and each read validates that the leased inode is still the sole publication at the canonical parent/name before and after its fixed-size snapshot.

Runtime history consumers receive this capability by value: AgentLoop/MessageBuilder, provider tool-call ID reconstruction, model compatibility validation, auto/manual compaction snapshots, current-session commands and permissions, line-shell status, ACP, and RPC serialization/session commands. Child and background loops bind a capability to their own child lease. A live pathname replacement therefore fails closed instead of supplying provider, compaction, or RPC context. Pathname reads remain intentional only for observational noncurrent session listing/tree metadata and legacy inactive compatibility adapters; `ava_tests.session_read_authority_inventory` keeps that grep inventory closed.

## M2 repair-pass invariants (2026-07-11)

- A guard and its active append route carry an immutable generation. Active routes reject terminal/stale generations; the distinct stable owner route remains usable during and between runs until shutdown or an explicit persistence latch.
- The coordinator gives each FIFO append ticket its own terminal status, bounds both item count and exact byte accounting, never calls `SessionStore::append` under its state mutex, rejects same-thread append/reset reentry, synchronizes terminal release with pending writes, and latches a persistence error until explicit recovery. One immutable staged failure is shared by the failed head and every queued ticket, preserving `append_commit_state` and preventing later queued writes after partial/unknown outcomes.
- `request_stop` only requests cancellation. A run already in `Completing` records `Completed`; otherwise the loop observes the stop token at its real boundary.
- AgentLoop publishes phase boundaries through the shared agent `RunPhase` contract. The callback is fallible, so lifecycle reporting cannot silently disappear.
- Child AgentLoop options explicitly clear both parent routes. Runtime-backed background parent errors use the stable owner route; the legacy detached fallback is limited to a standalone loop with no RuntimeSession route. `RuntimeSession` declares its controller before background jobs, so reverse destruction joins workers while the controller/store routes remain alive.

## Verification

`ava_tests.session_run_controller` covers valid/invalid transitions, move/destructor release, same/different admission inspection, same-correlation wait outcome, FollowUp commands, inactive owner routing, stale-generation rejection, snapshots, queue overflow, observer shutdown/reset reentrancy, queued cross-thread closing, exact byte drain, immutable persistence failures, and target lease release. Session/agent/compaction/RPC suites cover pathname replacement after authority binding. The focused `tsan` CMake preset runs this deterministic controller suite; it is separate from the ASan/UBSan preset and CI job.
