# AVA Backend Roadmap

This roadmap defines the backend work needed for AVA 1.0 to feel capable enough for serious daily coding work while keeping AVA's product constraints: native C++23, one binary, terminal first, explicit safety boundaries, and inspectable local files.

The companion frontend/TUI sequencing lives in `docs/roadmap/frontend-tui.md`.
The measurable backend maturity acceptance criteria and staged hardening gates live in `docs/roadmap/backend-maturity-baseline.md`.

External systems are behavior references for backend power, not architectures to copy directly. AVA should borrow the capability shape, then implement it with narrow C++ modules and clear permission boundaries.

## 1.0 Backend Goal

AVA 1.0 should be a reliable local coding-agent backend that supports:

- Long-running interactive and headless sessions.
- Strong streaming agent events for TUI and automation clients.
- Durable sessions with compaction, usage records, export, and branching-ready structure.
- Provider abstraction with at least OpenAI plus one additional production-quality provider path.
- Provider/model metadata for context windows, cost, tool support, streaming, and reasoning controls.
- Safe, high-quality file/search/edit/shell tools with permission audit trails.
- Context management that prevents long sessions from failing because the window filled up.
- A stable local plugin foundation for tools, commands, prompt resources, event hooks, and MCP servers after core safety is stable.

## Approved Backend MVP Cut

The 1.0 backend MVP was approved on 2026-05-03 after comparing AVA against external reference behavior. The MVP cut keeps AVA focused on a safe local coding-agent backend rather than full product parity with any reference system.

Required for the MVP, including both implemented foundations and remaining Phase 5.5/6 work:

- Provider-native OpenAI, Anthropic, Kimi/Moonshot, and OpenAI-compatible provider paths with fake-provider contract tests and opt-in live smokes. OpenAI is implemented, Anthropic is native and offline-hardened, and Kimi/Moonshot/OpenRouter-compatible shims have deterministic contract coverage; live credentialed smokes remain release-validation work.
- Reasoning/thinking storage, runtime events, RPC controls, and frontend-visible deltas for providers that expose reasoning.
- Safe mid-session model switching that preserves compatible history and rejects incompatible switches before sending invalid provider requests.
- Hardened built-in tools, tool cancellation where safe, and a registry foundation shared by built-in, plugin, and MCP tools.
- Out-of-process plugins with manifest validation, explicit enablement, diagnostics, permission/audit identity, and fake plugin tests.
- MCP stdio host support for tool discovery/calls, diagnostics, permission/audit identity, and fake MCP server tests.
- Docs, samples, protocol notes, and verification commands that prove the backend is ready.

Explicitly post-MVP but still on the product roadmap:

- 1.1 candidates: HTTP/server daemon mode, persistent permission rules, session tree UI with fork/clone/branch summaries, full LSP symbols/definitions/references, multimodal/image support, richer MCP resources and prompt UX if they do not fit the 1.0 slice, and broader live-provider smoke automation.
- 1.2+ candidates: plugin marketplace/package manager/remote install, extension UI bridge, advanced MCP HTTP/OAuth/subscriptions/sampling/elicitation/pagination, parallel tool execution, and OS-level plugin sandboxing.
- Later research: in-process native plugin ABI only if AVA accepts the crash/memory/C++ ABI support burden, and multi-agent/subagent orchestration only after plugin/process/session boundaries are stable.

Reference-code rule: material under `docs/reference-code/` is for behavior comparison only. Its source code and architecture must not be copied into AVA.

Terminology note: this roadmap uses "1.0" and "backend MVP" for the same release cut. "Post-MVP" means 1.1 and later.

## Reference: Backend Maturity Capabilities

The local reference material lives under `docs/reference-code/`. The most relevant capability areas are:

| Area | Useful Lessons |
| --- | --- |
| Agent runtime | Evented runtime, lifecycle events, steering/follow-up queues, abort propagation, sequential/parallel tool execution. |
| Providers | Provider registry, model capabilities, environment credential discovery, OAuth refresh, provider compatibility shims. |
| Sessions | Append-only JSONL with tree entries, migrations, session fork/clone/switch, compaction summaries. |
| RPC | JSONL protocol, bidirectional commands/events, model/session controls, prompt steering, extension UI bridge. |
| Tools | Pluggable operations, read/write/edit/bash/grep/find/ls, streaming tool updates, output truncation, robust edit diffs. |
| Compaction | Automatic/manual compaction, cut-point selection, file tracking, branch summaries. |
| Extensions | Event hooks, custom tools, commands, providers, UI requests, message interception. |
| Tests | Backend behavior is covered by focused protocol, session, compaction, concurrency, and agent-loop tests. |

## Current AVA Baseline

AVA already has important backend pieces:

- CLI/runtime orchestration in `src/ava/app/`.
- OpenAI auth, model config, prompt config, and curl transport.
- OpenAI Responses provider path in `src/ava/provider/`.
- Sequential agent loop in `src/ava/agent/`.
- Built-in tools for read, write, edit, glob, grep, bash, apply_patch, question, webfetch, and capability-gated LSP diagnostics.
- Build/plan permission policy in `src/ava/permissions/`.
- Append-only JSONL sessions, resume/list/export, and compaction entries in `src/ava/session/`.
- Project/global `AGENTS.md` context loading in `src/ava/context/`.
- Interactive TUI plus print and JSONL RPC headless modes.

