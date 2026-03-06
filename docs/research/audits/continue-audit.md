# Continue Deep Audit

> Comprehensive analysis of Continue's AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/continue/`

---

## Overview

Continue is an **IDE-agnostic AI coding assistant** with a unique three-process architecture: IDE ↔ Core ↔ LLM. Its core differentiator is a sophisticated **context provider system** with 30+ built-in providers (files, codebase embeddings, URLs, docs, MCP resources, git diffs, terminal output) all implementing a unified `IContextProvider` interface. Continue pioneered the **tab autocomplete** feature with a separate context pipeline using **token-aware prefix/suffix windowing** that prunes content around the cursor position. The edit system supports **two strategies**: whole-file and diff-based, with streaming diff application. The architecture cleanly separates concerns through a typed messenger protocol with ~40 IDE capabilities exposed. Continue implements a **tool policy system** with per-tool allow/deny/ask configuration via YAML.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Two Edit Strategies** | Whole-file and diff-based | `core/tools/definitions/editFile.ts` |
| **Streaming Diff Application** | Applies diffs as LLM streams | `core/edit/streamDiffLines.ts` |
| **Online Diff Algorithm** | `streamDiff()` with Levenshtein matching | `core/diff/streamDiff.ts` |
| **Lazy Apply System** | Deterministic AST-based or LLM-powered | `core/edit/lazy/` |
| **IDE-Agnostic Core** | Tool definitions in `core/`, rendering in IDE extensions | `core/tools/definitions/` |
| **7-Layer Architecture** | Tool def → selection → dispatch → Redux → apply → diff → render | Various files |
| **Myers Diff** | Non-streaming complete diff | `core/diff/myers.ts` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Context Providers System** | 30+ providers, unified `IContextProvider` | `core/context/providers/index.ts` |
| **Extensible Context** | Files, symbols, URLs, custom providers | `core/context/providers/` |
| **Prefix/Suffix Windowing** | Token-aware for autocomplete | `core/autocomplete/util/HelperVars.ts` |
| **Three-Process Model** | IDE ↔ Core ↔ LLM via typed protocol | `core/protocol/` |
| **Conversation Compaction** | LLM-generated incremental summaries | `core/util/conversationCompaction.ts` |
| **Context Providers** | Files, symbols, URLs, docs, MCP, git, terminal | `core/context/providers/` |
| **Autocomplete Pipeline** | 8+ parallel snippet sources | `core/autocomplete/snippets/getAllSnippets.ts` |
| **Retrieval Pipeline** | FTS + embeddings + reranking | `core/context/retrieval/pipelines/` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **GUI-Driven Step Loop** | Redux thunk chain | `gui/src/redux/thunks/streamResponse.ts` |
| **Three-Process Architecture** | IDE, Core, LLM processes | `core/protocol/` |
| **Protocol Between Processes** | ~100 message types | `core/protocol/core.ts`, `core/protocol/ide.ts` |
| **Tool Policy System** | Per-tool allow/deny/ask | `gui/src/redux/thunks/evaluateToolPolicies.ts` |
| **Error Handling** | Never-throw, feed-back-to-LLM | `core/tools/callTool.ts` |
| **Retry with Backoff** | 3 retries with exponential backoff | `gui/src/redux/thunks/streamThunkWrapper.tsx` |
| **18 Built-in Tools** | Tool registry | `core/tools/builtIn.ts` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Tool Policy System** | YAML-configured per-tool policies | `core/tools/policies/fileAccess.ts` |
| **Per-Tool Allow/Deny/Ask** | Three-tier permission model | `gui/src/redux/thunks/evaluateToolPolicies.ts` |
| **IDE-Mediated Execution** | Execution through IDE extension | IDE extensions |
| **Terminal Command Security** | 1,241-line defense-in-depth | `packages/terminal-security/src/evaluateTerminalCommandSecurity.ts` |
| **Context Provider Permissions** | Org-level toggles | `packages/config-yaml/src/schemas/policy.ts` |
| **No Sandboxing** | Relies on IDE for isolation | N/A |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **IDE-Agnostic Design** | VS Code + JetBrains support | `extensions/vscode/`, `extensions/intellij/` |
| **React Webview GUI** | Shared UI across IDEs | `gui/` |
| **Tab Autocomplete** | Inline edit suggestions | `core/autocomplete/` |
| **Context Provider UI** | @-mention system | `gui/src/components/` |
| **MCP Support** | MCP client for tools | `core/context/mcp/` |
| **Slash Commands** | Extensible command system | `core/commands/` |
| **15+ Languages** | i18n support | `gui/public/locales/` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **IDE-Agnostic Core** | Same core runs in VS Code and JetBrains | `core/` |
| **Three-Process Model** | Clean separation via typed protocol | `core/protocol/` |
| **Context Providers** | 30+ providers with unified interface | `core/context/providers/` |
| **Tab Autocomplete** | Separate context pipeline | `core/autocomplete/` |
| **NextEdit Prediction** | Predicts next edit before user asks | `core/edit/` |
| **7-Layer Architecture** | Clean separation of concerns | Various files |
| **Terminal Security Evaluator** | Deep shell parsing and classification | `packages/terminal-security/` |

