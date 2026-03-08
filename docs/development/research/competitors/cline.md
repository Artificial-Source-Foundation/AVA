# Cline

> VS Code extension AI coding assistant (~58k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Cline is a **VS Code extension** built on a monolithic architecture with a 3,547-line `Task` class containing the full agent loop. The architecture is built around **gRPC-over-postMessage** — using protobuf for type safety while tunneling over VS Code's native message passing.

**Key architectural decisions:**
- **Monolithic Task class** — Everything in one class: agent loop, streaming, tool execution, context management
- **Two-phase tool execution** — `handlePartialBlock()` for streaming UI updates + `execute()` for actual execution
- **40+ LLM providers** — Individual handlers per provider with unified `ApiHandler` interface
- **Shadow git checkpoints** — Isolated git repos for rollback without polluting user's history

### Project Structure

```
cline/
├── src/
│   ├── core/
│   │   ├── task/              # THE MAIN CLASS (3,547 lines)
│   │   ├── api/               # 40+ provider handlers
│   │   ├── prompts/           # Model-variant system prompts
│   │   └── context/           # Context management (~1,200 lines)
│   ├── services/
│   │   ├── browser/           # Puppeteer automation
│   │   └── mcp/               # MCP client with OAuth
│   └── integrations/
│       ├── checkpoints/       # Shadow git system
│       └── editor/            # VS Code diff editor
├── proto/                     # 16 protobuf definitions
└── webview-ui/                # React UI
```

---

## Key Patterns

### 1. Two-Phase Tool Execution

Cline's signature UX pattern — streaming partial tool UI during LLM generation:

```typescript
interface IToolHandler {
    handlePartialBlock(block, uiHelpers)  // Called during streaming
    execute(block)                        // Called when complete
}
```

For file edits, this opens VS Code's diff editor and streams content character-by-character while the LLM is still generating. This is Cline's crown jewel — impossible to replicate in CLI agents.

### 2. Three-Strategy Edit System

- **`replace_in_file`** — SEARCH/REPLACE blocks with 3-tier fuzzy matching (exact → line-trimmed → block-anchor)
- **`write_to_file`** — Complete file replacement for new files
- **`apply_patch`** — Unified diffs with 4-pass matcher (Levenshtein 66% threshold, Unicode canonicalization)

### 3. 40+ Provider Architecture

Simple handler interface with no shared base class:

```typescript
interface ApiHandler {
    createMessage(systemPrompt, messages, tools?): ApiStream
    getModel(): ApiHandlerModel
}
```

Each provider (Anthropic, OpenRouter, Bedrock, etc.) implements this interface directly. No abstraction layer beyond the interface.

### 4. Deleted-Range Context Management

Novel approach to context truncation:
- Maintains `[start, end]` range of messages to exclude
- Preserves first user-assistant pair always
- Messages excluded from API calls but kept on disk
- Dual-mode: programmatic truncation + auto-condense at 75% capacity

### 5. Shadow Git Checkpoints

```
~/.cline/data/checkpoints/{cwdHash}/
```

Isolated git repo per workspace:
- Captures state after each tool execution
- User can view diffs between any checkpoints
- Restore to any previous state
- No pollution of user's git history

---

## What AVA Can Learn

### High Priority

1. **Two-Phase Tool Execution** — Implement `handlePartial()` + `execute()` for tools. Tauri's webview can render custom diff views for streaming edits.

2. **3-Tier Fuzzy Matching** — Add block-anchor matching with Levenshtein similarity to AVA's edit cascade.

3. **Shadow Git Checkpoints** — Isolated rollback repos are cleaner than ghost commits in user's repo.

### Medium Priority

4. **Auto-Formatting Detection** — Report auto-formatting changes back to model to prevent cascading match failures.

5. **40+ Provider Support** — Use OpenRouter/LiteLLM as gateway instead of individual handlers.

6. **Focus Chain / Todo** — Persistent progress tracking across context compaction.

### Patterns to Avoid

- **Monolithic Task class** — Unmaintainable at 3,547 lines; AVA's modular architecture is correct
- **VS Code lock-in** — Limits deployment options; Tauri desktop is more flexible
- **Manual locking booleans** — Use proper mutex primitives
- **Single active task** — No concurrent execution

---

## Comparison: Cline vs AVA

| Capability | Cline | AVA |
|------------|-------|-----|
| **Platform** | VS Code only | Desktop (Tauri) + CLI |
| **Streaming diff** | Native VS Code diff | Possible via Tauri |
| **Architecture** | Monolithic (3,547 lines) | Modular (29 modules) |
| **Edit strategies** | 3 strategies | 8 strategies |
| **Provider count** | 40+ | ~16 |
| **Checkpoints** | Shadow git | Git snapshots |
| **Subagents** | 5 parallel runners | 13 agents, 3-tier hierarchy |
| **MCP** | Full with OAuth | Full client |
| **Hooks** | 8 lifecycle hooks | ExtensionAPI hooks |
| **LSP** | Tree-sitter only | Full LSP integration |

---

## File References

| File | Lines | Purpose |
|------|-------|---------|
| `src/core/task/index.ts` | 3,547 | Core Task class |
| `src/core/assistant-message/diff.ts` | ~400 | Edit strategies |
| `src/integrations/checkpoints/` | ~400 | Shadow git system |
| `src/core/context/context-management/` | ~1,200 | Context manager |

---

*Consolidated from: audits/cline-audit.md, cline/*.md, backend-analysis/cline.md, backend-analysis/cline-detailed.md*
