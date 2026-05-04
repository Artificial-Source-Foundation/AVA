# AVA Backend PI Baseline

This document defines what "as mature as PI as a baseline" means for AVA's backend. PI and OpenCode remain behavior references only. AVA should match the useful backend capability shape while preserving AVA's C++23, one-binary, terminal-first, permissioned local-agent architecture.

This is not a frontend polish goal. The backend baseline is about semantic contracts, runtime correctness, provider/tool/session robustness, extension containment, and tests. UI layout, cards, borders, modals, colors, and terminal rendering remain frontend-owned.

## Baseline Definition

AVA reaches PI-baseline backend maturity when these gates are true:

1. Runs are evented, replayable, and cancelable.
   - Every run has stable run, turn, message, tool-call, request, and correlation IDs where applicable.
   - Streaming text, reasoning, tool lifecycle, permissions, questions, retries, compaction, usage, cancellation, errors, and completion are emitted as semantic events.
   - TUI, print mode, RPC, tests, and future clients can consume the same event stream without terminal-specific payloads.
   - Cancellation is observed promptly at provider streaming boundaries, provider retry sleeps, shell/process boundaries, and long-running tools.

2. Providers are a tested matrix, not one happy path.
   - OpenAI remains first-class.
   - At least one additional provider family is production-quality, with provider-native message/tool/reasoning replay.
   - OpenAI-compatible and Anthropic-compatible routes have explicit quirks, auth, base URL, model capability, usage, context overflow, retry, and streaming tests.
   - Provider parsers normalize events into text, reasoning, tool call, usage, stop, and error events.
   - Provider switching either preserves semantics or prunes/translates unsafe provider-native data explicitly.

3. Tools are structured, permissioned operations.
   - Tools expose schemas, validate input deterministically, return structured results, and report summary/content/error/diff/changed paths/truncation/spill metadata.
   - Tool execution and tool results are useful to both the model and users.
   - File writes are atomic where practical; shell execution has timeout, cancellation, output limits, and process cleanup.
   - Tool permission prompts and audit entries connect request, decision, actor, executed operation, and result.

4. Permissions are shared by interactive and headless modes.
   - Allow, ask, deny, and resolver flows are identical across TUI, print/RPC, and plugin/MCP calls.
   - Persistent rules and session grants are explicit, inspectable, and revocable.
   - Deny reasons, risk/severity where available, target path/command, diff preview, and resolution status are semantic data, not renderer text.
   - New tool classes, plugin tools, MCP tools, and external-directory operations cannot bypass policy.

5. Sessions survive long real work.
   - Session JSONL stays append-only, inspectable, versioned, and migration-ready.
   - Message, tool, permission, model, reasoning, compaction, usage, cancel, and error entries are durable enough for replay and audit.
   - Long sessions can compact automatically and manually without losing tool/result semantics.
   - Session stats and context-window status are available to headless clients and the TUI without frontend reconstruction.
   - Fork/clone/tree navigation is either implemented or storage is demonstrably ready for it.

6. Headless/RPC is a first-class backend surface.
   - RPC has a versioned protocol, semantic event envelopes, state queries, resolver replies, cancellation, session operations, compaction, export, usage/stat commands, and protocol golden tests.
   - Print mode remains script-friendly and shares the same runtime path.
   - TUI-specific code is not required to exercise backend behavior.

7. Extensibility is contained.
   - Built-in tools and plugin/MCP tools route through the same registry, validation, permission, event, audit, cancellation, and session paths.
   - Out-of-process plugins have manifest validation, versioned handshake, bounded JSONL messages, startup/request timeouts, crash/malformed-output containment, and diagnostics.
   - MCP stdio servers are explicit, permissioned, bounded, and covered by fake-server tests.

8. Verification matches the risk.
   - Core tests cover event serialization, provider parsing, provider matrix behavior, tool schemas/results, permission parity, session replay, compaction, cancellation, RPC protocol, plugin/MCP containment, and regressions.
   - Fake providers and fake servers cover deterministic contract tests.
   - Live smokes are documented and opt-in for paid/network provider paths.
   - Normal build, sanitizer build, CTest, and whitespace checks pass before handoff.

