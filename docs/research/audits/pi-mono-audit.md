# Pi Mono Deep Audit

> Comprehensive analysis of Pi Mono AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/pi-mono/`

---

## Overview

Pi Mono is an AI coding assistant distinguished by its **session DAG with tree navigation** — an append-only JSONL format where every entry has `id` and `parentId` fields forming a persistent tree (DAG). A `leafId` pointer tracks the current position; branching is achieved by moving the leaf to an earlier entry and appending new children. Pi Mono pioneered **auto-compaction** that triggers when estimated tokens exceed `contextWindow - reserveTokens` (default 16,384 reserve), producing LLM-generated structured summaries. It implements **cross-provider normalization** via `transformMessages()` that rewrites thinking blocks, normalizes tool-call IDs, and synthesizes orphaned tool results. The **3-layer loop architecture** separates concerns: `ai` (raw LLM), `agent` (tool loop), and `coding-agent` (file context). A unique feature is **steering interrupts** — the ability to skip pending tools via `skipToolCall()` and inject follow-up messages via `getFollowUpMessages()`.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **oldText/newText Substitution** | No line-range parameters | `packages/coding-agent/src/core/tools/edit.ts` |
| **Two-Tier Matching** | Exact match first, then fuzzy normalization | `packages/coding-agent/src/core/tools/edit-diff.ts` |
| **Unicode Normalization** | Smart quotes, dashes, special whitespace | `packages/coding-agent/src/core/tools/edit-diff.ts` |
| **Path-Level NFC/NFD** | macOS HFS+/APFS compatibility | `packages/coding-agent/src/core/tools/path-utils.ts` |
| **No Configurable Strategies** | Single deterministic pipeline | `packages/coding-agent/src/core/tools/edit.ts` |
| **BOM Preservation** | UTF-8 BOM handling | `packages/coding-agent/src/core/tools/edit-diff.ts` |
| **Line Ending Normalization** | CRLF/CR → LF, then restore | `packages/coding-agent/src/core/tools/edit-diff.ts` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Session DAG** | Append-only JSONL with tree structure | `packages/coding-agent/src/core/session-manager.ts` |
| **Tree Navigation** | `getTree()`, `getBranch()`, `branch()` | `packages/coding-agent/src/core/session-manager.ts` |
| **Non-Destructive Branching** | Move leaf pointer, append children | `packages/coding-agent/src/core/session-manager.ts` |
| **Auto-Compaction** | Triggers at `contextWindow - reserveTokens` | `packages/coding-agent/src/core/compaction/compaction.ts` |
| **Structured Summary** | Goal/Constraints/Progress/Key Decisions/Next Steps/Critical Context | `packages/coding-agent/src/core/compaction/compaction.ts` |
| **Token Counting** | Chars÷4 heuristic + real Usage data | `packages/coding-agent/src/core/compaction/compaction.ts` |
| **Cross-Provider Normalization** | `transformMessages()` single-pass | `packages/ai/src/providers/transform-messages.ts` |
| **NFC/NFD Handling** | macOS path compatibility | `packages/ai/src/providers/transform-messages.ts` |
| **Thinking Block Handling** | Same-model replay, cross-model text conversion | `packages/ai/src/providers/transform-messages.ts` |
| **Tool Call ID Normalization** | OpenAI → Anthropic compatible | `packages/ai/src/providers/transform-messages.ts` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **3-Layer Loop** | `ai` → `agent` → `coding-agent` | `packages/agent/src/`, `packages/coding-agent/src/` |
| **Steering Interrupts** | Skip pending tools via `skipToolCall()` | `packages/agent/src/agent.ts` |
| **Follow-Up Messages** | `getFollowUpMessages()` queue | `packages/agent/src/agent.ts` |
| **Abort Capability** | `AbortController`-based, multi-level | `packages/agent/src/agent.ts` |
| **No maxSteps** | Unbounded loop | Not implemented |
| **Dual Queue Model** | Steering + follow-up queues | `packages/agent/src/agent.ts` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **No Built-in Sandboxing** | Direct execution | N/A |
| **Optional Docker** | `--sandbox=docker` for Slack bot | `packages/mom/src/sandbox.ts` |
| **Permission-Gate Extension** | Example (not default) for dangerous commands | `packages/coding-agent/examples/extensions/permission-gate.ts` |
| **Plan-Mode Extension** | Example (not default) for read-only mode | `packages/coding-agent/examples/extensions/plan-mode/index.ts` |
| **Extension Hook** | `tool_execution_start` for interception | `packages/coding-agent/src/core/extensions/` |
| **Output Truncation** | ~50KB / 500 lines cap | `packages/coding-agent/src/core/tools/truncate.ts` |
| **Process Timeout** | Cross-platform process tree killing | `packages/coding-agent/src/utils/shell.ts` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Custom TUI Framework** | Differential rendering, no Ink/Blessed | `packages/tui/src/tui.ts` |
| **Synchronized ANSI Output** | `\x1b[?2026h/l` for flicker-free | `packages/tui/src/tui.ts` |
| **Kitty + Legacy Keyboard** | Dual-protocol parsing | `packages/tui/src/keys.ts` |
| **Configurable Keybindings** | JSON config at `~/.pi/agent/keybindings.json` | `packages/tui/src/keybindings.ts` |
| **Append-Only Session Tree** | DAG with `id`/`parentId` | `packages/coding-agent/src/core/session-manager.ts` |
| **ASCII Tree Selector** | Iterative DFS, 5 filter modes | `packages/coding-agent/src/modes/interactive/components/tree-selector.ts` |
| **Session Selector** | Regex/exact/fuzzy search | `packages/coding-agent/src/modes/interactive/components/session-selector.ts` |
| **Theme System** | 50+ color tokens, truecolor/256 fallback | `packages/coding-agent/src/modes/interactive/theme/theme.ts` |
| **35 Interactive Components** | Tree selector, session selector, footer, etc. | `packages/coding-agent/src/modes/interactive/components/` |
| **Extension Lifecycle Hooks** | `session_before_compact`, `session_before_switch`, etc. | `packages/coding-agent/src/core/agent-session.ts` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **Session DAG** | Append-only JSONL with tree navigation | `packages/coding-agent/src/core/session-manager.ts` |
| **Non-Destructive Branching** | Move leaf pointer, preserve history | `packages/coding-agent/src/core/session-manager.ts` |
| **Auto-Compaction** | Token threshold with structured summaries | `packages/coding-agent/src/core/compaction/compaction.ts` |
| **Cross-Provider Normalization** | Single-pass `transformMessages()` | `packages/ai/src/providers/transform-messages.ts` |
| **Steering Interrupts** | Skip pending tools, inject follow-ups | `packages/agent/src/agent.ts` |
| **Differential TUI Rendering** | Synchronized ANSI output | `packages/tui/src/tui.ts` |
| **Declaration Merging** | Extensible types via TypeScript | Type definitions |
| **EventStream Generic** | Type-safe event streaming | `packages/ai/src/types.ts` |
| **Extension SDK** | Lifecycle hooks + UI primitives | `packages/coding-agent/src/core/extensions/` |
| **API Registry** | 22+ providers with compat flags | `packages/ai/src/api-registry.ts` |

---

## Worth Stealing (for AVA)

### High Priority

1. **Session DAG** (`packages/coding-agent/src/core/session-manager.ts`)
   - Append-only JSONL with tree structure
   - Non-destructive branching
   - Clean, crash-safe model

2. **Cross-Provider Normalization** (`packages/ai/src/providers/transform-messages.ts`)
   - Single-pass `transformMessages()`
   - Handles thinking blocks, tool IDs, orphaned results

3. **Steering Interrupts** (`packages/agent/src/agent.ts`)
   - Skip pending tools via `skipToolCall()`
   - Follow-up message queue

### Medium Priority

4. **Auto-Compaction** (`packages/coding-agent/src/core/compaction/compaction.ts`)
   - Token threshold with structured summaries
   - Split-turn handling

5. **Differential TUI Rendering** (`packages/tui/src/tui.ts`)
   - Synchronized ANSI output
   - Flicker-free updates

6. **Extension Lifecycle Hooks** (`packages/coding-agent/src/core/agent-session.ts`)
   - `session_before_compact`, `session_before_switch`, etc.
   - Cancellable hooks

### Lower Priority

7. **Custom TUI Framework** — AVA uses Tauri + SolidJS
8. **Theme System** — Good for TUI mode only
9. **35 Interactive Components** — Desktop app has different needs
10. **API Registry** — AVA's provider system is different

---

## AVA Already Has (or Matches)

| Pi Mono Feature | AVA Equivalent | Status |
|-----------------|----------------|--------|
| Session DAG | DAG session structure | ✅ Parity |
| Auto-compaction | Token compaction | ✅ Parity |
| Cross-provider normalization | (Not implemented) | ❌ Gap |
| Steering interrupts | (Not implemented) | ❌ Gap |
| Edit with fuzzy matching | Fuzzy matching in edit cascade | ✅ Parity |
| Extension hooks | ExtensionAPI hooks | ✅ Parity |
| 3-layer loop | Extension/middleware pattern | ✅ Different approach |
| TUI | Tauri desktop app | ✅ Different approach |
| TypeScript | TypeScript | ✅ Same |

---

## Anti-Patterns to Avoid

1. **No Built-in Sandboxing** — Pi Mono executes directly; AVA should maintain Docker
2. **Optional Docker Only in Slack Bot** — Inconsistent security model
3. **Permission Extensions as Examples** — Not enabled by default
4. **Unbounded Loop** — No maxSteps limit; could run forever
5. **Custom TUI Complexity** — Building UI framework from scratch is expensive

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **Enhanced Cross-Provider Support** — Better provider normalization
- **Improved Session Management** — Better branching UX
- **Extension SDK Improvements** — More hook points
- **TUI Optimizations** — Better rendering performance

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `packages/coding-agent/src/core/session-manager.ts` | ~1,000 | Session DAG |
| `packages/ai/src/providers/transform-messages.ts` | ~300 | Cross-provider normalization |
| `packages/agent/src/agent.ts` | ~800 | Agent loop with steering |
| `packages/coding-agent/src/core/compaction/compaction.ts` | ~400 | Auto-compaction |
| `packages/tui/src/tui.ts` | ~500 | Differential rendering |
| `packages/coding-agent/src/core/tools/edit.ts` | 227 | Edit tool |
| `packages/coding-agent/src/core/tools/edit-diff.ts` | 308 | Fuzzy matching |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
