# Zed Deep Audit

> Comprehensive analysis of Zed's AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/zed/crates/agent/` and `crates/assistant/`

---

## Overview

Zed is a **native GPU-accelerated editor** (GPUI framework) with integrated AI coding assistance. Its core differentiator is **StreamingDiff** — applies edits AS the LLM streams tokens, not after completion, using character-level incremental diffing with sophisticated scoring heuristics. Zed implements **per-hunk accept/reject** UI for granular change review with `ActionLog` tracking per-buffer diffs. It serves as both **MCP client and server** — consuming MCP tools AND exposing Zed's capabilities to other agents via the A2A protocol. The agent system uses a **Thread-based loop** with 18 built-in tools, sophisticated fuzzy matching (`StreamingFuzzyMatcher` with Levenshtein distance), and reindentation. Zed supports **agent profiles** bundling model + tools + instructions, and **subagent spawning** with max depth 1.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **StreamingDiff** | Applies edits AS LLM streams | `crates/streaming_diff/src/streaming_diff.rs` |
| **Two Strategies** | EditFileTool (secondary LLM) + StreamingEditFileTool (direct) | `crates/agent/src/tools/edit_file_tool.rs`, `crates/agent/src/tools/streaming_edit_file_tool.rs` |
| **StreamingFuzzyMatcher** | Levenshtein distance, line-by-line incremental | `crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs` |
| **Character-Level Diff** | Insert/Delete/Keep operations | `crates/streaming_diff/src/streaming_diff.rs` |
| **Reindentation** | Adjusts indentation as chunks stream | `crates/agent/src/edit_agent/reindent.rs` |
| **Per-Hunk Accept/Reject** | Granular change review | `crates/agent_ui/src/buffer_codegen.rs` |
| **Two Edit Formats** | XML tags (`<old_text>`) and diff-fenced (`<<<<<<< SEARCH`) | `crates/agent/src/edit_agent/edit_parser.rs` |
| **EditAgent** | Sub-agent for file modifications | `crates/agent/src/edit_agent.rs` |
| **ToolEditParser** | Partial JSON → edit event stream | `crates/agent/src/tools/tool_edit_parser.rs` |
| **Progressive Edit Events** | ResolvingEditRange, Edited, etc. | `crates/agent/src/edit_agent.rs` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Thread-Based Context** | `Thread` struct with `Vec<Message>` | `crates/agent/src/thread.rs` |
| **Full Thread Kept** | No windowing or truncation | `crates/agent/src/thread.rs:build_request_messages()` |
| **No Explicit Compaction** | Summaries for UI only | `crates/agent/src/thread.rs:summary()` |
| **SQLite Persistence** | zstd-compressed JSON blobs | `crates/agent/src/db.rs` |
| **Thread Store** | CRUD operations | `crates/agent/src/thread_store.rs` |
| **Rich Mentions** | Files, directories, symbols, selections, URLs | `crates/agent/src/thread.rs` |
| **Token Tracking** | Per-request and cumulative | `crates/agent/src/thread.rs` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Thread-Based Loop** | `run_turn_internal` with streaming | `crates/agent/src/thread.rs:1798-1972` |
| **18 Built-in Tools** | Tool registry | `crates/agent/src/tools.rs` |
| **Tool Execution** | `FuturesUnordered` collection | `crates/agent/src/thread.rs:2228+` |
| **Streaming Processing** | `handle_completion_event` dispatches events | `crates/agent/src/thread.rs:2028-2145` |
| **EditAgent Sub-Agent** | Secondary LLM for file edits | `crates/agent/src/edit_agent.rs` |
| **Subagent Spawning** | Max depth 1 | `crates/agent/src/tools/spawn_agent_tool.rs` |
| **Retry with Backoff** | 4 attempts, exponential | `crates/agent/src/thread.rs` |
| **Cancellation** | `watch::channel<bool>` cascade | `crates/agent/src/thread.rs` |
| **Completion Detection** | `StopReason::EndTurn` | `crates/agent/src/thread.rs` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Permission Model** | Settings-driven, regex-based | `crates/agent/src/tool_permissions.rs` |
| **ToolInspector Trait** | No capability declarations | Individual tool `run()` methods |
| **3-Layer Protection** | Regex check, symlink escape, sensitive settings | `crates/agent/src/tool_permissions.rs`, `crates/agent/src/tools/tool_permissions.rs` |
| **Hardcoded Security Rules** | Non-overridable dangerous command blocking | `crates/agent/src/tool_permissions.rs` |
| **Shell Sub-Command Parsing** | Each must pass independently | `crates/agent/src/tool_permissions.rs` |
| **Symlink Escape Detection** | Worktree-aware canonicalization | `crates/agent/src/tools/tool_permissions.rs` |
| **MCP Tools** | Namespaced as `mcp:<server>:<tool>` | `crates/agent/src/tools/context_server_registry.rs` |
| **Double-Gating** | Regex + `authorize_third_party_tool` | `crates/agent/src/tools/context_server_registry.rs` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Native GPU Editor** | GPUI framework | `crates/gpui/` |
| **Agent Panel** | Integrated UI | `crates/agent_ui/` |
| **Inline Diff Display** | Per-hunk accept/reject | `crates/agent_ui/src/buffer_codegen.rs` |
| **MCP Client + Server** | Dual role | `crates/agent/src/tools/context_server_registry.rs`, `crates/agent/src/native_agent_server.rs` |
| **Agent Profiles** | Bundle model + tools + instructions | `crates/agent/src/thread.rs` |
| **Streaming Rendering** | Progressive edit events | `crates/agent/src/edit_agent.rs` |
| **Monaco Editor** | VSCode-like editing | `crates/editor/` |
| **Theme System** | 50+ color tokens | `crates/theme/` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **StreamingDiff** | Applies edits AS LLM streams | `crates/streaming_diff/src/streaming_diff.rs` |
| **Per-Hunk Accept/Reject** | Granular change review | `crates/agent_ui/src/buffer_codegen.rs` |
| **MCP Client + Server** | Dual role | `crates/agent/src/tools/context_server_registry.rs`, `crates/agent/src/native_agent_server.rs` |
| **Native GPU Editor** | GPUI framework | `crates/gpui/` |
| **StreamingFuzzyMatcher** | Incremental Levenshtein matching | `crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs` |
| **Agent Profiles** | Bundled configurations | `crates/agent/src/thread.rs` |
| **18 Built-in Tools** | Comprehensive tool set | `crates/agent/src/tools.rs` |
| **Reindentation** | Indentation adjustment while streaming | `crates/agent/src/edit_agent/reindent.rs` |
| **Diff Entity** | `acp_thread::Diff` with reveal_range | `crates/agent/src/edit_agent.rs` |

