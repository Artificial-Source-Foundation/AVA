# Backend Capabilities For 1.0

This checklist tracks the backend capabilities AVA needs for 1.0. `docs/roadmap/backend.md` owns sequencing; this file exists so product docs have one status view. In this file, MVP and 1.0 mean the same release cut; post-MVP means 1.1 and later.

Status legend: `Done` means implemented with regression coverage, `Partial` means a foundation exists but 1.0 behavior is incomplete, `Planned` means required or desired work that is not implemented yet, and `Deferred` means outside the 1.0 cut but retained on the 1.1+ roadmap.

| Capability | Status | Roadmap Phase | Notes |
| --- | --- | --- | --- |
| OpenAI OAuth refresh before provider requests | Done | Phase 1 | Implemented in the auth/provider path and covered by tests. |
| Permission decision persistence and session audit export | Done | Phase 1 | Tool permission decisions are written as session audit entries. |
| Atomic file writes for normal write/edit flows | Done | Phase 1 | Existing file tools use temp-file plus rename where practical. |
| Print mode and JSONL RPC MVP | Done | Phase 0/1/2 | Protocol version 1, shared event envelopes, resolver replies, session commands, steering, and follow-up queues are implemented. Live headless smokes verified read/search/webfetch in print mode and write/edit/apply_patch/bash/question through RPC replies. |
| Project/global `AGENTS.md` context loading | Done | Phase 0/3 | Structured skills and prompt templates remain future work. |
| Manual compaction entries and session export | Done | Phase 0/3 | Provider-generated manual compaction, automatic compaction, retained recent context, and session export are implemented. |
| Stable shared runtime event stream | Done | Phase 2 | Versioned runtime envelopes and provider/tool/queue events are implemented; richer reasoning-specific events remain provider-track work. |
| OpenAI Responses tool-call streaming parser | Done | Phase 2 | Handles function-call starts from both `response.function_call.added` and live `response.output_item.added` item events; covered by regression tests and headless tool-call smoke. |
| Async cancellation for active provider and tool runs | Partial | Phase 2/4 | RPC active-run cancellation and queue cleanup exist; 1.0 should improve long-running tool/shell cancellation where safe, while provider transport calls may remain cooperative until the transport boundary supports interruption safely. |
| Versioned stable RPC protocol for editor integrations | Done | Phase 2 | Protocol versioning, resolver replies, session commands, steering, and follow-up queues are implemented for protocol version 1. |
| Provider-generated manual and automatic compaction | Done | Phase 3 | `/compact`, automatic compaction, retained recent context, and one context-overflow compaction retry are implemented. |
| Usage, cost, and context accounting | Partial | Phase 3/5 | Provider usage extraction, conservative cost accounting, and local model pricing metadata exist; broader provider catalog/pricing coverage remains Phase 5. |
| Session stats over TUI/RPC | Done | Phase 3 | RPC stats aggregate counts, exact provider usage, estimated fallback bytes, and conservative cost completeness from session entries. |
| Session tree storage, fork, clone, and branch summaries | Partial | Phase 3 | Storage has `id`/`parent_id` foundations and validation; full fork/clone/tree UI and branch summaries are deferred. |
| `.gitignore`-aware search | Done | Phase 4 | Native matcher respects root/nested `.gitignore` by default with a documented subset; local/internal `no_ignore` remains explicit-only and provider schemas cannot disable ignores. |
| Diff previews, fuzzy edit matching, CRLF/BOM handling | Partial | Phase 4 | Bounded unified diffs, exact-match diagnostics, CRLF/BOM preservation, and mutation serialization are implemented; broader fuzzy/Unicode matching remains future work. |
| Web fetch tool behind network permission | Done | Phase 4 | `webfetch` uses `network.fetch`, URL/DNS validation, DNS pinning, disabled redirects, text/binary filtering, timeout and size caps. |
| LSP diagnostics/symbols/definitions/references | Partial | Phase 4/1.1 | `lsp_diagnostics` first slice is done and capability-gated; symbols, definitions, references, document sync, and production server discovery are deferred to 1.1+. |
| Provider registry and model capability catalog | Partial | Phase 5/5.5 | Registry, built-in OpenAI/Anthropic factories, and capability metadata exist; MVP still needs richer reasoning/cache/quirk metadata for Anthropic, Kimi/Moonshot, and OpenAI-compatible shims. |
| One additional production-quality provider | Partial | Phase 5/5.5 | Anthropic Messages first slice exists with native `tool_use`/`tool_result`; Anthropic is not 1.0 production-quality until thinking/cache-control/signature/stop-reason hardening and smoke coverage are complete. |
| Kimi/Moonshot coding profile | Planned | Phase 5.5 | Required for 1.0. Must compare current PI/OpenCode behavior and public Kimi/Moonshot docs before implementation; needs aliases, credentials, reasoning output, defaults, overflow errors, fake tests, and optional credentialed smokes. |
| OpenAI-compatible provider shims | Planned | Phase 5.5 | Required for 1.0 provider breadth. Needs configurable base URLs, auth/header/path quirks, usage/error normalization, and fake contract tests. |
| Provider-native reasoning storage and replay | Planned | Phase 5.5 | Must define an inspectable session schema for reasoning blocks, signatures, and reasoning-control changes before provider-specific code lands. |
| Frontend-visible reasoning events | Planned | Phase 5/5.5 | Required for 1.0. Runtime/RPC should emit reasoning start/delta/end events so clients can show what supported models are thinking without parsing provider-specific raw events. |
| Mid-session model switching and model-change entries | Partial | Phase 5 | RPC model listing/switching and durable model-change entries exist; MVP still needs provider-history compatibility checks, reasoning-control entries, and edge-case tests. |
| Reasoning/thinking controls | Planned | Phase 5/5.5 | Only for models whose metadata declares support, and only when provider request builders serialize the native fields. |
| Anthropic OAuth refresh | Deferred | 1.1 candidate | OpenAI OAuth refresh is done; Anthropic bearer/OAuth tokens are currently treated as static credentials until provider-specific refresh support is designed and tested. |
| Stable local plugin foundation | Planned | Phase 6 | Out-of-process JSONL plugins with manifest, explicit enablement, diagnostics, permissions, audit identity, fake plugin tests, and sample docs. |
| MCP servers through plugin/runtime safety model | Planned | Phase 6 | Stdio MCP tools are required first; resources/prompts are strongly desired but may defer to 1.1 if tool-host stability requires focus. |
| HTTP/server daemon mode | Deferred | 1.1 candidate | Keep on the roadmap after stdio RPC maturity. |
| Persistent permission rules | Deferred | 1.1 candidate | Session audit exists; cross-session allow/deny persistence is post-MVP. |
| Session tree UI, fork, clone, and branch summaries | Deferred | 1.1 candidate | Storage has foundations; user-facing tree workflows are post-MVP. |
| Full LSP symbols, definitions, and references | Deferred | 1.1 candidate | Diagnostics remain the 1.0 code-intelligence slice. |
| Image or multimodal support | Deferred | 1.1 candidate | Requires provider modality metadata, session attachment shape, and provider payload support. |
| Parallel tool execution | Deferred | 1.2 candidate | Sequential execution remains safer for MVP; revisit after registry, permissions, and cancellation mature. |
| Advanced MCP HTTP/OAuth/subscriptions/sampling/pagination | Deferred | 1.2 candidate | Stdio MCP tool hosting is the MVP; advanced remote and interactive MCP behavior is later work. |
| Plugin package manager, marketplace, remote install | Deferred | 1.2+ candidate | Explicitly excluded from the 1.0 plugin foundation but retained as later product work. |
| In-process native plugin ABI | Deferred | Later research | Excluded due crash, memory, and C++ ABI risk; only reconsider if AVA accepts that support burden. |
| Multi-agent/subagent orchestration | Deferred | Later research | Requires stable plugin/process/session boundaries first. |
