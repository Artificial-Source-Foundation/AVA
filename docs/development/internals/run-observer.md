# M1 RunObserver trace

`RunObserver` is a local, disabled-by-default diagnostic/evaluation facility. It is not a `RuntimeEvent` adapter: product events remain UI/RPC events, session JSONL remains authoritative conversation history, and observer JSONL is a separate non-authoritative artifact.

## Schema v1

Each canonical JSONL object has `schema_version`, `sequence`, `timestamp_ms`, `type`, `run_id`, `turn_id`, `call_id`, `session_id`, `provider_id`, `phase`, `outcome`, and a sorted `fields` object. Schema-v1 strings are serialized centrally from closed C++ event/phase/outcome enums. IDs and timestamps can be injected through `RunObservation` for byte-identical tests.

M1 emits these non-authoritative boundaries:

- `agent.run_start` and `agent.run_terminal`; terminal outcome is `completed`, `canceled`, `provider_error`, `tool_error`, `session_error`, or `failed` where observable.
- `transport.request_result` for a logical transport request, `transport.attempt_result` for retry inner attempts, and `transport.retry`. They contain method, status, and request/response byte totals only—never URL, query, headers, body, or credentials.
- `session.append_attempt`/`session.append_result` and `session.load_attempt`/`session.load_result`. Result is emitted after the authoritative operation has completed; observers are outside SessionStore's internal locks.
- `tool.dispatch_start`/`tool.dispatch_result`, provider stream metadata, and `process.start`/`process.result`. Process result aggregates output bytes; M1 deliberately does not emit one event per output chunk.

## Safety and defaults

- No observer is constructed by default: no trace file is created and normal runtime/session/RPC output is unchanged. An AgentLoop clears its SessionStore observation at every exit, including disabled and failed runs.
- `RunObservation::emit(...) noexcept` is the only production emission entrypoint. Its lambda covers event construction, clock/ID enrichment, serialization at the writer, and callback delivery; every exception is accounted without changing the observed result. Scope destructors are explicitly `noexcept`.
- `JsonlRunObserver` remains deterministic synchronous JSONL for tests. `QueuedJsonlRunObserver` is the production writer: it enqueues only already-bounded serialized records in bounded item and byte queues, using a brief normal mutex only for deque bookkeeping (writer I/O is outside that mutex). `queue_dropped` counts only records rejected after closing or at configured item/byte capacity—never producer mutex contention; `queue_failures` and queue high-water counters remain separately accounted. It owns and joins a `jthread`, drains accepted records on normal close, and safely closes with concurrent producers. A regular-file write already in progress is OS-uninterruptible, so close can wait for that one operation but never detaches a thread.
- Trace paths are walked descriptor-relatively from a stable root using `mkdirat`/`openat` and `O_NOFOLLOW`. Only newly-created directories are chmodded. The retained final fd is opened `O_NONBLOCK`, verified as a private effective-user-owned regular single-link file with no group/other bits, lifetime-locked nonblockingly, and receives each JSONL line plus newline in one write operation. FIFOs, devices, hardlinks, symlinks, and competing writers fail closed.
- Field provenance controls redaction. Secret, authorization-header, and environment values are replaced; prompt/tool/request/output content and paths are omitted. Provenance is primary, with a small exact canonical-key credential allowlist as defense in depth for accidentally public Authorization/API-key/token/secret and recognized credential-environment fields; it is not broad regex redaction. Public metadata is otherwise individually bounded. Duplicate serialized field keys are deterministically omitted rather than producing ambiguous JSON.
- Q21 default applies: traces stay on the local machine; M1 implements no exporter or egress. Q20 default applies: no paid/live provider run is enabled or required.

## Disabled fast-path timing (non-gating)

The deterministic unit test proves a disabled `emit` does not call its enricher, clock, or ID generator and creates no observer artifact. A separate local, non-gating microbenchmark is recorded in `fixtures/eval/m1/disabled-fast-path-timing.json`: on 2026-07-11 it measured 10,000,000 default-disabled `emit` calls at 2.99446 ns/call with Ubuntu GCC 13.3.0 on Linux 6.17 x86_64. The measurement is informational only—not a CTest gate or a cross-machine performance claim.

To reproduce the method without making it a test gate, compile a small `RunObservation{}` loop with `-O2`, run 10,000,000 `emit(AgentRunStart,{})` calls, and report `steady_clock` elapsed nanoseconds divided by the iteration count. The fixture records the exact local command and result.

To enable locally, construct a `JsonlRunObserver`, wrap it in `RunObservation`, and pass that optional value to the owning `AgentLoopOptions` (or direct test boundary). Production configuration intentionally has no automatic trace path in M1.
