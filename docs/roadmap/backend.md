# AVA Backend Roadmap

This roadmap defines the backend work needed for AVA 1.0 to feel as capable as PI while keeping AVA's product constraints: native C++23, one binary, terminal first, explicit safety boundaries, and inspectable local files.

PI is a reference target for backend power, not an architecture to copy directly. AVA should borrow the capability shape, then implement it with narrow C++ modules and clear permission boundaries.

## 1.0 Backend Goal

AVA 1.0 should be a reliable local coding-agent backend that supports:

- Long-running interactive and headless sessions.
- Strong streaming agent events for TUI and automation clients.
- Durable sessions with compaction, usage records, export, and branching-ready structure.
- Provider abstraction with at least OpenAI plus one additional production-quality provider path.
- Provider/model metadata for context windows, cost, tool support, streaming, and reasoning controls.
- Safe, high-quality file/search/edit/shell tools with permission audit trails.
- Context management that prevents long sessions from failing because the window filled up.
- A small extension surface for tools, commands, and event hooks after core safety is stable.

## Reference: PI Backend Capabilities

The PI backend reference lives under `docs/reference-code/pi-mono`. The most relevant files are:

| Area | PI Reference Paths | Useful Lessons |
| --- | --- | --- |
| Agent runtime | `packages/agent/src/agent.ts`, `packages/agent/src/agent-loop.ts`, `packages/agent/src/types.ts` | Evented runtime, lifecycle events, steering/follow-up queues, abort propagation, sequential/parallel tool execution. |
| Providers | `packages/ai/src/types.ts`, `packages/ai/src/api-registry.ts`, `packages/ai/src/providers/register-builtins.ts`, `packages/ai/src/env-api-keys.ts` | Provider registry, model capabilities, environment credential discovery, OAuth refresh, provider compatibility shims. |
| Sessions | `packages/coding-agent/src/core/session-manager.ts`, `packages/coding-agent/src/core/agent-session.ts`, `packages/coding-agent/src/core/agent-session-runtime.ts` | Append-only JSONL with tree entries, migrations, session fork/clone/switch, compaction summaries. |
| RPC | `packages/coding-agent/src/modes/rpc/rpc-mode.ts`, `packages/coding-agent/src/modes/rpc/rpc-types.ts`, `packages/coding-agent/src/modes/rpc/jsonl.ts`, `packages/coding-agent/docs/rpc.md` | JSONL protocol, bidirectional commands/events, model/session controls, prompt steering, extension UI bridge. |
| Tools | `packages/coding-agent/src/core/tools/*.ts` | Pluggable operations, read/write/edit/bash/grep/find/ls, streaming tool updates, output truncation, robust edit diffs. |
| Compaction | `packages/coding-agent/src/core/compaction/compaction.ts`, `packages/coding-agent/src/core/compaction/branch-summarization.ts` | Automatic/manual compaction, cut-point selection, file tracking, branch summaries. |
| Extensions | `packages/coding-agent/src/core/extensions/types.ts`, `packages/coding-agent/src/core/extensions/runner.ts`, `packages/coding-agent/docs/extensions.md` | Event hooks, custom tools, commands, providers, UI requests, message interception. |
| Tests | `packages/coding-agent/test/rpc.test.ts`, `packages/coding-agent/test/compaction.test.ts`, `packages/coding-agent/test/agent-session-*.test.ts`, `packages/agent/test/*.ts` | Backend behavior is covered by focused protocol, session, compaction, concurrency, and agent-loop tests. |

## Current AVA Baseline

AVA already has important backend pieces:

- CLI/runtime orchestration in `src/ava/app/`.
- OpenAI auth, model config, prompt config, and curl transport.
- OpenAI Responses/Codex provider path in `src/ava/provider/`.
- Sequential agent loop in `src/ava/agent/`.
- Built-in tools for read, write, edit, glob, grep, bash, apply_patch, and question.
- Build/plan permission policy in `src/ava/permissions/`.
- Append-only JSONL sessions, resume/list/export, and compaction entries in `src/ava/session/`.
- Project/global `AGENTS.md` context loading in `src/ava/context/`.
- Interactive TUI plus print and JSONL RPC headless modes.

The gap is not that AVA lacks a backend. The gap is that AVA's backend is still a single-provider, mostly-linear, synchronous local agent backend. PI's power comes from richer runtime events, deeper session semantics, provider breadth, compaction, extensibility, and stronger tool operations.

## 1.0 Gap Inventory

### Runtime And Events

AVA needs a more complete backend event model.