---

## Worth Stealing (for AVA)

### High Priority

1. **Context Providers System** (`core/context/providers/`)
   - 30+ providers with unified `IContextProvider` interface
   - Extensible architecture for custom providers
   - Perfect for AVA's extension system

2. **Typed Protocol Architecture** (`core/protocol/`)
   - ~100 message types between processes
   - Clean separation of concerns
   - Should adopt for AVA's frontend-backend communication

3. **Tab Autocomplete** (`core/autocomplete/`)
   - Inline edit suggestions
   - Separate context pipeline
   - High user value feature

4. **Terminal Security Evaluator** (`packages/terminal-security/`)
   - 1,241-line shell command classifier
   - Detects dangerous patterns, injection attacks
   - Should integrate into AVA's permission system

### Medium Priority

5. **Context Provider Abstraction** (`core/context/`)
   - Clean provider interface
   - Priority-based context assembly

6. **Tool Policy System** (`core/tools/policies/`)
   - Per-tool allow/deny/ask
   - YAML configuration

7. **7-Layer Architecture** (`core/edit/`)
   - Clean separation from tool def to rendering
   - Good reference for AVA's edit system

### Lower Priority

8. **Three-Process Model** — AVA's Tauri architecture is simpler
9. **IDE Integration** — AVA is desktop-native, different approach
10. **Autcomplete** — Good feature but requires editor integration

---

## AVA Already Has (or Matches)

| Continue Feature | AVA Equivalent | Status |
|------------------|----------------|--------|
| Multiple edit strategies | 8 strategies | ✅ Parity |
| Streaming diff | Streaming edits | ✅ Parity |
| Context providers | Extension-based tools | ✅ Different approach |
| Tab autocomplete | (Not implemented) | ❌ Gap |
| IDE-agnostic core | Tauri desktop app | ✅ Different approach |
| Tool policy system | Middleware pipeline | ✅ Better |
| MCP support | Full MCP client | ✅ Parity |
| Context compaction | Token compaction | ✅ Parity |
| Terminal security | (Not implemented) | ❌ Gap |

---

## Anti-Patterns to Avoid

1. **Monolithic Core Class** — `core/core.ts` is large; prefer AVA's modular extensions
2. **React in Webview** — Adds complexity; AVA's SolidJS approach is cleaner
3. **No Runtime Extensions** — Continue lacks runtime plugin system; AVA's extensions are better
4. **GUI-Protocol Coupling** — Some coupling between GUI and protocol; keep AVA's cleaner separation
5. **Single Mega Type Definition** — `core/index.d.ts` is large; prefer modular types

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **NextEdit Improvements** — Better prediction accuracy
- **MCP Enhancements** — OAuth support, better integration
- **Context Provider Expansion** — New providers added
- **Autocomplete Refinements** — Better suggestions

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `core/tools/definitions/editFile.ts` | ~100 | Edit tool definition |
| `core/edit/streamDiffLines.ts` | ~200 | Streaming edit orchestrator |
| `core/diff/streamDiff.ts` | ~150 | Online diff algorithm |
| `core/context/providers/index.ts` | ~200 | Provider registry |
| `core/protocol/core.ts` | ~500 | Core protocol types |
| `core/autocomplete/` | ~2,000 | Autocomplete pipeline |
| `packages/terminal-security/` | ~1,241 | Terminal security |
| `gui/src/redux/` | ~3,000 | Redux state management |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
