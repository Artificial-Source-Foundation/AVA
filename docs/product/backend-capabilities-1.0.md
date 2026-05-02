# Backend Capabilities For 1.0

This checklist tracks the backend capabilities AVA needs for 1.0. `docs/roadmap/backend.md` owns sequencing; this file exists so product docs have one status view.

Status legend: `Done` means implemented with regression coverage, `Partial` means a foundation exists but 1.0 behavior is incomplete, `Planned` means not implemented yet, and `Deferred` means outside the 1.0 cut unless earlier phases finish cleanly.

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
| Async cancellation for active provider and tool runs | Partial | Phase 2 | RPC active-run cancellation and queue cleanup exist; provider transport calls are still cooperative rather than interrupted mid-request. |
| Versioned stable RPC protocol for editor integrations | Done | Phase 2 | Protocol versioning, resolver replies, session commands, steering, and follow-up queues are implemented for protocol version 1. |
| Provider-generated manual and automatic compaction | Done | Phase 3 | `/compact`, automatic compaction, retained recent context, and one context-overflow compaction retry are implemented. |
| Usage, cost, and context accounting | Partial | Phase 3/5 | Provider usage extraction, conservative cost accounting, and local model pricing metadata exist; broader provider catalog/pricing coverage remains Phase 5. |
| Session stats over TUI/RPC | Done | Phase 3 | RPC stats aggregate counts, exact provider usage, estimated fallback bytes, and conservative cost completeness from session entries. |
| Session tree storage, fork, clone, and branch summaries | Partial | Phase 3 | Storage has `id`/`parent_id` foundations and validation; full fork/clone/tree UI and branch summaries are deferred. |
| `.gitignore`-aware search | Done | Phase 4 | Native matcher respects root/nested `.gitignore` by default with a documented subset; local/internal `no_ignore` remains explicit-only and provider schemas cannot disable ignores. |
| Diff previews, fuzzy edit matching, CRLF/BOM handling | Partial | Phase 4 | Bounded unified diffs, exact-match diagnostics, CRLF/BOM preservation, and mutation serialization are implemented; broader fuzzy/Unicode matching remains future work. |
| Web fetch tool behind network permission | Done | Phase 4 | `webfetch` uses `network.fetch`, URL/DNS validation, DNS pinning, disabled redirects, text/binary filtering, timeout and size caps. |
| LSP diagnostics/symbols/definitions/references | Partial | Phase 4 | `lsp_diagnostics` first slice is implemented and capability-gated; symbols, definitions, references, document sync, and production server discovery remain future work. |
| Provider registry and model capability catalog | Planned | Phase 5 | OpenAI remains current path; registry must precede serious provider breadth. |
| One additional production-quality provider | Planned | Phase 5 | Must include streaming, tools where supported, usage, auth, and tests. |
| Mid-session model switching and model-change entries | Planned | Phase 5 | Requires provider registry and session entry support. |
| Reasoning/thinking controls | Planned | Phase 5 | Only for models whose metadata declares support. |
| Stable local plugin foundation | Planned | Phase 6 | Out-of-process JSONL plugins with manifest, diagnostics, permissions, and tests. |
| MCP servers through plugin/runtime safety model | Planned | Phase 6 | Stdio MCP tools/resources/prompts are the required first slice. |
| Plugin package manager, marketplace, remote install | Deferred | After 1.0 | Explicitly excluded from the 1.0 plugin foundation. |
| In-process native plugin ABI | Deferred | After 1.0 | Excluded due crash, memory, and C++ ABI risk. |
| HTTP/server daemon mode | Deferred | After RPC maturity | Stdio RPC must be proven first. |