## Current AVA Snapshot

AVA is past the toy-backend stage. The important existing seams are:

- Runtime events and envelopes: `src/ava/app/events.*`.
- Runtime/RPC/print mode: `src/ava/app/runtime.*`, `src/ava/app/rpc/*`, `src/ava/app/print_mode.*`.
- Provider-neutral request, stream, reasoning, usage, retry, and transport contracts: `src/ava/provider/provider.h`.
- Provider-native content parts for text, reasoning, tool use, and tool result: `src/ava/provider/provider.h`.
- Tool dispatcher and registry: `src/ava/agent/tool_dispatcher.*`, `src/ava/agent/tool_registry.*`, `src/ava/agent/tool_types.h`.
- Permission requests/prompts/resolvers: `src/ava/permissions/permission.*`.
- Append-only session storage and typed entry categories: `src/ava/session/session_store.*`.
- Focused tests already exist under `tests/` for providers, events, runtime, tools, sessions, permissions, plugin, MCP, LSP, and TUI composer behavior.

The backend is not yet PI-baseline because several seams are still thinner than the maturity target:

- `RuntimeEvent` is semantic but still uses a broad flat payload with many optional string fields. That is workable for compatibility, but typed payload contracts should be strengthened around tools, permissions, questions, usage, and cancellation.
- `ToolDispatchResult` now carries an initial structured result payload with status, content type, errors, diffs, changed paths, truncation, and spill metadata. PI-baseline tools still need broader permission linkage, RPC golden coverage, and full migration away from text-only compatibility paths.
- Cancellation now reaches provider retry sleeps, streaming and non-streaming transport boundaries, bash process cleanup, search/webfetch tool paths, and plugin/MCP brokered tool calls. The baseline still requires explicit guarantees for every remaining long-running file/LSP operation and broader RPC cancel golden coverage.
- Provider abstractions are credible, but baseline maturity requires a tested provider matrix and cross-provider semantics, not only OpenAI plus partial Anthropic support.
- Sessions are inspectable and versioned, and an initial replay validator checks entry IDs, parent links, tool call/result pairing, permission audit integrity/pairing, structured tool-result payloads, compaction preservation boundaries, and model/reasoning entry shape. Branch-ready structure and migration/repair coverage still need to be proven.
- Plugin and MCP direction exists, but baseline maturity requires stronger shared registry/policy/audit/cancellation behavior and more containment tests.

## Gap Matrix

