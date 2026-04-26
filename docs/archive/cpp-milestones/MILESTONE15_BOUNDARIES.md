# C++ Milestone 15 Boundaries (First Narrow Pass)

This note records the first narrow Milestone 15 slice after accepted Milestone 14.

## Implemented in this M15 pass

1. Kept and actively wired an orchestration-owned per-run lease seam (`RunController`):
   - real per-run `run_id` values (`<session_id>:run:<n>`)
   - cooperative cancellation token + handle shared into runtime execution
2. Extended the current C++ runtime path (`ava_agent::AgentRuntime`) for narrow streaming parity:
   - emits incremental `AssistantResponseDelta` events from provider stream chunks
   - accumulates streamed tool-call deltas into executable tool calls
   - preserves non-stream fallback when stream chunks are unavailable
   - checks cooperative cancellation at turn boundaries, during stream iteration, and before tool execution
3. Threaded run identity into interactive lifecycle ownership:
   - approvals/questions/plans now register request handles against active run IDs when executing through wired runtime paths
   - no session-ID substitution for active interactive request correlation
4. Wired the seam into current foreground execution surfaces in minimal scope:
   - headless `ava` run path
   - minimal FTXUI `ava_tui` run path (including cooperative cancel request on `q` while running)
   - native blocking subagent spawn run path (run identity + internal cooperative token only)
5. Added focused test coverage for the M15 slice:
   - run lease ID/cancellation behavior
   - streaming delta + run ID event propagation
   - cooperative cancellation completion behavior
   - headless NDJSON delta/run_id mapping
   - minimal TUI state handling for streaming delta + cancelled completion

## Explicitly Deferred (still out of this M15 pass)

1. Background interactive arbitration and watchdog promotion parity.
2. MCP/plugin/background runtime parity.
3. Full TUI workflow parity for rich interactive request UX.
4. Broader async/runtime scheduling parity beyond this foreground cooperative cancellation seam.
5. Externally addressable child-run cancellation controls for subagent task spawns.

This pass is intentionally minimal and scoped to honest foreground streaming/cancellation ownership and run identity correlation.

## Follow-up green-fix notes

- Suppressed empty `AssistantResponse` events for streamed tool-call-only turns so headless/event consumers do not see semantically empty assistant messages before tool execution.
- Persisted the foreground headless `run_id` under `metadata.headless.last_run.run_id` to keep durable run identity evidence aligned with emitted events.
- Made the minimal TUI run thread explicitly clear `running` after runtime return instead of relying only on completion-event delivery.
