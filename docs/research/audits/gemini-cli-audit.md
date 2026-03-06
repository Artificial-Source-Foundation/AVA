# Gemini CLI Deep Audit

> Comprehensive analysis of Google's Gemini CLI AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/gemini-cli/`

---

## Overview

Gemini CLI is Google's official AI coding assistant, distinguished by its **event-driven parallel tool scheduler** — a formal state machine that batches read-only tools via `Promise.all` while serializing write tools. It implements a **three-layer loop detection system**: heuristic tool-call repetition (hash matching), streaming content chanting (sliding window), and LLM-as-judge double-check after 40 turns. Its most novel security feature is **Conseca** — a dynamic security policy generator where one LLM generates least-privilege rules and a second LLM enforces them per tool call. Gemini CLI is the first coding agent to ship a full **A2A (Agent-to-Agent) protocol server** with multi-source agent discovery. It leverages **1M token native context window** (all models default to 1,048,576 tokens) with curated/comprehensive dual history views. The **Google Search grounding tool** provides real-time web search with byte-position-accurate citation insertion.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Search-and-Replace Model** | `replace` tool with `old_string`/`new_string` | `packages/core/src/tools/edit.ts` |
| **4-Tier Edit Cascade** | Exact → flexible → regex → fuzzy | `packages/core/src/tools/edit.ts:290-343` |
| **LLM Self-Correction** | `FixLLMEditWithInstruction()` with dedicated LLM | `packages/core/src/utils/llm-edit-fixer.ts` |
| **Omission Placeholder Detection** | Rejects `// ... existing code ...` patterns | `packages/core/src/tools/omissionPlaceholderDetector.ts` |
| **External Editor Integration** | Modify tool arguments in external editor | `packages/core/src/tools/modifiable-tool.ts`, `packages/core/src/utils/editor.ts` |
| **ModifiableDeclarativeTool** | Intercept proposed edits before commit | `packages/core/src/tools/modifiable-tool.ts` |
| **Write File Tool** | Whole-file replacement with content correction | `packages/core/src/tools/write-file.ts` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **1M Native Token Context** | All models default to 1,048,576 tokens | `packages/core/src/core/tokenLimits.ts` |
| **GEMINI.md System** | 3-tier hierarchical memory (global/extension/project) | `packages/core/src/core/context/context.ts` |
| **Memory Save/Load Tools** | `memory_save`/`memory_load` | `packages/core/src/tools/memory.ts` |
| **Dual History Views** | Curated (valid turns) + Comprehensive (all turns) | `packages/core/src/core/geminiChat.ts` |
| **Context Compaction** | Auto-trigger at 50% token usage, gradual summaries | `packages/core/src/core/context/compaction.ts` |
| **Google Search Grounding** | Server-side grounding with citation insertion | `packages/core/src/tools/web-search.ts` |
| **Chat Compression Info** | `COMPRESSED`, `CONTENT_TRUNCATED` statuses | `packages/core/src/core/geminiChat.ts` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Event-Driven Scheduler** | Formal state machine: Validating → Scheduled → Executing → Success/Error/Cancelled | `packages/core/src/scheduler/scheduler.ts` |
| **Parallel Tool Execution** | Batches read-only and Agent-kind tools via `Promise.all` | `packages/core/src/scheduler/scheduler.ts:520-523` |
| **Tail-Call Chaining** | Tool returns `TailToolCallRequest` to chain without LLM round-trip | `packages/core/src/scheduler/types.ts` |
| **Three-Layer Loop Detection** | Tool hash (5x) + content chanting (10x) + LLM judge (after 40 turns) | `packages/core/src/services/loopDetectionService.ts` |
| **Sequential Turn-Based** | One tool at a time (non-parallel write tools) | Core loop |
| **5-Turn Limit** | Hard limit on main loop | Configuration |
| **DeclarativeTool Pattern** | Abstract base class with `Kind` enum | `packages/core/src/tools/tools.ts` |
| **A2A Protocol** | Full Agent-to-Agent server with discovery | `packages/a2a-server/src/` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Policy Engine** | Central rule evaluator with 5-tier hierarchy | `packages/core/src/policy/policy-engine.ts` |
| **5-Tier Priority System** | Default < Extension < Workspace < User < Admin | `packages/core/src/policy/config.ts` |
| **Conseca** | LLM-generated dynamic security policies | `packages/core/src/safety/conseca/` |
| **TOML Policy Loader** | Zod schema validation, ReDoS protection | `packages/core/src/policy/toml-loader.ts` |
| **AllowedPathChecker** | Validates file paths stay in workspace | `packages/core/src/safety/built-in.ts` |
| **Shell Command Security** | Tree-sitter parsing, sub-command evaluation | `packages/core/src/utils/shell-utils.ts` |
| **Policy Persistence** | SHA-256 hashing, atomic file writes | `packages/core/src/policy/config.ts` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **React Ink TUI** | Terminal UI framework | `packages/cli/src/` |
| **Browser Agent** | Chrome DevTools Protocol (not Playwright) | `packages/core/src/browser/` |
| **A2A Protocol** | Cross-agent interoperability | `packages/a2a-server/src/` |
| **Skills System** | Multi-tool extensions with configuration | `packages/core/src/skills/` |
| **Model-Variant Prompts** | Different prompts per model family | `packages/core/src/prompts/` |
| **Hook System** | BeforeModel, AfterModel, BeforeTool, BeforeToolSelection | `packages/core/src/hooks/` |
| **MCP Client Support** | stdio, SSE, HTTP transports | `packages/core/src/mcp/` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **Event-Driven Parallel Scheduler** | State machine with pub/sub, batches parallel tools | `packages/core/src/scheduler/scheduler.ts` |
| **Three-Layer Loop Detection** | Hash + chanting + LLM judge with adaptive intervals | `packages/core/src/services/loopDetectionService.ts` |
| **Conseca Dynamic Policies** | LLM generates AND enforces security policies | `packages/core/src/safety/conseca/` |
| **A2A Protocol Server** | First production A2A implementation | `packages/a2a-server/src/` |
| **1M Token Context** | All models default to 1,048,576 tokens | `packages/core/src/core/tokenLimits.ts` |
| **Google Search Grounding** | Byte-position-accurate citation insertion | `packages/core/src/tools/web-search.ts` |
| **Tail-Call Chaining** | Chain tools without LLM round-trip | `packages/core/src/scheduler/types.ts` |
| **Mid-Confirmation Editing** | Edit tool arguments before approval | `packages/core/src/tools/modifiable-tool.ts` |
| **Dual History Views** | Curated + comprehensive history | `packages/core/src/core/geminiChat.ts` |