The gap is not that AVA lacks a backend. The gap is that AVA's backend is still a single-provider, mostly-linear, synchronous local agent backend. Mature coding-agent backends rely on richer runtime events, deeper session semantics, provider breadth, compaction, extensibility, and stronger tool operations.

## Milestone Audit Summary

Explorer agents checked each roadmap phase against the current AVA code and external reference lessons on 2026-04-30. A Phase 1 closure audit on the same date verified the normal build, sanitizer build, whitespace check, and headless CLI basics.

The 0.33 backend maturity ledger later grew large enough that the version docs moved through the 0.60 platform catch-up, 0.65 provider-hardening line, bundled 0.70 reasoning/model closeout, and 0.75 extension foundation. Treat older "immediate next" wording as historical unless it is restated below; active work is now 0.80 extension API stabilization while live provider smoke evidence and final 1.0 release validation remain incomplete.

- Phase 0 documentation reconciliation is closed for the known under-reported backend features: current docs now surface print/RPC mode, `AGENTS.md` loading, manual compaction entries, export, OAuth refresh, permission audit persistence, and atomic writes, while older version plans with superseded deferrals are marked historical.
- Phase 1 is implemented and verified for the current single-provider backend. Keep edge-case coverage aligned as Phase 2 exposes new event/protocol boundaries.
- Phase 2 is implemented and verified for the approved evented-runtime scope: versioned event envelopes, shared event bus routing for headless modes, incremental provider streaming, reasoning lifecycle events, RPC protocol versioning/session commands, active-run cancellation, permission/question resolver replies, and bounded `steer`/`follow_up` queues.
- Phase 3 core context/session work is implemented for the approved scope: provider-generated `/compact`, automatic compaction, one bounded context-overflow retry, provider usage/cost records, RPC stats, model/reasoning entries, additive session version checks, and branching-ready `id`/`parent_id` validation. Full fork/clone/tree UI and branch summaries remain deferred to later phases.
- Phase 4's core tool-quality slice is implemented and verified: centralized built-in tool metadata, `.gitignore`-aware search with a documented native subset, bounded spill files, streaming tool progress, edit/apply_patch diff previews with CRLF/BOM diagnostics, per-path mutation serialization, `webfetch` behind `network.fetch`, and an LSP diagnostics first slice. Final validation included live headless smokes for read/search/webfetch plus RPC permission/question reply smokes for write/edit/apply_patch/bash/question; LSP diagnostics are covered by fake-server tests because the tool is capability-gated. Remaining tool-quality expansion includes broader fuzzy/Unicode matching, image/multimodal reads, LSP symbols/definitions/references, and Phase 6-grade registry/artifact ownership.
- Phase 5 is implemented and verified for its foundation scope: provider registry, model capability catalog, provider-neutral credential discovery, retry/error normalization, Anthropic Messages first slice, model-change session entries, and RPC model listing/switching. This does not mean Anthropic is production-quality for the 1.0 cut yet; production-quality non-OpenAI support depends on the remaining Phase 5.5 hardening.
- Phase 5.5 has moved past the first provider-native slice: provider-neutral content parts, Anthropic native `tool_use`/`tool_result`, thinking/signature/redacted-thinking, cache usage, stop reasons, enabled-only Anthropic model metadata, Kimi/Moonshot-compatible reasoning coverage, OpenRouter-compatible request/error coverage with built-in reasoning controls disabled, provider-switch compatibility checks, and retry/idempotency documentation are implemented or in the 0.65 validation path. Remaining Phase 5.5 risk is primarily live credentialed endpoint smoke coverage, broader provider compatibility drift, OpenRouter-native reasoning support, and richer pricing/cache/strict-schema metadata before final 1.0 release readiness.
- Phase 6 has a source-backed safe local plugin/MCP foundation: subprocess containment, command exposure, MCP stdio hosting, MCP prompt discovery/invocation, plugin diagnostics, plugin prompt/skill resources and commands, unified command-registry seams, AI-friendly authoring docs, a checked-in sample plugin, real-sample headless RPC smoke coverage, compatibility policy, minimal golden contracts, focused audit rechecks, selected failure coverage, explicit MCP resource deferral, and OpenAI 5.5 manual headless release-validation coverage are implemented and tested. Final diagnostics polish and any review-driven failure-matrix closure remain before 0.90; non-OpenAI live provider smoke evidence remains credential-dependent release validation.

## 1.0 Gap Inventory

### Runtime And Events

AVA now has the Phase 2 backend event model needed for headless clients and future UI integration.

Missing or incomplete:

- Full live TUI consumption of the shared event stream; the TUI still uses blocking runtime glue and can replay buffered events.
- Broader reasoning UI polish beyond the provider-neutral reasoning lifecycle events already emitted for supported models.
- Deeper cancellation interruption inside buffered/non-streaming provider calls and individual long-running tools beyond the current cooperative boundaries.
- Parallel tool execution controls, if the tool and permission model can safely support it.

1.0 target:

- Every run emits structured lifecycle events.
- TUI and RPC consume the same event stream.
- Cancellation is observed promptly at tool and shell boundaries and at safe provider boundaries. Direct interruption of in-flight provider transport calls remains a hardening item unless the transport boundary can support it safely.
- Tool execution remains sequential by default, with explicit parallel eligibility later.

### Providers And Auth

AVA's provider layer now has a registry-backed OpenAI path, an Anthropic Messages path with native `tool_use`/`tool_result` and reasoning replay, Kimi/Moonshot-compatible shims with deterministic reasoning-content coverage, and an OpenRouter-compatible shim with deterministic request/error coverage but no built-in reasoning controls yet. OpenAI OAuth credentials refresh before use when a refresh token is available. External behavior references remain useful for live smoke coverage, model metadata, Kimi/Moonshot quirks, OpenRouter-native reasoning, and broader provider compatibility drift; AVA should match the relevant behavior without copying implementation architecture.

Missing or incomplete:

- Live credentialed provider smokes for Anthropic streaming/non-streaming paths and Kimi/Moonshot/OpenRouter-compatible paths.
- Broader provider compatibility drift beyond the deterministic fake contracts currently covering Anthropic, Kimi, Moonshot, and OpenRouter-style routes, including OpenRouter-native reasoning fields.
- Kimi/Moonshot coding behavior beyond the implemented compatible route, such as deeper reference-informed coding prompt/profile work and live endpoint validation.
- Provider-specific auth discovery beyond simple API-key/OAuth-token sources, such as Google ADC, AWS Bedrock credentials, or provider-specific CLI tokens.
- Provider-specific idempotency keys where providers support request ids or idempotency keys; current behavior is documented as best-effort without deduplication guarantees.

1.0 target:

- OpenAI remains excellent.
- At least one additional provider path reaches production-quality before 1.0 ships; Anthropic is the current partial candidate and mainly needs live credentialed smoke evidence and final provider-doc recheck before being counted as production-ready.
- Provider/model definitions declare capabilities that the runtime can enforce.
- OAuth credentials refresh before expiry.
- Provider failures are categorized and actionable.
- Provider-native protocols preserve tool/result/thinking semantics instead of relying on text-only transcript replay.
- Reasoning deltas are emitted in a shape that a frontend can display without parsing provider-specific raw events.

### Sessions And Context

AVA has append-only session storage, persisted permission audit entries, provider-generated compaction, automatic compaction, bounded context-overflow retry, usage/cost records, and RPC stats. It does not yet have mature tree navigation or fork/clone UI.

Missing or incomplete:

- Mid-session model and thinking-level changes with session entries.
- Durable reasoning/thinking storage that follows the strongest useful external lessons while preserving AVA's inspectable JSONL sessions.
- Session tree structure, branching, fork, clone, and branch summaries.
- Full rewrite-style migrations for future schema changes beyond the current additive version checks and actionable future-version rejection.
- A backend-provided live token/context usage summary for the TUI composer status slot. The frontend can reserve space for it in Phase 1, but accurate counts need backend session/provider usage data and context-window metadata.

1.0 target:

- Long sessions can continue without manual transcript pruning.
- Session files remain inspectable JSONL.
- Session entries are versioned and migration-ready.
- Branching is either implemented or the storage format is ready for it without another rewrite.

### Tools And Operations

AVA has a strong small tool set with permissioned atomic file writes where practical, cooperative cancellation for read/write/edit file operations, tested shell process-group cleanup on timeout, Phase 4 search/edit/web/LSP improvements, and remaining maturity work around deeper abstractions and expanded code intelligence.

Missing or incomplete:

- Dedicated filesystem and process operation interfaces beneath tools.
- Broader Unicode normalization awareness and fuzzy edit fallback beyond current exact matching with CRLF/BOM diagnostics.
- UI-mediated patch preview/approval flow beyond provider-visible bounded unified diffs.
- Search parity for unsupported `.gitignore` edge syntax such as bracket character classes; current native matcher documents its subset.
- Broader process-tree cleanup proofs beyond the current bash timeout, plugin, MCP, and LSP process-group coverage.
- Image/file attachment reads if AVA wants multimodal model support.
- LSP diagnostics now share the active run cancellation callback for startup and request waits; symbols, definitions, references, document sync, and configured production server discovery remain beyond the current diagnostics first slice.
- Delete/move tools, only after audit and permissions are stronger.

1.0 target:

- Tools are safe, bounded, testable, and independently replaceable in tests.
- File writes are atomic where practical.
- Search is predictable for real repositories.
- Shell execution has strong timeout, cancellation, and output semantics.
- Tool results are useful to users, not only to the model.

### Permissions And Audit

AVA's permission model is a differentiator. Tool permission decisions are persisted as session audit entries, and
headless RPC has an initial exact-match session grant path for repeated prompts with inspect, revoke, and clear
commands. Persistent rules and broader UX semantics remain deferred.

Missing or incomplete:

- Persistent allow/deny rules.
- Broader session-wide grants and durable grant storage beyond the initial headless exact-match path.
- Deny reasons surfaced consistently to users and headless clients.
- Richer permission-prompt UX beyond the current TUI and RPC resolver flows.
- Richer audit views that connect request, decision, actor, and executed operation.
- Policy categories for delete/move, plugin tools, and external directories. `network.fetch` and `lsp.query` now exist for the Phase 4 web/LSP tools.

