# Parallel Ordinary Tool Execution Plan

Status: staged implementation prep. Stages 1-2 of the recommended rollout have landed in this batch: sequential contract/replay tests, plus a dormant internal scheduler seam in `src/ava/agent/tool_scheduler.{h,cpp}` behind the current sequential path. No parallel ordinary-tool workers are enabled yet, and no provider prompt, user-facing tool text, CLI output, or RPC behavior advertises parallel execution.

## Problem

AVA can receive more than one provider tool call in a single assistant turn, but the normal agent loop still dispatches those calls sequentially in provider order. This is safe and easy to replay, but it leaves latency on the table for independent read/search/network work.

The relevant current surface is:

- `src/ava/agent/agent_loop.cpp`: parses the assistant turn, appends each `tool_call`, dispatches it, appends the `tool_result`, then moves to the next call.
- `src/ava/agent/tool_scheduler.{h,cpp}`: builds provider-ordered schedule slots, classifies tools conservatively for future scheduling decisions, and currently runs those slots sequentially through the agent-loop callback.
- `src/ava/agent/tool_dispatcher.cpp`: dispatches one `ProviderToolCall` through built-in, plugin, and MCP registry paths and gathers permission request ids into the structured result.
- `src/ava/tools/file_tools.cpp`: performs permission decisions/audit entries and owns file read/write/edit semantics.
- `src/ava/tools/mutation_queue.*`: serializes same-path mutations and multi-path patch locks in deterministic path order.
- `src/ava/agent/message_builder.cpp` and `src/ava/session/validation.cpp`: define replay assumptions for tool call/result pairing.
- `src/ava/agent/background_job_registry.*`: owns the already-shipped `task background=true` subagent path, which is intentionally separate from ordinary tool parallelism.

The landed scheduler classification is intentionally conservative and not yet load-bearing for concurrency:

- Identifiable future candidates: built-in filesystem read/search tools (`read_file`, `list_directory`, `glob`, `grep`).
- Barriers or deferred until separately reviewed: mutation tools (`write_file`, `edit_file`, `apply_patch`), shell/process tools, `question`, `task`/subagent, `skill`, LSP tools, network tools (`webfetch`, `websearch`), plugin/MCP-brokered external tools, and unknown or otherwise unreviewed tools.

Actual read/search/network epochs, worker pools, parallel permission gates, cancellation fan-out, and replay-ordering changes remain future work pending the design gates below.

## Goals

- Run independent ordinary tool calls from the same assistant turn concurrently when doing so does not change model-visible semantics.
- Preserve provider-order permission prompts, permission audit records, session replay, runtime events, and final tool results.
- Keep mutation safety at least as strong as today's per-path queue and all-validation-before-write patch behavior.
- Make cancellation bounded and deterministic even when multiple tool workers are running.
- Prove behavior with deterministic tests before exposing it by default.

## Non-goals

- Do not use `task background=true` as the implementation substrate for ordinary tool parallelism.
- Do not make mutation, shell/process, network, plugin, MCP, question, task/subagent, skill, LSP, unknown, or unreviewed tools parallel-eligible in the current dormant seam.
- Do not change provider prompts or advertise parallel behavior until replay and permission ordering are proven.

## Proposed model

Introduce a turn-local scheduler with explicit per-tool eligibility metadata. The scheduler should treat provider order as the semantic order and worker completion order as an implementation detail.

Future activation eligibility should be conservative:

- Parallel-eligible candidates: `read_file`, `list_directory`, `glob`, `grep`, and eventually `webfetch`/`websearch` only after their current permission, cancellation, network, and output-bound paths are proven thread-safe.
- Deferred until separately reviewed: LSP queries, `skill`, plugin tools, MCP tools, and other brokered external integrations.
- Barriers by default: `write_file`, `edit_file`, `apply_patch`, `bash`, `question`, `task`, unknown tools, and any tool whose metadata does not explicitly opt in.

The current landed classifier is narrower than this future target: it only identifies the built-in filesystem read/search tools as future candidates and keeps network tools deferred.

The scheduler should split a provider tool-call batch into epochs. A read/search/network epoch can run concurrently. A barrier tool runs alone in provider order. Calls after a mutation or shell command must not observe the workspace before that barrier completes, because today's sequential semantics let later file reads/searches see earlier mutations and command effects.

## Permission order

Permission resolution must remain provider-ordered, not worker-completion-ordered.

Recommended rules:

1. Parse and validate every call id/name/argument envelope in provider order before launching workers.
2. Assign each call a stable ordinal for logs, events, permission gates, and result slots.
3. Permission prompts and audit records that require user/RPC resolution pass through a single ordered gate. Call `N+1` cannot prompt before call `N` has either completed its permission path or failed closed.
4. Auto-allow and auto-deny policy decisions may be computed in workers only if their audit entries are committed through the same ordered gate.
5. Tools with late permissions, such as edit diff approval or LSP server launch approval, must re-enter the ordered permission gate at the point where the prompt is needed.
6. Headless/no-resolver ask paths must still fail closed and record the same policy/outcome audit entries as today.

