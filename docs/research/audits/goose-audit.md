# Goose Deep Audit

> Comprehensive analysis of Block's Goose AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/goose/`

---

## Overview

Goose is Block's AI coding assistant built in **Rust + Tauri** — the same stack as AVA. Its core differentiator is being **MCP-first**: extensions ARE MCP servers, with no separate plugin API. Every capability is provided through MCP, making it the most MCP-integrated tool. Goose implements a **3-layer tool inspection pipeline** (`SecurityInspector` → `PermissionInspector` → `RepetitionInspector`) that intercepts every tool call before execution. The architecture uses **MOIM (Message Of Immediate Moment)** — ephemeral per-turn context injection that assembles transient state (timestamp, working directory, token usage) into an XML-tagged user message. Goose supports a **recipe system** with YAML/JSON recipes defining typed parameters, nested sub-recipes, and cron-based scheduling. The **lead-worker model** routes expensive model calls for the first N turns, then switches to a cheaper model with automatic fallback on failure.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Two-Tool Edit System** | `write` (whole-file) and `edit` (search-and-replace) | `crates/goose/src/agents/platform_extensions/developer/edit.rs` |
| **No Strategy Selection** | Single deterministic pipeline | `crates/goose/src/agents/platform_extensions/developer/edit.rs` |
| **Unique Match Enforcement** | Rejects if `old_string` appears 0 or 2+ times | `crates/goose/src/agents/platform_extensions/developer/edit.rs:74-153` |
| **Fuzzy Suggestions** | `find_similar_context` on 0-match | `crates/goose/src/agents/platform_extensions/developer/edit.rs` |
| **MCP-Native Tools** | Tools exposed through MCP protocol | `crates/goose/src/agents/platform_extensions/developer/mod.rs` |
| **Developer Extension** | Built-in MCP server for core tools | `crates/goose/src/agents/platform_extensions/developer/` |
| **Tree Tool** | Gitignore-aware directory listing | `crates/goose/src/agents/platform_extensions/developer/tree.rs` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **MOIM Context Injection** | Per-turn ephemeral context | `crates/goose/src/agents/moim.rs` |
| **MOIM Assembly** | Timestamp, CWD, token %, extension contributions | `crates/goose/src/agents/extension_manager.rs:~1593` |
| **SQLite Storage** | sqlx with WAL mode, migration-versioned schema | `crates/goose/src/session/session_manager.rs` |
| **Recipe System** | YAML/JSON with typed parameters, sub-recipes | `crates/goose/src/recipe/mod.rs` |
| **Auto-Compaction** | Progressive summarization with 80% threshold | `crates/goose/src/context_mgmt/mod.rs` |
| **Chatrecall Extension** | Cross-session keyword search | `crates/goose/src/agents/platform_extensions/chatrecall.rs` |
| **Token Tracking** | Byte-based estimation with reactive compaction | `crates/goose/src/token_counter.rs` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Turn-Based Rust Loop** | `reply_internal` with `turns_taken` counter | `crates/goose/src/agents/agent.rs:1125-1656` |
| **Max Turns Limit** | Hard-coded at 1000 | `crates/goose/src/agents/agent.rs:62` |
| **Async Tool Execution** | `stream::select_all` for concurrent tools | `crates/goose/src/agents/agent.rs:1270-1402` |
| **No Repair Loop** | Error injected as user message | `crates/goose/src/agents/agent.rs:1463-1473` |
| **Tool Inspection Pipeline** | 3-layer: Security → Permission → Repetition | `crates/goose/src/tool_inspection.rs` |
| **Context Compaction** | Automatic when token usage exceeds threshold | `crates/goose/src/context_mgmt/mod.rs` |
| **Lead-Worker Model** | Expensive model first, cheap model after | `crates/goose/src/lead_worker.rs` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **3-Layer Inspection Pipeline** | Security → Permission → Repetition | `crates/goose/src/tool_inspection.rs` |
| **ToolInspector Trait** | Pluggable inspectors | `crates/goose/src/tool_inspection.rs` |
| **SecurityInspector** | Prompt injection detection | `crates/goose/src/security/security_inspector.rs` |
| **PermissionInspector** | Allow/deny/ask per tool | `crates/goose/src/permission/permission_inspector.rs` |
| **RepetitionInspector** | Detects consecutive identical calls | `crates/goose/src/tool_monitor.rs` |
| **Four Operational Modes** | Auto, Approve, SmartApprove, Chat | `crates/goose/src/config/goose_mode.rs` |
| **Permission Persistence** | YAML-based with runtime journal | `crates/goose/src/config/permission.rs` |
| **SmartApprove** | LLM classifies tool as read-only | `crates/goose/src/permission/permission_judge.rs` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **TUI + Desktop** | Tauri-based (same as AVA) | `ui/`, `crates/goose-cli/` |
| **Rust Performance** | Fast startup, low memory | Rust architecture |
| **MCP-Native Extensions** | All extensions are MCP servers | `crates/goose/src/agents/extension.rs` |
| **Recipe Sharing** | Via deeplinks | `crates/goose/src/recipe_deeplink.rs` |
| **Local Inference** | llama.cpp bindings | `crates/goose/src/providers/` |
| **7 Extension Config Variants** | Sse, Stdio, Builtin, Platform, StreamableHttp, Frontend, InlinePython | `crates/goose/src/agents/extension.rs` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **MCP-First Design** | Extensions ARE MCP servers | `crates/goose/src/agents/extension.rs` |
| **Same Stack as AVA** | Rust + Tauri | Entire codebase |
| **3-Layer Inspection Pipeline** | Security → Permission → Repetition | `crates/goose/src/tool_inspection.rs` |
| **MOIM Context Injection** | Per-turn ephemeral context | `crates/goose/src/agents/moim.rs` |
| **Recipe System** | YAML workflows with sub-recipes | `crates/goose/src/recipe/mod.rs` |
| **Lead-Worker Model** | Model routing based on turn count | `crates/goose/src/lead_worker.rs` |
| **SmartApprove** | LLM-based read-only classification | `crates/goose/src/permission/permission_judge.rs` |
| **Auto-Compaction** | 3 continuation modes (Pause, Continue, Exit) | `crates/goose/src/context_mgmt/mod.rs` |