1.0 target:

- Every ask/allow/deny decision is auditable.
- Interactive and headless modes share the same permission semantics.
- Persistent rules are explicit, inspectable, and revocable.
- New tool classes cannot bypass policy.

### Headless And Automation

AVA has print mode and JSONL RPC protocol version 1. Print/RPC now share the event envelope for provider streaming, tool lifecycle/progress, permission requests, question requests, cancellation, queue lifecycle, retry countdown ticks, assistant messages, and terminal outcomes. Live headless smokes verified model-visible tool calls for read/search/webfetch in print mode and mutating/bash/question tools through RPC resolver replies.

Missing or incomplete:

- Extension UI bridge or an explicit narrower replacement for select/confirm/input/editor-style requests.
- Model selection, model cycling, available-model listing, and thinking/reasoning controls.
- Fork/clone branch session commands beyond the current session lifecycle and stats APIs.
- Direct backend command for bash and richer stable command schemas beyond the current compact/export/context/state commands.
- Broader subprocess-level protocol tests and live smoke automation that can exercise all resolver paths without ad hoc harness code.

1.0 target:

- RPC is stable enough for editor integrations and test harnesses.
- Print mode stays simple and script-friendly.
- Server mode remains deferred until stdio RPC is proven.

### Extensibility And Plugin System

AVA should not jump straight into a marketplace-scale platform, but a serious 1.0 backend should include a small, stable plugin foundation. Users should be able to create a plugin with docs, examples, or AI assistance without writing C++, and a bad plugin should fail as a contained plugin failure instead of crashing or corrupting AVA's core state. The plugin/MCP contract is expanded in `docs/plugin-system.md`.

The safest 1.0 shape is an out-of-process plugin protocol: AVA launches a plugin executable, performs a versioned handshake, exchanges bounded JSONL messages, and routes plugin contributions through the same validation, permission, event, audit, cancellation, and session paths used by built-in features. AVA should avoid in-process native shared-library plugins for 1.0 because they can crash the process, corrupt memory, and create a C++ ABI support burden.

Implemented foundation:

- The registry and command seams accept built-in, plugin, project/global prompt, skill, and MCP prompt contributions behind bounded validation and permission paths.
- Plugin manifests, discovery, explicit enablement, versioned handshake, out-of-process runner lifecycle, startup/request timeouts, cancellation, stderr/stdout bounds, malformed-record handling, plugin command/tool/static-resource/event paths, and diagnostics are implemented and tested for the current foundation scope.
- AI-friendly authoring docs and a checked-in `examples/plugins/todo/` sample now exist, and focused tests load and run the actual sample files.
- MCP stdio startup, initialize, tool discovery/calls, prompt discovery/get, command-registry prompt invocation, diagnostics, and fake-server coverage share bounded AVA paths.

Missing or incomplete:

- Freeze the v1 plugin/MCP compatibility policy, deprecation path, supported contribution types, and schema-change rules.
- Add golden contract tests for command discovery/invocation schemas, plugin diagnostics, MCP registry output, and protocol compatibility.
- Complete the broader plugin/MCP failure matrix and audit recheck, including enough evidence for crash, timeout, cancellation, malformed output, invalid results, restart/disable, permission denial, and last-failure diagnostics.
- Decide and document whether MCP resources remain deferred for 1.0 or land behind explicit read-style permissioned commands.
- Add the core service proxy for plugin-requested file/search/shell/network operations if plugin side effects must go through AVA policy rather than ad hoc plugin behavior.
- Document future task/subagent worker process boundaries without shipping built-in orchestration in this phase.

1.0 target:

- Core modules expose narrow extension seams.
- Built-in tools and plugin tools use the same registry, validation, permission, event, audit, and cancellation shape.
- A simple plugin can add a tool, slash command, prompt template, or non-mutating event hook without requiring C++ changes.
- Plugin process crashes, hangs, malformed JSONL, unsupported API versions, or invalid results produce contained plugin errors and do not kill AVA or corrupt session files.
- Plugin-contributed operations cannot bypass AVA permissions when they request file, shell, network, external-directory, or session access through AVA.
- MCP servers are launched or connected through explicit permissioned configuration, MCP tools and prompts are exposed through bounded AVA registry paths, and MCP resources are explicitly deferred or implemented only through read-style permissioned paths.
- The v1 plugin API has an explicit compatibility policy, deprecation path, and contract tests.
- Subagents remain optional until the core extension and permission model can contain worker processes; MCP stays bounded to the explicit 1.0 host scope in `docs/plugin-system.md`.
- Full untrusted-code sandboxing remains a separate hardening layer; arbitrary plugin executables must be treated as local code the user chose to run unless AVA later adds OS-level sandboxing.

## Roadmap Phases

### Phase 0: Roadmap Reconciliation

Purpose: make the planning base truthful before deeper work.

Scope:

- Update stale product/version docs that still describe print, RPC, AGENTS loading, manual compaction records, and export as deferred.
- Update stale docs that still describe OAuth refresh, permission audit persistence, and atomic file writes as future work.
- Keep `docs/headless-protocol.md` as the current contract for the RPC MVP.
- Add an explicit 1.0 backend capability checklist to product planning.
- Make version docs clearly historical when they describe old deferred status.
- Cross-check `README.md`, `docs/CONFIG.md`, `docs/USAGE.md`, `docs/TESTING.md`, `docs/headless-protocol.md`, and `docs/product/*.md` against current code before starting Phase 2.