Missing or incomplete:

- True provider streaming to runtime consumers instead of parse-after-complete curl responses.
- A subscription-style event bus for TUI, RPC, tests, and future extensions.
- Steering and follow-up queues so users or clients can interrupt or queue work during a run.
- Clear turn/message/tool lifecycle events, not only final assistant/tool summaries.
- Thinking/reasoning lifecycle events for models that expose reasoning blocks or deltas.
- Consistent cancellation propagation through provider requests, tools, bash children, and event listeners.
- Parallel tool execution controls, if the tool and permission model can safely support it.

1.0 target:

- Every run emits structured lifecycle events.
- TUI and RPC consume the same event stream.
- Cancellation is observed promptly at provider, tool, and shell boundaries.
- Tool execution remains sequential by default, with explicit parallel eligibility later.

### Providers And Auth

AVA's provider layer is currently OpenAI-first and OpenAI-shaped.

Missing or incomplete:

- Automatic OAuth refresh.
- Multi-provider registry.
- Provider capability metadata, including tool-call support, context window, reasoning controls, streaming support, image input, and usage accounting support.
- Model catalog fields for cost, max output tokens, context windows, cache pricing, and provider compatibility quirks.
- Environment credential discovery beyond the current OpenAI-focused path.
- Retry, backoff, and rate-limit handling.
- Retry-after header parsing where providers expose it.
- Normalized provider errors for context overflow, auth failure, quota, invalid request, and transient transport failure.
- Usage and cost extraction from provider responses.

1.0 target:

- OpenAI remains excellent.
- At least one additional provider path is production-quality.
- Provider/model definitions declare capabilities that the runtime can enforce.
- OAuth credentials refresh before expiry.
- Provider failures are categorized and actionable.

### Sessions And Context

AVA has append-only session storage, but not PI-level session lifecycle management.

Missing or incomplete:

- Permission decision entries are never appended; the session entry type and export path exist, but no tool path writes them.
- Provider-generated manual compaction summaries.
- Automatic compaction trigger using the existing estimator and threshold logic.
- Context overflow detection and retry after compaction.
- Token, usage, and cost records per assistant response.
- Mid-session model and thinking-level changes with session entries.
- Session tree structure, branching, fork, clone, and branch summaries.
- Session migrations for future schema changes.
- Strong session stats for UI and headless clients.

1.0 target:

- Long sessions can continue without manual transcript pruning.
- Session files remain inspectable JSONL.
- Session entries are versioned and migration-ready.
- Branching is either implemented or the storage format is ready for it without another rewrite.

### Tools And Operations

AVA has a strong small tool set, but PI has more mature tool internals.

Missing or incomplete:

- Dedicated filesystem and process operation interfaces beneath tools.
- Atomic writes for normal file writes.
- Per-path file mutation queue for concurrent edits.
- Robust edit behavior around line endings, BOM, Unicode normalization, and fuzzy fallback.
- Unified diff output and safer patch preview flow.
- Search behavior closer to `rg`/`fd`, including `.gitignore` semantics and an explicit way to opt out.
- Streaming tool updates for long shell/search operations.
- Full-output spill files for truncated bash/search output.
- Process-tree cleanup on cancellation and timeout.
- Image/file attachment reads if AVA wants multimodal model support.
- Web fetch and LSP tools.
- Delete/move tools, only after audit and permissions are stronger.

1.0 target:

- Tools are safe, bounded, testable, and independently replaceable in tests.
- File writes are atomic where practical.
- Search is predictable for real repositories.
- Shell execution has strong timeout, cancellation, and output semantics.
- Tool results are useful to users, not only to the model.

### Permissions And Audit

AVA's permission model is a differentiator, but it needs persistence and broader UX semantics.

Missing or incomplete:

- Permission decision entries are never appended today; audit persistence must cover every allow, ask, deny, and resolved prompt.
- Persistent allow/deny rules.
- Session-wide grants.
- Deny reasons surfaced consistently to users and headless clients.
- Permission prompts over RPC.
- Audit trail that connects request, decision, actor, and executed operation.
- Policy categories for network fetch, delete/move, LSP, extension tools, and external directories.

1.0 target:

- Every ask/allow/deny decision is auditable.
- Interactive and headless modes share the same permission semantics.
- Persistent rules are explicit, inspectable, and revocable.
- New tool classes cannot bypass policy.

### Headless And Automation

AVA already has print mode and a JSONL RPC MVP. PI's protocol is broader and more interactive.

Missing or incomplete:

- Protocol versioning.
- Full event stream for message/tool deltas.
- `steer` and `follow_up` requests.
- Async cancellation tied to active runs.
- RPC permission and question resolvers.
- Extension UI bridge or an explicit narrower replacement for select/confirm/input/editor-style requests.
- Model selection, model cycling, available-model listing, and thinking/reasoning controls.
- Session lifecycle commands for new, switch, fork, clone, list, get messages, and get session stats.
- Direct backend commands for bash, compact, export, context, and state with stable schemas.
- Typed protocol tests that spawn AVA as a subprocess.

1.0 target:

- RPC is stable enough for editor integrations and test harnesses.
- Print mode stays simple and script-friendly.
- Server mode remains deferred until stdio RPC is proven.

### Extensibility

AVA should not jump straight into a large plugin platform, but PI shows where backend seams need to exist.

Missing or incomplete:

- Tool registry that can accept non-core tools safely.
- Event hooks around agent runs, provider requests, tool calls, and tool results.
- Custom slash/backend commands.
- Skill and prompt-template resources beyond current `AGENTS.md` loading.
- Extension-scoped permissions and audit labels.
- Subagent/task execution as isolated AVA worker processes.

1.0 target:

- Core modules expose narrow extension seams.
- Built-in tools use the same registry shape future tools will use.
- Subagents and MCP remain optional until the core extension and permission model can contain them.

## Roadmap Phases

### Phase 0: Roadmap Reconciliation

Purpose: make the planning base truthful before deeper work.

Scope:

- Update stale product/version docs that still describe print, RPC, AGENTS loading, manual compaction records, and export as deferred.
- Keep `docs/headless-protocol.md` as the current contract for the RPC MVP.
- Add an explicit 1.0 backend capability checklist to product planning.

Acceptance criteria:

- Docs agree on what is implemented, deferred, and required for 1.0.
- The backend roadmap is the source of truth for backend sequencing.

### Phase 1: Backend Hardening

Purpose: make the current backend reliable before expanding it.

Scope:

- Implement OpenAI OAuth refresh token exchange and wire automatic refresh before provider startup fails on expiry.
- Persist permission decision entries for every allow, ask, and deny resolution.
- Split or extend focused regression tests for permissions, file tools, search tools, bash, sessions, print mode, and RPC mode.
- Introduce safe filesystem/process operation boundaries under existing tools.
- Make `write_file` and `edit_file` atomic via temp-file plus rename; `apply_patch` already uses staged replacement.
- Improve bash cancellation and timeout cleanup.
- Add `git --no-pager diff --check` and sanitizer runs to release verification docs.

Acceptance criteria:

- Existing single-provider workflows are stable.
- Permission audit entries appear in session export.
- File writes do not leave obvious partial writes on normal failure paths.
- Headless tests cover malformed input, cancellation, permission denial, and successful prompt flow with a fake provider.

### Phase 2: Evented Runtime And Protocol

Purpose: turn the backend into a shared runtime for TUI and automation clients.

Scope:

- Define a runtime event taxonomy for session, turn, message, provider, tool, permission, compaction, and error events.
- Include agent, turn, message start/update/end, thinking update, tool start/update/end, permission, queue, compaction, retry, cancellation, and error event types in that taxonomy.
- Redesign the transport/provider boundary to support incremental stream event delivery instead of only returning a complete HTTP response.
- Route TUI, print JSON, and RPC through the same event stream.
- Add message delta events when provider streaming is available.
- Add async cancellation tied to active runs.
- Add RPC protocol versioning.
- Add RPC `steer`, `follow_up`, `get_messages`, `get_session_stats`, `new_session`, `switch_session`, `set_model`, `cycle_model`, and `get_available_models` commands.
- Add RPC question and permission resolver flows.

Acceptance criteria:

- A client can observe a full turn without reading session internals.
- A client can cancel an active provider/tool run and receive a terminal event.
- The event envelope can add future compaction, retry, provider, and extension events without breaking existing clients.
- TUI behavior does not depend on private agent-loop state.

### Phase 3: Context, Usage, And Sessions

Purpose: support long-running real projects.

Scope:

- Implement provider-generated compaction summaries.
- Wire `should_auto_compact` into the agent loop using token estimates and provider context windows.
- Detect context overflow and retry after compaction when safe.
- Store provider usage and cost metadata on assistant messages.
- Add session stats aggregation.
- Add session schema versioning and migration hooks.
- Design tree entries with `id` and `parent_id` compatibility, even if UI branching follows later.
- Implement fork/clone and branch summary if tree sessions are included in 1.0.

