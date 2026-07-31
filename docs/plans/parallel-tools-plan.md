# Parallel Ordinary Tool Execution Plan

Status: internal/backend opt-in landed, default off. The staged rollout has now landed a scheduler seam, a bounded parallel scheduler engine, and `AgentLoop` integration behind `AgentLoopOptions::parallel_read_search_tools` with `parallel_read_search_max_workers`. The active eligible set is deliberately narrow: builtin `read_file`, `list_directory`, `glob`, and `grep` only, and only after preflight proves the call cannot prompt. There is still no CLI/TUI/RPC public flag, no provider-prompt or user-facing claim that ordinary tools run in parallel, no default behavior change, and no full-binary smoke for this path unless a future public/headless flag is added.

## Problem

AVA can receive more than one provider tool call in a single assistant turn. The default agent loop still dispatches ordinary tool calls sequentially in provider order, which is the safest user-facing behavior for permission prompts, session replay, and mutation visibility. An internal backend option can now run a conservative read/search subset concurrently to prove the implementation path without exposing it as product behavior yet.

The relevant current surface is:

- `src/ava/agent/agent_loop.cpp`: parses the assistant turn, preflights provider-ordered tool calls, and uses the parallel scheduler only when `AgentLoopOptions::parallel_read_search_tools` is true.
- `src/ava/agent/agent_loop.h`: owns the default-off internal options `parallel_read_search_tools` and `parallel_read_search_max_workers`.
- `src/ava/agent/tool_scheduler.{h,cpp}`: builds provider-ordered schedule slots, splits read/search epochs from barriers/deferred calls, runs eligible epochs with bounded `std::jthread` workers, propagates `std::stop_token` cancellation, and returns provider-ordered outcomes.
- `src/ava/agent/tool_dispatcher.{h,cpp}`: dispatches one `ProviderToolCall` through built-in, plugin, and MCP registry paths; the AgentLoop integration uses copied worker contexts and buffered callbacks for preflight-proven read/search calls.
- `src/ava/tools/file_tools.cpp` and search helpers: implement the currently eligible builtin read/search tools with bounded output and permission checks.
- `src/ava/tools/mutation_queue.*`: still serializes mutation tools; mutation tools remain barriers and are not parallelized.
- `src/ava/agent/message_builder.cpp` and `src/ava/session/validation.cpp`: replay still expects provider-order tool call/result pairing, so the internal opt-in commits completed outcomes in that order.
- `src/ava/agent/background_job_registry.*`: owns the already-shipped `task background=true` subagent path, which remains separate from ordinary tool parallelism.

The landed eligibility is intentionally conservative:

- Parallel-ready candidates: builtin filesystem read/search tools `read_file`, `list_directory`, `glob`, and `grep`, only after argument parsing plus permission preflight prove the call is non-interactive.
- Sequential barriers or deferred until separately reviewed: mutation tools (`write_file`, `edit_file`, `apply_patch`), shell/process tools, `question`, `task`/subagent, `skill`, LSP tools, network tools (`webfetch`, `websearch`), plugin/MCP-brokered external tools, and unknown or otherwise unreviewed tools.
- Ask/unpreflightable paths: any call that can require an interactive permission resolver, cannot be locally preflighted, or would need live user/question/LSP/task callbacks stays on the existing sequential path.

## Goals

- Run independent ordinary tool calls from the same assistant turn concurrently only when doing so does not change model-visible semantics.
- Preserve provider-order permission prompts, permission audit records, session replay, runtime events, and final tool results.
- Keep mutation safety at least as strong as today's per-path queue and all-validation-before-write patch behavior.
- Make cancellation bounded and deterministic even when multiple tool workers are running.
- Prove behavior with deterministic tests before exposing it by default or through a public flag.

## Non-goals

- Do not use `task background=true` as the implementation substrate for ordinary tool parallelism.
- Do not make mutation, shell/process, network, plugin, MCP, question, task/subagent, skill, LSP, unknown, or unreviewed tools parallel-eligible in the current internal opt-in.
- Do not claim user-facing/default parallel tools until a public CLI/RPC/TUI/headless surface, full-binary smoke, and release docs are added.
- Do not change session schema or switch to durable batch replay ordering in this internal activation.

## Landed internal model

The scheduler treats provider order as the semantic order and worker completion order as an implementation detail. It splits a provider tool-call batch into epochs. A ready read/search epoch can run concurrently with a bounded worker cap; a barrier tool runs alone in provider order; deferred/unknown/unready slots stay sequential. Calls after a mutation or shell command do not observe the workspace before that barrier completes, preserving today's sequential visibility.

Current activation eligibility is narrower than the long-term target:

- Active now behind internal opt-in: builtin `read_file`, `list_directory`, `glob`, and `grep` with preflight-proven non-interactive permission decisions.
- Future candidates only after separate review: `webfetch`/`websearch`, LSP queries, `skill`, plugin tools, MCP tools, and other brokered external integrations.
- Barriers by default: `write_file`, `edit_file`, `apply_patch`, `bash`, `question`, `task`, unknown tools, and any tool whose metadata does not explicitly opt in.

## Permission order

Permission resolution remains provider-ordered, not worker-completion-ordered.

The landed internal opt-in uses these rules:

1. Parse and validate the call id/name/argument envelope in provider order before launching workers.
2. Assign each call a stable provider index for logs, events, permission gates, and result slots.
3. Mark a slot `PreflightProvenNonInteractive` only when local argument parsing plus `permissions::decide` prove the expected permission path is Allow or Deny without Ask.
4. Keep Ask, unparseable, unpreflightable, and late-permission calls on the sequential barrier path so live prompts still occur on the normal ordered resolver path.
5. Remove live permission/question/task/LSP resolvers from worker contexts. If a filesystem race invalidates preflight and a worker would need a prompt, it fails closed instead of prompting from a worker thread.
6. Buffer worker permission audit/progress callbacks per provider slot and commit them in provider order.