Acceptance criteria:

- Docs agree on what is implemented, deferred, and required for 1.0.
- The backend roadmap is the source of truth for backend sequencing.
- `docs/product/backend-capabilities-1.0.md` exists and maps each 1.0 backend capability to current status and roadmap phase.
- Documented commands and protocol examples match app command/RPC handlers or are explicitly marked planned.
- `git --no-pager diff --check` passes after documentation reconciliation.

### Phase 1: Backend Hardening

Purpose: make the current backend reliable before expanding it.

Scope:

- Keep OpenAI OAuth refresh token exchange covered by tests and wired before provider startup fails on expiry.
- Keep permission decision entries persisted for every allow, ask, and deny resolution.
- Split or extend focused regression tests for permissions, file tools, search tools, bash, sessions, print mode, and RPC mode.
- Keep safe filesystem/process operation boundaries under existing tools and document any remaining seams that Phase 4 must extract.
- Keep `write_file` and `edit_file` atomic via temp-file plus rename; `apply_patch` already uses staged replacement.
- Keep bash cancellation and timeout cleanup covered by regression tests.
- Add `git --no-pager diff --check` and sanitizer runs to release verification docs.
- Verify permission audit export includes operation, path/command, decision, resolver source, and denial reason where applicable.
- Add or keep headless coverage for malformed input, cancellation, permission denial, and successful fake-provider prompt flow.

Acceptance criteria:

- Existing single-provider workflows are stable.
- Permission audit entries appear in session export.
- File writes do not leave obvious partial writes on normal failure paths.
- Headless tests cover malformed input, cancellation, permission denial, and successful prompt flow with a fake provider.
- Release verification docs include normal, sanitizer, and whitespace-check commands from `AGENTS.md`.

### Phase 2: Evented Runtime And Protocol

Status: implemented and verified for the approved Phase 2 scope on 2026-05-01. Deferred items that depend on a broader provider registry or richer model capability metadata are tracked under Phase 5.

Purpose: turn the backend into a shared runtime for TUI and automation clients.

Scope:

- Define a runtime event taxonomy for session, run, turn, message, provider, tool, permission, queue, compaction, retry, cancellation, and error events.
- Define a stable event envelope with schema version, event type, event id, session id, run id, turn id where applicable, timestamp, correlation ids, payload, and error fields.
- Include agent, turn, message start/update/end, thinking update, tool start/update/end, permission, queue, compaction, retry, retry countdown, cancellation, and error event types in that taxonomy.
- Add a subscription-style event bus that can feed TUI, print JSON, RPC, tests, and future plugins without each mode reading private loop state.
- Redesign the transport/provider boundary to support incremental stream event delivery instead of only returning a complete HTTP response.
- Route TUI, print JSON, and RPC through the same event stream.
- Add message delta events when provider streaming is available.
- Add thinking/reasoning delta events when provider capabilities expose them.
- Add async cancellation tied to active provider/tool runs, including a terminal cancellation event.
- Add RPC protocol versioning and a compatibility rule for unknown request/event fields.
- Add RPC `steer`, `follow_up`, `get_messages`, `get_session_stats`, `new_session`, and `switch_session` commands. Model selection commands such as `set_model`, `cycle_model`, and `get_available_models` are deferred to the Phase 5 provider/model catalog work.
- Add RPC question and permission resolver flows.
- Add subprocess-level RPC tests that feed JSONL requests and assert response/event order with fake provider transport.

Acceptance criteria:

- A client can observe a full turn without reading session internals.
- Message deltas, tool start/update/end, permission prompts, and final terminal events are visible over RPC.
- A client can cancel an active provider/tool run and receive a terminal event.
- The event envelope can add future compaction, retry, provider, and extension events without breaking existing clients.
- TUI behavior does not depend on private agent-loop state.
- RPC tests cover malformed JSONL, unknown commands, prompt success, permission/question resolver flows, cancellation, and session commands.

### Phase 3: Context, Usage, And Sessions

Purpose: support long-running real projects.

Status: implemented and verified for the approved core scope on 2026-05-01. Full fork/clone/tree UI, branch summaries, and mid-session model/thinking entries are deferred to later phases.

Scope:

- Implemented provider-generated compaction summaries for `/compact` and automatic compaction.
- Implemented a structured compaction prompt that captures goal, constraints/preferences, decisions, files read/modified, unresolved tasks, and next steps.
- Wired automatic compaction into the agent loop using token estimates, configured model context windows, and a safe fallback threshold.
- Implemented one bounded context-overflow retry after compaction.
- Stored provider usage metadata and conservative cost metadata on assistant messages.
- Extracted OpenAI provider usage data when available and stored byte-estimate fallback metadata separately when it is not.
- Added session stats aggregation for RPC/UI consumers without requiring clients to parse JSONL.
- Added additive session version checks, future-version rejection, and branching-ready `id`/`parent_id` validation.
- Deferred full fork/clone/tree UI, branch summaries, and mid-session model/thinking entries.
- Added RPC/session APIs for messages, stats, provider-backed compaction, export, and context inspection. Branch/tree APIs remain deferred until tree UI work starts.