| Area | PI-baseline expectation | AVA status | Closure work | Verification gate |
| --- | --- | --- | --- | --- |
| Runtime events | Stable event envelope plus typed semantic payloads for run/message/reasoning/tool/permission/question/retry/compaction/usage/cancel/error | Partial. Event envelope exists; payloads are still broad and flat in places | Define typed payload structs or narrow JSON schema helpers for the high-risk event families while keeping compatibility serialization | Event golden tests, replay tests, RPC round-trip tests |
| Cancellation | Prompt cancellation at provider stream, retry sleep, tool execution, shell timeout, process cleanup, and active run queue | Partial. Provider retry, streaming/non-streaming transport, bash, search, webfetch, plugin, and MCP paths are covered; LSP and some file-tool paths still need explicit coverage | Keep the shared cancellation callback flowing through remaining long-running tools and add fuller RPC golden coverage | Fake transport abort tests, retry-sleep cancel test, bash/plugin/MCP process cleanup tests, RPC cancel test |
| Providers | OpenAI plus at least one production-quality additional family, compatible-provider shims, reasoning/tool/usage/context/retry normalization | Partial. OpenAI strong; Anthropic native slice present but not complete; Kimi/OpenAI-compatible hardening remains | Finish Anthropic thinking/cache/stop/usage, add Kimi/Moonshot profile, harden OpenAI-compatible and Anthropic-compatible route tests | Provider matrix tests for stream, abort, empty, context overflow, tokens, tool call/result, reasoning, cross-provider handoff |
| Tools | Structured schema/input/result lifecycle, bounded output, diffs, changed paths, errors, truncation/spill, permission metadata, deterministic summaries | Partial. Structured tool result payloads exist for dispatcher/runtime/session paths, but permission linkage and RPC goldens remain incomplete | Complete permission linkage and compatibility migration for built-in, plugin, MCP, command, and RPC tool paths | Tool contract tests, golden result JSON tests, truncation/spill tests, permission audit tests |
| Permissions | Shared policy for interactive/headless/plugin/MCP; persistent rules; audit links request/decision/operation/result | Partial. Policy/prompt/resolver/audit foundation exists | Add persistent rules/session grants, risk/severity fields where real, richer audit records, plugin/MCP categories in execution path | Permission parity tests across TUI/RPC/tool/plugin/MCP fakes |
| Sessions/context | Durable semantic transcript with versioning, compaction, usage, model/reasoning changes, replay, branch-ready shape | Partial. JSONL store, key entry types, stats, and replay validation cover IDs, parent links, tool/permission pairing, compaction boundaries, and model/reasoning entry shape | Extend replay validation to entry-version checks, migration/repair coverage, and branch metadata | Session replay/repair tests, compaction semantic preservation tests, stats/context tests |
| RPC/headless | Versioned protocol can drive all backend behavior without TUI dependencies | Partial to strong. Protocol exists with resolver, state, stats, and validation paths plus a direct headless CLI smoke | Add full protocol golden suite and command coverage for model/reasoning/session/fork-ready operations | JSONL protocol tests, subprocess smokes, resolver-flow tests |
| Extensibility/MCP | Out-of-process plugin/MCP tools use same registry/policy/events/session path and fail contained | Partial. Plugin/MCP registry, permission, audit, bounded protocol, timeout, and cancellation paths exist; baseline containment still needs broader proof | Manifest/handshake hardening, richer event/session integration, bounded output/errors, audit labels, fake-server/plugin failure matrix | Plugin/MCP contract tests for crash, timeout, cancellation, malformed JSON, unsupported version, denied permission |
| Observability/tests | Contract-focused tests and opt-in live smokes prevent backend regressions | Partial. Good focused CTest set; matrix/stress coverage incomplete | Add deterministic fake-provider and fake-server suites, sanitizer preset in release checklist, documented live-smoke scripts | `cmake`, `ctest`, sanitizer CTest, `git diff --check`, opt-in live smokes |

## Staged Hardening Plan

### Stage 0: Freeze The Contract Target

Deliverables:

- Keep this document as the PI-baseline acceptance target.
- Keep `docs/roadmap/backend.md` as the broader 1.0 roadmap.
- Treat reference code as product behavior input only.

Exit gate:

- New backend work maps to at least one gap row above or is explicitly out of scope.

### Stage 1: Tool Result And Event Payload Contracts

Deliverables:

- Add a structured tool result type alongside or underneath `ToolDispatchResult`.
- Include success, summary, content, content type, error category/code/message, diff, changed paths, truncation, spill file, permission request/resolution linkage, and execution status.
- Update runtime events/session entries/RPC serialization to carry the structured fields without terminal layout data.
- Add golden tests for tool start/progress/result/error event payloads and session persistence.

Exit gate:

- Built-in tools return semantically typed results, and old text output remains only compatibility/debug presentation.

### Stage 2: Cancellation And Process Robustness

Deliverables:

- Define a shared cancellation token or equivalent narrow contract.
- Pass cancellation through runtime, provider retry sleeps, streaming transport, tool dispatcher, bash, webfetch, search, and LSP where meaningful.
- Ensure shell cancellation/timeout cleans up the process group or platform-equivalent child tree.
- Normalize canceled outcomes in events, sessions, and RPC.

Exit gate:

- Deterministic tests prove cancellation for provider stream, retry sleep, bash timeout/cancel, active run queue, and at least one long-running tool path.

### Stage 3: Provider Matrix Maturity

Deliverables:

- Finish Anthropic-native thinking/cache-control/stop/usage support.
- Add Kimi/Moonshot capability profile after checking current provider docs and reference behavior.
- Add explicit OpenAI-compatible and Anthropic-compatible shims with auth/base URL/quirk metadata.
- Harden provider-switch semantics for native content parts.
- Expand fake-provider matrix for abort, empty output, context overflow, unicode/surrogate safety, usage totals, tool call/result replay, reasoning, and cross-provider handoff.

Exit gate:

- OpenAI plus one non-OpenAI family are production-quality, and compatible-provider behavior is covered by deterministic tests.

### Stage 4: Session, Context, And RPC Replay

Deliverables:

- Add session replay validators for IDs, parent links, entry versions, tool call/result pairing, permission pairing, compaction preservation, and model/reasoning changes. Initial ID, parent, tool, permission, compaction, and model/reasoning shape checks are implemented; entry version/migration and branch metadata checks remain.
- Expose context-window and usage status through RPC/runtime events.
- Add fork/clone/tree-ready metadata or implement the first backend-only fork operation if the storage shape is ready.
- Add protocol golden tests for session, compact, export, usage, model, reasoning, cancel, permission, and question flows.

Exit gate:

- A headless test can replay a realistic long session through compaction and still recover transcript, tools, permissions, usage, and final status.

### Stage 5: Permission Rules And Audit

Deliverables:

- Add persistent rules and session grants with explicit scope, reason, expiry/revocation shape if applicable, and audit entries.
- Include risk/severity only when it is derived from real policy classification rather than guessed by the frontend.
- Ensure plugin/MCP/external-directory/network/LSP operations use the same policy path as built-in tools.

Exit gate:

- Interactive and RPC resolver tests make the same allow/ask/deny decisions and produce equivalent audit entries.

### Stage 6: Plugin And MCP Containment

Deliverables:

- Finalize manifest schema, API versioning, discovery paths, enable/disable rules, and capability declarations.
- Route plugin tools and MCP tools through shared validation, permission, event, audit, cancellation, truncation, and session paths.
- Add malformed JSONL, timeout, crash, unsupported version, denied permission, bad result schema, and stderr diagnostics tests.

Exit gate:

- A bad plugin or fake MCP server cannot crash AVA, bypass permissions, corrupt sessions, or emit unbounded output.

### Stage 7: Release Verification Harness

Deliverables:

- Document normal, sanitizer, and opt-in live-smoke commands.
- Add deterministic subprocess/RPC smokes that exercise resolver replies, cancellation, tool failures, compaction, and session export.
- Keep live provider smokes opt-in and credential-gated.

Exit gate:

- Backend handoff requires normal CMake build, CTest, sanitizer CTest for touched backend work, whitespace check, and relevant deterministic smokes.

## First Implementation Slice

The best first code slice is Stage 1: structured tool results and event payload contracts.

Why this first:

- It is central to PI-baseline maturity because tools, sessions, permissions, RPC, plugins, MCP, and frontend rendering all depend on the same result semantics.
- It is narrower than provider breadth or plugin hosting.
- It reduces future churn for Carlo's frontend API because backend events will carry semantic data instead of UI-shaped summaries.

Initial patch scope:

1. Add a structured tool result type under `src/ava/agent/` or `src/ava/tools/` with:
   - `call_id`, `name`, `status`, `success`;
   - `summary`, `content`, `content_type`;
   - `error_category`, `error_code`, `error_message`;
   - `diff`, `changed_paths`;
   - `truncated`, `omitted_bytes`, `omitted_lines`, `spill_path`;
   - optional permission request/resolution IDs once the audit path has stable IDs.
2. Adapt `ToolDispatchResult` or replace it behind a compatibility helper.
3. Update runtime event serialization and session tool-result entries to preserve the structured fields.
4. Add tests for:
   - result invariants;
   - JSON serialization;
   - truncation/spill fields;
   - diff/changed-path propagation;
   - tool failure event payloads;
   - compatibility plain text summary.

The first implementation should not be treated as full Stage 1 closure until command-triggered tools, plugin tools, MCP tools, and RPC golden tests also consume the same structured result shape. This should be followed by Stage 2 cancellation hardening, because robust tool results need reliable canceled/error states.