This preserves the user's mental model: the first prompt shown corresponds to the first provider tool call that needs permission, even though later non-interactive read/search work may have been eligible for concurrent execution.

## Mutation queue

The existing mutation queue remains required and shared by the runtime/session, not created per worker.

Rules for mutation-capable tools:

- Mutation tools are barriers for the current implementation.
- Same-path mutations continue through `MutationQueue::lock_path`.
- Multi-path mutations continue through `MutationQueue::lock_paths`, with paths sorted/deduplicated to avoid deadlock.
- If future non-overlapping mutations are allowed in the same epoch, each call must declare its complete affected path set before execution. Unknown or data-dependent path sets fall back to barrier execution.
- Reads/searches after any mutation barrier start only after the barrier commits or fails, preserving current sequential visibility.

## Session replay ordering

Current replay remains a major constraint. The existing session path is safest when it records an assistant message followed by alternating provider-order pairs:

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

`message_builder.cpp` has native multi-tool replay logic that depends on matching a tool result immediately after its call, ignoring intervening permission-decision entries. If parallel execution instead persisted all calls first and results later, native replay behavior would need to be updated and likely treated as a session replay-ordering change under `docs/development/session-versioning.md`.

The internal opt-in therefore uses the minimal compatibility path: run only side-effect-free eligible calls in parallel, buffer their results/audit/progress, and append `ToolCall`, running event, buffered audit/progress, `ToolResult`, and final event in provider order. This preserves current replay, but it intentionally limits crash-recovery semantics and is not suitable for mutating or external-side-effect tools.

The durable batch path remains the better long-term shape if AVA ever wants public/default broader parallel execution. That path would append all tool-call entries in provider order before launching workers, then append results in provider order after completion, and it would require replay, validation, export, and versioning work before use.

## Cancellation

Cancellation is turn-scoped and cooperative:

- AgentLoop checks cancellation before launching a ready epoch.
- Each active ready epoch receives an epoch-local `std::stop_source`; workers observing `options_.cancel_requested` request the scheduler stop token.
- The scheduler stops launching new workers after its stop token is requested.
- Already-launched read/search workers return completed, denied, failed, or canceled dispatch results through the same ordered outcome vector.
- AgentLoop commits the contiguous recorded prefix in provider order, appends the normal turn cancellation boundary when cancellation wins, and stops before later barriers/epochs or provider continuation.
- Calls whose durable `ToolCall` entry was never committed do not get orphan results.

This behavior is internal only and covered at scheduler and AgentLoop levels; a future public flag should add a full-binary smoke that can exercise cancellation through the exposed surface.

## Deterministic tests and evidence

Current evidence for the internal opt-in:

- `ava_tests.tool_scheduler`: epoch splitting, reverse worker completion, worker cap behavior, stop-token cancellation, pre-stopped cancellation, executor exception conversion, null/zero-worker guards, provider-order first error, non-eligible sequential behavior, and real builtin `read_file`/`list_directory`/`glob`/`grep` scheduler dispatch.
- `ava_tests.agent_loop`: default-off integration coverage, provider-order read/search progress/audit/session/replay, zero-worker cap clamping, Ask fallback through the sequential resolver path, active-epoch cancellation stopping unstarted slots, and cancellation before a later barrier.
- Focused sanitizer suites from the M1/M2 implementation journal covered scheduler, AgentLoop, session, and tools paths, including real read/search parallel execution.

No full `ava` binary smoke currently exercises this path because there is no public CLI/RPC/TUI/headless option to turn it on. If a future public/headless flag is added, add a full-binary fake-provider smoke before documenting user-facing parallel-tool behavior.

## Why `task` background jobs are separate

The current `task` background path is not ordinary tool parallelism:

- It starts a child agent loop in a child session, not a second ordinary tool worker in the parent turn.
- Its launch is automatically allowed and audited, returns `task_id`/`job_id` immediately, and leaves the child session as the source of truth for the full transcript. Foreground nested Ask actions use normal permission UI; background nested Ask actions fail closed.
- It uses a runtime-owned `BackgroundJobRegistry` with bounded job count, retained snapshots, cancellation, and join/shutdown ownership.
- Background children receive fresh provider and transport instances and intentionally do not inherit parent UI callbacks, LSP providers, or nested background factories.
- Parent session replay only sees the `task` tool result that queued or completed the subagent; it does not interleave the child tool timeline into the parent turn.

Ordinary parallel tools need a different scheduler because they must preserve one parent assistant turn's tool-call/result contract, permission order, mutation visibility, and provider replay semantics.

## Rollout state and remaining work

1. Landed: replay/order tests and the scheduler abstraction that document the current sequential contract.
2. Landed: bounded `std::jthread`/`std::stop_token` parallel scheduler engine with conservative barrier/deferred semantics.
3. Landed: internal AgentLoop opt-in for preflight-proven builtin read/search epochs, buffered audit/progress, provider-order session/timeline commit, and cooperative cancellation behavior.
4. Remaining before any user-facing claim: decide whether AVA needs a public/headless flag, wire it through the chosen CLI/RPC/TUI surface, document exact semantics, and add a full-binary fake-provider smoke for the exposed option.
5. Remaining future work: network tools, LSP, skill, task/subagent, plugin, MCP, and broader external tools stay deferred until separate thread-safety, permission, cancellation, and replay reviews land.
6. Remaining research: durable batch session ordering and any move toward default-on parallel execution require session-versioning/replay/export design and separate approval.