---

## Worth Stealing (for AVA)

### High Priority

1. **Three-Layer Loop Detection** (`packages/core/src/services/loopDetectionService.ts`)
   - Tool hash (5x) + content chanting (10x) + LLM judge
   - Adaptive check intervals based on confidence
   - Zero false positives on productive patterns

2. **Conseca Dynamic Policy Generation** (`packages/core/src/safety/conseca/`)
   - LLM generates least-privilege policies
   - Second LLM enforces per tool call
   - Adapts to user intent dynamically

3. **Event-Driven Parallel Scheduler** (`packages/core/src/scheduler/scheduler.ts`)
   - Formal state machine with pub/sub
   - Batches read-only tools
   - Tail-call chaining for efficiency

### Medium Priority

4. **1M Token Context Handling** (`packages/core/src/core/tokenLimits.ts`)
   - Curated/comprehensive dual views
   - Good pattern for large context windows

5. **Google Search Grounding** (`packages/core/src/tools/web-search.ts`)
   - Byte-position-accurate citations
   - Server-side grounding

6. **A2A Protocol** (`packages/a2a-server/src/`)
   - Agent-to-agent interoperability
   - Could enable AVA agent marketplace

7. **Mid-Confirmation Editing** (`packages/core/src/tools/modifiable-tool.ts`)
   - Edit tool arguments before approval
   - Good UX pattern

### Lower Priority

8. **Hook System** — AVA's middleware system is more comprehensive
9. **React Ink TUI** — AVA uses Tauri + SolidJS
10. **Skills System** — Similar to AVA's skills

---

## AVA Already Has (or Matches)

| Gemini CLI Feature | AVA Equivalent | Status |
|-------------------|----------------|--------|
| Loop detection | Doom loop extension | ⚠️ Should upgrade to 3-layer |
| Policy engine | Middleware pipeline | ✅ Different approach |
| Context compaction | Token compaction | ✅ Parity |
| MCP support | Full MCP client | ✅ Parity |
| Parallel tool execution | (Not implemented) | ❌ Gap |
| A2A protocol | (Not implemented) | ❌ Gap |
| Conseca | (Not implemented) | ❌ Gap |
| 1M token context | Supports large contexts | ✅ Parity |
| Hook system | Extension hooks | ✅ Better |

---

## Anti-Patterns to Avoid

1. **Fail-Open Conseca** — Defaults to ALLOW on errors; AVA should default to ASK_USER
2. **React Ink** — Terminal UI limits deployment; AVA's desktop app is better
3. **Google-Only** — Locked to Gemini; AVA's multi-provider approach is better
4. **Complex Policy Hierarchy** — 5 tiers may be overkill; AVA's simpler middleware is cleaner
5. **Dual Scheduler Confusion** — New + legacy schedulers coexist; avoid parallel implementations

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **Improved Conseca** — Better policy generation accuracy
- **Enhanced A2A** — Better agent discovery
- **Browser Agent Improvements** — Better CDP integration
- **Scheduler Optimizations** — Better parallel execution

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `packages/core/src/scheduler/scheduler.ts` | 764 | Parallel tool scheduler |
| `packages/core/src/services/loopDetectionService.ts` | 669 | Three-layer loop detection |
| `packages/core/src/safety/conseca/conseca.ts` | 170 | Dynamic policy system |
| `packages/a2a-server/src/agent/executor.ts` | 616 | A2A protocol server |
| `packages/core/src/tools/edit.ts` | 1,248 | Edit tool with 4-tier cascade |
| `packages/core/src/core/geminiChat.ts` | 1,034 | Chat management |
| `packages/core/src/policy/policy-engine.ts` | ~800 | Policy evaluation |
| `packages/core/src/tools/web-search.ts` | 246 | Google Search grounding |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