Acceptance criteria:

- Long sessions compact automatically before they fail from context pressure.
- `/compact` produces useful provider summaries, not empty placeholders.
- Session exports show usage, cost, compaction, and permission audit information.

### Phase 4: Tool Quality And Code Intelligence

Purpose: make AVA's tools feel dependable on real repositories.

Scope:

- Improve `edit_file` and `apply_patch` with diff previews, line-ending preservation, BOM handling, and clearer failure messages.
- Add per-path mutation serialization for writes/edits/patches.
- Respect `.gitignore` by default, prune ignored directories before traversal, and support an explicit `no_ignore` option for search tools.
- Add tool output spill files for truncated shell/search output.
- Add streaming tool progress events.
- Add `webfetch` behind network permission policy.
- Add LSP diagnostics, symbols, definitions, and references.
- Decide whether image reading belongs in core for multimodal support.

Acceptance criteria:

- Edits explain why they failed and show safe previews when they will change files.
- Search results match developer expectations in common repositories.
- Web and LSP tools are permissioned, bounded, and covered by tests.

### Phase 5: Provider And Model Breadth

Purpose: make AVA provider-flexible without weakening the core loop.

Scope:

- Introduce provider registry and model capability metadata.
- Add at least one non-OpenAI provider with streaming, tool calls, usage handling, and tests.
- Add model catalog fields for context window, max output, input modalities, cost, cache pricing, and provider compatibility quirks.
- Add environment credential discovery.
- Normalize provider errors, including context overflow, auth, quota, invalid request, refusal/content filter, and transient transport failures.
- Add retry/backoff/rate-limit handling, including `Retry-After` parsing where available.
- Add reasoning/thinking controls only where provider capabilities support them.

Acceptance criteria:

- Provider selection is not hard-coded in app modes.
- Model capabilities are checked before enabling tools, streaming, reasoning, or context thresholds.
- Usage and cost are calculated from provider usage data and model pricing metadata.
- Provider failures produce actionable user-facing errors.

### Phase 6: Controlled Extensibility

Purpose: open backend seams without creating an unsafe plugin platform too early.

Scope:

- Move built-in tools behind a registry interface.
- Add event hooks for provider request/response, tool call/result, session lifecycle, and command handling.
- Add custom backend slash commands through the registry.
- Add prompt templates and structured skills under global/project config paths.
- Add extension-scoped permission categories.
- Design the permission and process boundaries that a future task/subagent worker would need, without shipping built-in orchestration in this phase.

Acceptance criteria:

- Built-in tools and extension tools share validation, permission, event, and audit paths.
- Extensions cannot mutate files, run commands, or fetch network resources without policy coverage.
- Future subagent/task workers have documented session, permission, and cancellation constraints before implementation starts.

## 1.0 Cut Line

Required for 1.0:

- OAuth refresh.
- Permission decision persistence and session audit export.
- Stable event stream shared by TUI, print JSON, and RPC.
- Async cancellation for provider and tool runs.
- Provider-generated manual and automatic compaction.
- Usage/cost/context accounting.
- Hardened file/search/bash tools with focused tests.
- Stable RPC protocol version with prompt, cancel, state, sessions, messages, compact, export, permission, and question flows.
- Provider registry with OpenAI plus one additional high-quality provider, backed by model capability and pricing metadata.
- Mid-session model switching, model-change session entries, and session stats exposed over RPC.
- Thinking/reasoning controls for providers and models that support them.

Strongly desired for 1.0:

- Session tree storage and fork/clone.
- LSP tools.
- Web fetch.
- Diff previews for edits and patches.
- Search behavior matching `.gitignore` expectations.
- Prompt templates and structured skills.

Defer unless earlier phases finish cleanly:

- HTTP/server daemon mode.
- MCP.
- Broad third-party plugin runtime.
- Package manager for extensions.
- Full theme/UI extension system.
- Built-in multi-agent orchestration, including `task` workers and subagents.

## Immediate Next Work

The next implementation planning slice should be Phase 1.

Recommended first tickets:

1. Persist permission decisions whenever a tool permission is resolved.
2. Add OpenAI OAuth refresh before provider startup fails on expired credentials.
3. Add focused headless RPC tests with fake provider credentials/transport.
4. Introduce a small safe filesystem operation layer used by `read_file`, `write_file`, `edit_file`, and `apply_patch`.
5. Make `write_file` and `edit_file` atomic and add regression coverage for write failure behavior.
6. Update stale product docs to align with the current backend baseline.