Acceptance criteria:

- Long sessions compact automatically before they fail from context pressure.
- `/compact` produces useful provider summaries, not empty placeholders.
- Session exports show usage, cost, compaction, and permission audit information.
- Session stats are available to TUI and RPC without reparsing JSONL in clients.
- Context-overflow retry has a max retry count and cannot loop forever.
- Session files remain inspectable JSONL and old entries can be migrated or rejected with actionable errors.

### Phase 4: Tool Quality And Code Intelligence

Purpose: make AVA's tools feel dependable on real repositories.

Scope:

- Improve `edit_file` and `apply_patch` with unified diff previews, line-ending preservation, BOM handling, Unicode-normalization awareness, fuzzy fallback, and clearer failure messages.
- Add multi-edit support to `edit_file` if it remains distinct from `apply_patch`, with overlap detection and per-edit diagnostics.
- Add per-path mutation serialization for writes/edits/patches.
- Respect `.gitignore` by default, prune ignored directories before traversal, and support an explicit `no_ignore` option for search tools.
- Decide whether search is backed by `rg`, libgit2 ignore parsing, or a small internal ignore matcher, then document the portability tradeoff.
- Add tool output spill files for truncated shell/search output.
- Add streaming tool progress events.
- Add `webfetch` behind network permission policy with URL validation, content-type handling, size limits, timeout, and redirect policy.
- Add an LSP lifecycle manager, starting with diagnostics and then symbols, definitions, and references.
- Decide whether image reading belongs in core for multimodal support.
- Add tool registry metadata for name, description, input schema, output bounds, permission category, execution mode, and event rendering hints.

Acceptance criteria:

- Edits explain why they failed and show safe previews when they will change files.
- Search results match developer expectations in common repositories.
- Web and LSP tools are permissioned, bounded, and covered by tests.
- Truncated shell/search output points to bounded spill files when full output is useful.
- Concurrent mutations to the same path are serialized or rejected deterministically.
- Tool metadata is centralized enough for Phase 6 plugin and MCP tools to reuse the same registry shape.

### Phase 5: Provider And Model Breadth

Purpose: make AVA provider-flexible without weakening the core loop.

Scope:

- Introduce a provider registry with provider factories and no hard-coded provider selection in app modes.
- Expand model capability metadata to include API family, context window, max output tokens, input modalities, tool support, streaming support, reasoning support, usage support, pricing, cache pricing, and compatibility quirks.
- Add at least one non-OpenAI provider with streaming, tool calls, usage handling, and tests.
- Add model catalog fields for context window, max output, input modalities, cost, cache pricing, and provider compatibility quirks.
- Add environment credential discovery beyond OpenAI, with a documented resolution order for CLI/config/auth-file/env sources.
- Refactor OpenAI-shaped auth state only as far as needed for a second provider; do not broaden credential storage without tests.
- Normalize provider errors, including context overflow, auth, quota, invalid request, refusal/content filter, and transient transport failures.
- Add retry/backoff/rate-limit handling, including `Retry-After` parsing where available.
- Add reasoning/thinking controls only where provider capabilities support them.
- Add model-change and reasoning-control validation before a run starts and when RPC changes the active model.

Acceptance criteria:

- Provider selection is not hard-coded in app modes.
- Model capabilities are checked before enabling tools, streaming, reasoning, or context thresholds.
- Usage and cost are calculated from provider usage data and model pricing metadata.
- Provider failures produce actionable user-facing errors.
- Tests can register a fake provider through the registry without using OpenAI-specific code paths.
- A non-tool-capable model does not receive tool definitions.
- Auth discovery and refresh failures preserve existing credentials and produce safe, actionable errors.

### Phase 5.5: Provider-Native Hardening And Breadth

Status: 0.65 provider-native hardening is implemented for offline/fake coverage: provider-neutral `ContentPart` replay, Anthropic native `tool_use`/`tool_result`, thinking/signature/redacted-thinking, cache usage, stop reasons, enabled-only Anthropic reasoning metadata, Kimi/Moonshot/OpenRouter-compatible shim contracts, provider-switch compatibility checks, and documented best-effort retry/idempotency behavior. Live credentialed smokes remain deferred release evidence.

Purpose: turn Phase 5's provider-flexible foundation into provider-native behavior that is reliable across real Anthropic/OpenAI-compatible providers before plugin and MCP seams depend on it.

Scope:

- Extend the provider-native message/content contract beyond the completed text/tool-call/tool-result slice to represent images/files when supported, provider reasoning/thinking blocks, provider-specific stop reasons, and cache-control hints without flattening everything into plain text.
- Preserve existing session JSONL readability while adding enough structured replay metadata for providers that require native `tool_use`/`tool_result` or thinking-signature continuity. Native Anthropic replay and compatible `reasoning_content` replay are implemented for the current scope.
- Complete remaining Anthropic Messages release evidence after the offline-hardening pass: real endpoint smokes for streaming and non-streaming paths, plus ongoing public-doc rechecks for thinking/cache semantics.
- Keep Anthropic-compatible endpoint support explicit: configurable base URL, API-key auth, no credential leakage across redirects, tolerant but loud SSE parsing, and tests that cover common compatible-provider drift.
- Add at least two more provider families or compatibility shims after Anthropic is native. Preferred order: OpenAI-compatible endpoints such as OpenRouter/DeepSeek/xAI/Groq/Mistral where the Responses or Chat Completions shape is close enough; Google Gemini next if its message/tool/thinking shape can be tested without SDK sprawl; Bedrock/Vertex only after credential-chain and event-stream risks are planned.
- Add and maintain explicit Kimi/Moonshot coding capability profiles: Kimi-specific coding prompt selection, Kimi K2/K2.5/K2-thinking model aliases across Moonshot and compatible gateways, temperature/top-p defaults, endpoint-family thinking quirks, `reasoning_content` handling, and Moonshot/Kimi context-overflow patterns.
- Before implementation, re-check current external reference behavior and current public Kimi/Moonshot docs so AVA chooses the smallest correct provider path instead of guessing between OpenAI-compatible and Anthropic-compatible routes.
- Add provider-specific request transforms behind provider implementations, not in the agent loop: schema sanitization, tool-name quirks, strict JSON schema variants, stop-sequence fields, reasoning/thinking controls, max-output fields, and modality limits.
- Add provider-specific auth discovery only with safe documented precedence and tests. Environment variables are sufficient for first support, but stored auth files, OAuth refresh, ADC, AWS, and CLI-token discovery must be explicit per provider.
- Add provider-native fake transports/harnesses that exercise a complete tool loop for each non-OpenAI provider: request with tools, provider tool call, AVA tool result, follow-up request with native result block, and final answer.
- Harden retry/idempotency policy for provider POSTs. Current 0.65 behavior is documented as best-effort without provider-specific idempotency keys; add keys later where a provider supports deduplication.
- Update model catalog metadata with provider-native compatibility flags such as `api_family`, strict schema requirements, tool-result support, reasoning parameter names, thinking signature requirements, cache-control support, max image/file constraints, and known endpoint quirks.

Acceptance criteria:

- Anthropic can complete a multi-turn tool workflow using native `tool_use`/`tool_result` blocks, not text-only summaries.
- Anthropic fake-transport/agent-loop tests cover streaming and non-streaming native workflows; real CLI/transport endpoint smokes remain deferred release evidence when credentials are unavailable.
- Switching from a tool-capable provider to a non-tool provider either prunes/translates tool history safely or produces a clear warning/error before an invalid request is sent.
- Reasoning/thinking controls are only exposed when the selected model declares support and the provider request builder actually serializes the corresponding native fields.
- Kimi/Moonshot coding models have cataloged capabilities and endpoint quirks for OpenAI-compatible and Anthropic-compatible routes, including tests for request parameters, reasoning/thinking output, overflow classification, and the Kimi-specific coding prompt/profile.
- At least two additional provider families or shims are registered, documented, and covered by fake-provider contract tests for request shape, streaming parse, usage parse, auth discovery, and normalized errors.
- Provider-specific compatibility code stays inside provider modules or narrow transform helpers; the agent loop remains provider-neutral.
- Retry behavior is documented per provider as idempotent, best-effort, or disabled for unsafe request classes.
- The headless RPC model catalog shows enough capability and compatibility metadata for clients to avoid offering unsupported tools, modalities, or reasoning controls.

### Phase 6: Stable Plugin Foundation

Status: required for the 1.0 backend MVP, sequenced after provider-native and tool-registry foundations so external contributions cannot depend on unstable contracts.

Purpose: open backend seams as a small, safe plugin system without turning AVA into a marketplace or letting plugins destabilize the core.

Scope:

- Move built-in tools behind a registry interface.
- Treat the registry and event bus as prerequisites; do not implement external plugin execution before built-in tools and events use those seams.
- Define the plugin manifest schema, discovery paths, enable/disable rules, API versioning, and compatibility policy.
- Implement an out-of-process JSONL plugin runner with handshake, capability negotiation, timeout, cancellation, stderr capture, bounded output, malformed-record handling, restart/disable behavior, and contract tests.
- Add plugin tool contributions through the same registry used by built-in tools.
- Add custom backend slash commands through the registry.
- Add prompt templates and structured skills under global/project config paths.
- Add non-mutating event hooks for provider request/response metadata, tool call/result metadata, session lifecycle, and command handling.
- Add extension-scoped permission categories.
- Add plugin and MCP audit fields for plugin id/version, contribution id/type, MCP server id, requested capability, decision, resolver source, and executed core operation.
- Add plugin diagnostics: list, inspect, enable, disable, validate manifest, and show last failure.
- Keep the AI-friendly plugin authoring docs and minimal sample plugin current as the v1 contract hardens.
- Add MCP server support as a plugin contribution type, starting with stdio transport, `initialize`, `tools/list`, `tools/call`, `prompts/list`, and `prompts/get`; defer `resources/list` and `resources/read` unless an explicit read-style permission design lands for 1.0.
- Add MCP diagnostics and tests with a fake server for launch, initialize, list, call, errors, malformed records, timeout, cancellation, restart, and shutdown.
- Design the permission and process boundaries that a future task/subagent worker would need, without shipping built-in orchestration in this phase.