---

## Worth Stealing (for AVA)

### High Priority

1. **MCP-First Architecture** (`crates/goose/src/agents/extension.rs`)
   - Extensions ARE MCP servers
   - Simplest extension model
   - Should adopt for AVA's extension system

2. **3-Layer Inspection Pipeline** (`crates/goose/src/tool_inspection.rs`)
   - Security → Permission → Repetition
   - Typed `InspectionResult` with escalation-only merging
   - Clean, composable design

3. **MOIM Context Injection** (`crates/goose/src/agents/moim.rs`)
   - Per-turn ephemeral context
   - Without polluting history

### Medium Priority

4. **Recipe System** (`crates/goose/src/recipe/mod.rs`)
   - YAML workflows with typed params
   - Sub-recipes, cron scheduling

5. **Lead-Worker Model** (`crates/goose/src/lead_worker.rs`)
   - Expensive model first N turns
   - Automatic fallback on failure

6. **SmartApprove** (`crates/goose/src/permission/permission_judge.rs`)
   - LLM classifies tool as read-only
   - Reduces unnecessary prompts

7. **Env Var Security Denylist** (`crates/goose/src/agents/extension.rs`)
   - 31 blocked env vars
   - Prevents PATH/LD_PRELOAD hijacking

### Lower Priority

8. **Local Inference** — Nice-to-have but not critical
9. **Auto-Compaction Modes** — AVA's compaction is simpler
10. **Recipe Deeplinks** — Sharing mechanism

---

## AVA Already Has (or Matches)

| Goose Feature | AVA Equivalent | Status |
|---------------|----------------|--------|
| Rust + Tauri | Rust + Tauri | ✅ Same stack |
| MCP support | Full MCP client | ✅ Parity |
| Tool inspection | Middleware pipeline | ✅ Similar |
| Permission system | Permissions extension | ✅ Parity |
| Context injection | Context management | ✅ Different approach |
| Recipe system | Skills | ✅ Similar |
| Auto-compaction | Token compaction | ✅ Parity |
| Local inference | (Not implemented) | ❌ Gap |
| 3-layer inspection | Single middleware pipeline | ⚠️ Could adopt 3-layer |

---

## Anti-Patterns to Avoid

1. **No Sandboxing** — Goose has no sandbox; AVA should maintain Docker option
2. **Electron Confusion** — Documentation incorrectly lists Goose as Tauri; it actually uses Tauri
3. **MOIM Complexity** — Per-turn context injection adds complexity
4. **Recipe Limitations** — No runtime parameter validation
5. **SmartApprove Fail-Open** — LLM errors default to ALLOW; should default to ASK

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **Enhanced MCP Integration** — Better MCP server lifecycle
- **Improved Permission System** — Better SmartApprove accuracy
- **Recipe System Expansion** — More recipe types
- **Context Management Refinements** — Better compaction strategies

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `crates/goose/src/agents/agent.rs` | ~1,288 | Core agent loop |
| `crates/goose/src/tool_inspection.rs` | ~200 | 3-layer inspection pipeline |
| `crates/goose/src/agents/moim.rs` | ~100 | MOIM context injection |
| `crates/goose/src/recipe/mod.rs` | ~500 | Recipe system |
| `crates/goose/src/lead_worker.rs` | ~745 | Lead-worker model routing |
| `crates/goose/src/agents/extension.rs` | ~276 | MCP-first extension system |
| `crates/goose/src/context_mgmt/mod.rs` | ~828 | Auto-compaction |
| `crates/goose/src/agents/platform_extensions/developer/edit.rs` | ~153 | Edit tool |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