This preserves the user's mental model: the first prompt shown corresponds to the first provider tool call that needs permission, even if later read-only work could finish sooner.

## Mutation queue

The existing mutation queue remains required and should be shared by all workers in the same runtime/session, not created per worker.

Rules for mutation-capable tools:

- Mutation tools are barriers for the first implementation.
- Same-path mutations continue through `MutationQueue::lock_path`.
- Multi-path mutations continue through `MutationQueue::lock_paths`, with paths sorted/deduplicated to avoid deadlock.
- If future non-overlapping mutations are allowed in the same epoch, each call must declare its complete affected path set before execution. Unknown or data-dependent path sets fall back to barrier execution.
- Reads/searches after any mutation barrier start only after the barrier commits or fails, preserving current sequential visibility.

## Session replay ordering

Current replay is a major constraint. The existing session path is safest when it records an assistant message followed by alternating provider-order pairs:

```text
AssistantMessage(tool_calls=N)
ToolCall(call_1)
PermissionDecision*(call_1)
ToolResult(call_1)
ToolCall(call_2)
PermissionDecision*(call_2)
ToolResult(call_2)
...
```

`message_builder.cpp` has native multi-tool replay logic that depends on matching a tool result immediately after its call, ignoring intervening permission-decision entries. If parallel execution instead persisted all calls first and results later, native replay behavior would need to be updated and likely treated as a session replay-ordering change under `docs/engineering/session-versioning.md`.

Design options:

- Minimal compatibility path: run only side-effect-free eligible calls in parallel, buffer their results, and append call/result pairs in provider order after each result is available. This preserves current replay but limits crash recovery and should not be used for mutating or external-side-effect tools.
- Durable batch path: append all tool-call entries in provider order before launching workers, then append results in provider order after completion. This is better for crash recovery but requires updating replay, validation, export expectations, and versioning before use.

The durable batch path is the better long-term shape. Until that replay work lands, parallel ordinary tools should remain disabled or limited to an internal experiment.

## Cancellation

Cancellation should be turn-scoped and cooperative:

- A single parent cancellation token fans out to every running worker.
- The scheduler stops launching new workers after cancellation is requested.
- Running workers use existing `cancel_requested` callbacks and process/network timeout paths.
- A barrier tool that is already running is canceled through its existing tool-specific boundary.
- Results for canceled workers are still committed in provider order as canceled/error structured tool results when a durable tool call entry exists.
- The session records one turn cancellation boundary plus any per-tool canceled results needed for replay; avoid appending orphan results for calls whose durable call entry was never committed.

## Deterministic tests

Required tests before enabling:

- Scheduler unit tests with fake blocking tools prove provider-order result commits despite reverse completion order.
- Permission tests prove prompts/audit entries appear in provider order when workers request permission out of order.
- Mutation tests prove same-path writes serialize and later reads observe committed mutations, not speculative parallel reads.
- Replay tests cover the selected session ordering and provider-native multi-tool reconstruction.
- Cancellation tests cover cancel before launch, cancel while workers are blocked, and cancel while a barrier tool is running.
- RPC/event tests prove clients receive stable ordinals, running/completed states, permission request ids, and terminal events without depending on thread timing.

Use fake tools/transports and condition variables rather than sleeps. Keep live smokes out of the gating path until the deterministic suite is stable.

## Why `task` background jobs are separate

The current `task` background path is not ordinary tool parallelism:

- It starts a child agent loop in a child session, not a second ordinary tool worker in the parent turn.
- It requires explicit `TaskRun` permission and returns `task_id`/`job_id` immediately; the child session is the source of truth for the full transcript.
- It uses a runtime-owned `BackgroundJobRegistry` with bounded job count, retained snapshots, cancellation, and join/shutdown ownership.
- Background children receive fresh provider and transport instances and intentionally do not inherit parent UI callbacks, LSP providers, or nested background factories.
- Parent session replay only sees the `task` tool result that queued or completed the subagent; it does not interleave the child tool timeline into the parent turn.

Ordinary parallel tools need a different scheduler because they must preserve one parent assistant turn's tool-call/result contract, permission order, mutation visibility, and provider replay semantics.

## Recommended rollout

1. Landed in this batch: replay/order tests that document the current sequential contract.
2. Landed in this batch: an internal scheduler abstraction behind the existing sequential path, with no parallel workers yet.
3. Promote the dormant conservative classification into explicit parallel eligibility metadata and deterministic fake-tool tests.
4. Choose and implement the replay ordering path, including versioning docs if the durable batch path is selected.
5. Enable read/search/network epochs behind an opt-in flag only after replay, permission-ordering, and cancellation gates are satisfied.
6. Consider broader tools only after separate safety reviews.