---

## Worth Stealing (for AVA)

### High Priority

1. **StreamingDiff** (`crates/streaming_diff/src/streaming_diff.rs`)
   - Applies edits AS LLM streams tokens
   - Reduces perceived latency
   - Character-level Insert/Delete/Keep

2. **Per-Hunk Accept/Reject** (`crates/agent_ui/src/buffer_codegen.rs`)
   - Granular change review
   - Users accept/reject individual hunks
   - Excellent UX for multi-file changes

3. **StreamingFuzzyMatcher** (`crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs`)
   - Incremental Levenshtein matching
   - Line-by-line as tokens arrive
   - 80% match threshold

### Medium Priority

4. **MCP Client + Server** (`crates/agent/src/tools/context_server_registry.rs`, `crates/agent/src/native_agent_server.rs`)
   - Dual role enables agent marketplace
   - Expose AVA's tools to other agents

5. **Reindentation** (`crates/agent/src/edit_agent/reindent.rs`)
   - Adjusts indentation while streaming
   - Handles mixed indentation

6. **Agent Profiles** (`crates/agent/src/thread.rs`)
   - Bundle model + tools + instructions
   - Shareable configurations

7. **Diff Entity** (`crates/agent/src/edit_agent.rs`)
   - Tracks per-edit buffer state
   - Progressive `reveal_range`

### Lower Priority

8. **Native GPU Editor** — AVA uses Tauri + SolidJS, different approach
9. **No Compaction** — Zed keeps full thread; AVA's compaction is better
10. **18 Tools** — AVA has 55+ tools

---

## AVA Already Has (or Matches)

| Zed Feature | AVA Equivalent | Status |
|-------------|----------------|--------|
| Multiple edit strategies | 8 strategies | ✅ Parity |
| Fuzzy matching | Fuzzy matching in edit cascade | ✅ Parity |
| Streaming edits | Streaming tool execution | ✅ Parity |
| MCP client | Full MCP client | ✅ Parity |
| MCP server | (Not implemented) | ❌ Gap |
| Per-hunk accept/reject | (Not implemented) | ❌ Gap |
| StreamingDiff | (Not implemented) | ❌ Gap |
| Agent profiles | Extension configs | ✅ Similar |
| Subagent spawning | `delegate_*` tools | ✅ Better |
| 18 tools | 55+ tools | ✅ Better |

---

## Anti-Patterns to Avoid

1. **No Compaction** — Zed keeps full thread, hits context limits; AVA's compaction is better
2. **Rust-Only** — Limits extension ecosystem; TypeScript is more accessible
3. **GPUI Lock-in** — Native GPU requires specific framework
4. **Complex Fuzzy Matching** — StreamingFuzzyMatcher is complex; AVA's approach is simpler
5. **No Tool Capability Declarations** — Each tool manages own permissions; prefer centralized

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **Enhanced StreamingDiff** — Better scoring heuristics
- **Improved Per-Hunk UI** — Better accept/reject flow
- **MCP Server Improvements** — Better dual-role support
- **Agent Profile Expansion** — More built-in profiles

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `crates/streaming_diff/src/streaming_diff.rs` | ~500 | StreamingDiff algorithm |
| `crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs` | ~300 | Fuzzy matching |
| `crates/agent/src/edit_agent.rs` | ~800 | EditAgent sub-agent |
| `crates/agent_ui/src/buffer_codegen.rs` | ~600 | Per-hunk accept/reject |
| `crates/agent/src/tools.rs` | ~300 | Tool registry (18 tools) |
| `crates/agent/src/thread.rs` | 4,144 | Thread-based loop |
| `crates/agent/src/tools/context_server_registry.rs` | ~600 | MCP client |
| `crates/agent/src/native_agent_server.rs` | ~200 | MCP server |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features. Limited to `crates/agent/` and `crates/assistant/` directories.*