Acceptance criteria:

- Built-in tools and plugin tools share validation, permission, event, and audit paths.
- Plugins cannot mutate files, run commands, or fetch network resources without policy coverage.
- Project plugin code does not execute until the user explicitly enables that plugin and can inspect its requested capabilities.
- Plugin crashes, hangs, malformed records, invalid schemas, and unsupported API versions are reported as plugin failures without crashing AVA or corrupting session JSONL.
- A fake plugin subprocess is covered by tests for registration, successful tool call, permission denial, timeout, cancellation, malformed output, and disable behavior.
- A fake MCP server is covered by tests for initialization, tool listing, tool calls, prompt discovery/get, permission denial, timeout, cancellation, malformed output, and restart behavior. Resource reads remain deferred unless an explicit read-style design lands.
- A developer or AI assistant can create a minimal plugin from the docs without reading AVA C++ internals.
- Future subagent/task workers have documented session, permission, and cancellation constraints before implementation starts.
- Plugin enablement storage, MCP naming, restart backoff, stderr capture limits, and collision handling are documented before the runner is treated as stable.

## 1.0 Cut Line

Versioned path from the 0.60 platform catch-up into the shipped 1.0 backend MVP:

- 0.65: provider-native hardening for Anthropic, Kimi/Moonshot/OpenAI-compatible shims, provider-switch safety, retry/idempotency documentation, and offline contract validation; live provider smokes remain the main deferred release evidence.
- 0.70: bundled into the 0.65 pass as a bounded reasoning/model lifecycle closeout for protocol docs, focused tests, and export polish.
- 0.75: source-backed extension foundation alpha: plugin manifests, explicit enablement, JSONL handshake, extension-scoped permissions/audit, plugin commands and prompt/skill resources, and MCP tools/prompts through bounded registry paths.
- 0.80: extension API stabilization. Authoring docs, the sample plugin, real-sample headless RPC smoke coverage, compatibility policy, minimal golden tests, focused plugin/MCP failure containment coverage, audit rechecks, explicit MCP resource deferral, and OpenAI 5.5 manual headless validation are implemented; remaining stabilization is limited to final diagnostics polish or review-driven failure-matrix closure discovered by 0.90.
- 0.90: v1 release-candidate completion. Freezes v1 compatibility, re-audits docs/capabilities, records release verification and live smokes, resolves or explicitly defers every required partial, and provides the release-bump evidence. OpenAI live evidence is current and Kimi-for-coding has passing live prompt evidence; Anthropic remains auth-blocked, while Moonshot/OpenRouter-compatible prompt evidence remains credential-blocked.
- 1.0: shipped backend MVP runtime at `1.0.0` after the required cut-line capabilities below were completed and verified.

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
- Provider-native Anthropic support hardened through offline/fake coverage, plus Kimi/Moonshot and at least one OpenAI-compatible provider shim covered by fake-provider contract tests. OpenAI and Kimi-for-coding live prompt evidence now pass, and Kimi-for-coding is accepted as the additional production-quality provider path for 1.0. Anthropic is still blocked by configured-key authentication, and Moonshot/OpenRouter-compatible prompt evidence is blocked by missing auth for follow-up provider-breadth validation.
- Mid-session model switching, model-change session entries, and session stats exposed over RPC.
- Thinking/reasoning controls for providers and models that support them.
- Stable local plugin foundation with manifest schema, versioned out-of-process JSONL protocol, tool/command/prompt/event/MCP contributions, diagnostics, docs, and regression tests.

Strongly desired for 1.0:

- Session tree storage and fork/clone.
- LSP symbols, definitions, references, and richer code-intelligence UI beyond diagnostics.
- Image/file attachment reads for multimodal providers.
- Search parity for advanced `.gitignore` syntax beyond the documented native subset.
- Prompt templates and structured skills.
- Optional OS-level sandbox integration for plugin processes where it can be implemented without weakening portability.

Defer unless earlier phases finish cleanly:

- HTTP/server daemon mode.
- Plugin package manager, marketplace, or remote install flow.
- In-process native plugin ABI.
- Provider/message-interception plugins that can alter core prompts or provider requests before the event, permission, and audit model proves safe.
- MCP marketplace/discovery, complex remote MCP OAuth flows, and MCP sampling callbacks.
- Cross-platform hard sandbox guarantees for arbitrary untrusted plugin executables.
- Full theme/UI extension system.
- Built-in multi-agent orchestration, including `task` workers and subagents.

These deferred items remain product-roadmap items rather than discarded ideas. Track them as 1.1+ follow-up work after the backend MVP is stable and verified.

## Immediate Next Work

The original Phase 5.5-only next-work marker has been overtaken by the 0.33/0.60 backend work, the 0.65/0.70 provider and reasoning closeout, the 0.75 extension foundation, the 0.80 stabilization/validation pass, and the 0.90 release-candidate evidence. Current next work is:

- Run final commit/tag/release-publication steps only if explicitly requested.
- Keep Anthropic, Moonshot, and OpenRouter-compatible live smokes as follow-up provider-breadth validation unless the product decision changes.
- Start 1.1+ planning from the post-1.0 roadmap items instead of continuing to label the backend as 0.33, 0.60, or 0.90.
