# Backend Capabilities For 1.0

This checklist tracks the backend capabilities AVA needs for 1.0. `docs/roadmap/backend.md` owns sequencing; this file exists so product docs have one status view.

Status legend: `Done` means implemented with regression coverage, `Partial` means a foundation exists but 1.0 behavior is incomplete, `Planned` means not implemented yet, and `Deferred` means outside the 1.0 cut unless earlier phases finish cleanly.

| Capability | Status | Roadmap Phase | Notes |
| --- | --- | --- | --- |
| OpenAI OAuth refresh before provider requests | Done | Phase 1 | Implemented in the auth/provider path and covered by tests. |
| Permission decision persistence and session audit export | Done | Phase 1 | Tool permission decisions are written as session audit entries. |
| Atomic file writes for normal write/edit flows | Done | Phase 1 | Existing file tools use temp-file plus rename where practical. |
| Print mode and JSONL RPC MVP | Done | Phase 0/1 | RPC still needs Phase 2 protocol expansion. |
| Project/global `AGENTS.md` context loading | Done | Phase 0/3 | Structured skills and prompt templates remain future work. |
| Manual compaction entries and session export | Done | Phase 0/3 | Provider-generated summaries and automatic compaction are still planned. |
| Stable shared runtime event stream | Partial | Phase 2 | Basic events exist; rich turn/message/tool/provider deltas and event bus are planned. |
| Async cancellation for active provider and tool runs | Partial | Phase 2 | Bash timeout cleanup exists; full active-run RPC cancellation is planned. |
| Versioned stable RPC protocol for editor integrations | Partial | Phase 2 | Current JSONL RPC is MVP; protocol versioning and resolver flows are planned. |
| Provider-generated manual and automatic compaction | Planned | Phase 3 | Requires usage/context metadata and provider summary calls. |
| Usage, cost, and context accounting | Planned | Phase 3/5 | Requires provider usage extraction and model pricing metadata. |
| Session stats over TUI/RPC | Planned | Phase 3 | Should aggregate from session entries, not require clients to parse JSONL. |
| Session tree storage, fork, clone, and branch summaries | Strongly desired | Phase 3 | Storage has `id`/`parent_id` foundations; full branching can follow if time allows. |
| `.gitignore`-aware search | Strongly desired | Phase 4 | Current search has hardcoded exclusions, not full ignore semantics. |
| Diff previews, fuzzy edit matching, CRLF/BOM handling | Strongly desired | Phase 4 | Needed for PI-level edit ergonomics. |
| Web fetch tool behind network permission | Strongly desired | Phase 4 | Requires a `network.fetch` policy category and bounded fetch implementation. |
| LSP diagnostics/symbols/definitions/references | Strongly desired | Phase 4 | Diagnostics can be the first useful slice. |
| Provider registry and model capability catalog | Planned | Phase 5 | OpenAI remains current path; registry must precede serious provider breadth. |
| One additional production-quality provider | Planned | Phase 5 | Must include streaming, tools where supported, usage, auth, and tests. |
| Mid-session model switching and model-change entries | Planned | Phase 5 | Requires provider registry and session entry support. |
| Reasoning/thinking controls | Planned | Phase 5 | Only for models whose metadata declares support. |
| Stable local plugin foundation | Planned | Phase 6 | Out-of-process JSONL plugins with manifest, diagnostics, permissions, and tests. |
| MCP servers through plugin/runtime safety model | Planned | Phase 6 | Stdio MCP tools/resources/prompts are the required first slice. |
| Plugin package manager, marketplace, remote install | Deferred | After 1.0 | Explicitly excluded from the 1.0 plugin foundation. |
| In-process native plugin ABI | Deferred | After 1.0 | Excluded due crash, memory, and C++ ABI risk. |
| HTTP/server daemon mode | Deferred | After RPC maturity | Stdio RPC must be proven first. |
