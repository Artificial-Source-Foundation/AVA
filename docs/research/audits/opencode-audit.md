# OpenCode Deep Audit

> Comprehensive analysis of SST's OpenCode AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/opencode/`

---

## Overview

OpenCode is a TypeScript/Bun-based AI coding assistant that pioneered several innovative techniques. Its core differentiator is **dynamic provider loading** supporting 75+ LLM providers through bundled SDKs, external registry (models.dev), and dynamic npm installation. OpenCode implements **shadow git snapshots** — a separate git repository at `$DATA_DIR/snapshot/$PROJECT_ID` for point-in-time filesystem capture and rollback without polluting project history. The architecture features **git worktree isolation** for parallel agent sessions, a **15+ plugin hook system** allowing extensions to mutate tool descriptions and intercept lifecycle events, and **30+ LSP servers** for comprehensive language support. The session management uses **Drizzle ORM + SQLite** with a tree-structured session format supporting branching conversations.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **9 Replacer Strategies** | Cascading chain: Simple → LineTrimmed → BlockAnchor → WhitespaceNormalized → IndentationFlexible → EscapeNormalized → TrimmedBoundary → ContextAware → MultiOccurrence | `packages/opencode/src/tool/edit.ts` |
| **Custom Patch Format** | `*** Begin Patch` / `*** End Patch` envelope | `packages/opencode/src/tool/apply_patch.ts` |
| **Fuzzy Matching** | Levenshtein distance with tunable thresholds | `packages/opencode/src/tool/edit.ts` |
| **Multi-Edit Tool** | Batched sequential edits | `packages/opencode/src/tool/multiedit.ts` |
| **LSP Integration** | Diagnostics after every edit | `packages/opencode/src/lsp/` |
| **Unicode Normalization** | Smart quotes, dashes, special spaces | `packages/opencode/src/patch/index.ts` |
| **File Staleness Guard** | `FileTime` with 50ms tolerance | `packages/opencode/src/file/time.ts` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Drizzle ORM + SQLite** | Relational session storage | `packages/opencode/src/db/` |
| **Tree Sessions** | JSONL with DAG structure for branching | `packages/opencode/src/session/` |
| **Auto-Compaction** | Token threshold with pruning | `packages/opencode/src/session/processor.ts` |
| **Plugin Hooks** | 15+ hook points for extensions | `packages/opencode/src/hooks.ts` |
| **Hono HTTP Server** | REST API for sessions | `packages/opencode/src/server/` |
| **Shadow Git Snapshots** | Isolated rollback system | `packages/opencode/src/snapshot/index.ts` |
| **Protected-Tool Compaction** | Compacts around protected tools | `packages/opencode/src/session/processor.ts` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Custom Outer Loop** | `SessionPrompt.loop()` with `while(true)` | `packages/opencode/src/session/processor.ts` |
| **Vercel AI SDK** | `generateText` / `streamText` with `maxSteps` | Provider integration |
| **Flat Agents** | 7 agents: build, plan, general, explore, compaction, title, summary | `packages/opencode/src/agent/agent.ts` |
| **Doom Loop Detection** | 3 identical consecutive calls | `packages/opencode/src/session/processor.ts` |
| **Auto Context Compaction** | Pruning with summarization | `packages/opencode/src/session/processor.ts` |
| **Tool Call Repair** | Fixes malformed tool calls | `packages/opencode/src/session/processor.ts` |
| **Retry with Backoff** | Exponential backoff for API errors | Provider layer |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Tree-Sitter Bash Parsing** | Structural command analysis | `packages/opencode/src/tool/bash.ts` |
| **Wildcard Permission Model** | `allow`/`ask`/`deny` per pattern | `packages/opencode/src/permission/next.ts` |
| **Git Worktrees** | Per-session isolation | `packages/opencode/src/worktree/index.ts` |
| **Shadow Git Checkpoints** | Snapshot before/after edits | `packages/opencode/src/snapshot/index.ts` |
| **`.opencodeignore`** | File exclusion patterns | Configuration |
| **BashArity** | Command prefix to arity mapping | `packages/opencode/src/permission/arity.ts` |
| **External Directory Protection** | Prevents out-of-project access | `packages/opencode/src/tool/external-directory.ts` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Bun Runtime** | Fast TypeScript execution | Bun ecosystem |
| **TUI Interface** | Terminal UI (not Go/Elm as claimed) | `packages/opencode/src/tui/` |
| **75+ Providers** | Via bundled SDKs + models.dev registry | `packages/opencode/src/provider/` |
| **Plugin Hooks** | `onToolCall`, `onMessage`, `onError` | `packages/opencode/src/hooks.ts` |
| **Git Worktrees UI** | Per-session branch management | `packages/opencode/src/worktree/index.ts` |
| **LSP Integration** | 30+ language servers | `packages/opencode/src/lsp/` |
| **ACP Protocol** | Agent Client Protocol REST server | `packages/opencode/src/acp/` |
| **Enterprise Config** | SSO, audit logging, custom models | `packages/opencode/src/config/` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **Dynamic Provider Loading** | 75+ providers via SDKs + registry + npm | `packages/opencode/src/provider/` |
| **Shadow Git Snapshots** | Isolated rollback repos | `packages/opencode/src/snapshot/index.ts` |
| **Git Worktree Isolation** | Per-session worktrees | `packages/opencode/src/worktree/index.ts` |
| **15+ Plugin Hooks** | Extensive extension points | `packages/opencode/src/hooks.ts` |
| **30+ LSP Servers** | Comprehensive language support | `packages/opencode/src/lsp/` |
| **Typed Event Bus** | Pub/sub with typed events | `packages/opencode/src/event.ts` |
| **Enterprise Config** | SSO, audit, custom models | `packages/opencode/src/config/` |
| **LLM Agent Generation** | Generates agents from descriptions | `packages/opencode/src/agent/generate.ts` |
| **Hono HTTP Server** | REST API | `packages/opencode/src/server/` |
| **Protected-Tool Compaction** | Smart context management | `packages/opencode/src/session/processor.ts` |

---

## Worth Stealing (for AVA)

### High Priority

1. **Shadow Git Snapshots** (`packages/opencode/src/snapshot/index.ts`)
   - Isolated rollback repos
   - No project history pollution
   - Should add to AVA's git integration

2. **Dynamic Provider Loading** (`packages/opencode/src/provider/`)
   - Bundled SDKs + external registry + dynamic npm
   - Better than AVA's static provider list

3. **Git Worktree Isolation** (`packages/opencode/src/worktree/index.ts`)
   - Per-session worktrees
   - Clean isolation for parallel agents

### Medium Priority

4. **15+ Plugin Hooks** (`packages/opencode/src/hooks.ts`)
   - Extensive extension points
   - Tool mutation, lifecycle interception

5. **Typed Event Bus** (`packages/opencode/src/event.ts`)
   - Type-safe pub/sub
   - Better than generic event emitters

6. **BashArity** (`packages/opencode/src/permission/arity.ts`)
   - Command prefix to arity mapping
   - Better permission patterns

### Lower Priority

7. **Enterprise Config** — Only needed for enterprise deployments
8. **LLM Agent Generation** — Nice-to-have feature
9. **30+ LSP Servers** — AVA already has 9 LSP tools
10. **Hono Server** — AVA uses different architecture

---

## AVA Already Has (or Matches)

| OpenCode Feature | AVA Equivalent | Status |
|------------------|----------------|--------|
| 9 edit replacers | 8 strategies | ✅ Parity |
| LSP integration | 9 LSP tools | ✅ Parity |
| Git snapshots | Git snapshots, ghost checkpoints | ✅ Better |
| Context compaction | Token compaction | ✅ Parity |
| Plugin hooks | ExtensionAPI hooks | ✅ Parity |
| Multi-provider | 16 providers | ⚠️ Should adopt dynamic loading |
| Worktrees | (Not implemented) | ❌ Gap |
| Shadow snapshots | (Not implemented) | ❌ Gap |
| 75+ providers | 16 providers | ❌ Gap |
| TypeScript | TypeScript | ✅ Same |

---

## Anti-Patterns to Avoid

1. **Bun Coupling** — Tied to Bun runtime; AVA's Node compatibility is better
2. **External Registry SPOF** — models.dev dependency; AVA should support offline
3. **Shadow Git Complexity** — Separate repos add complexity
4. **Mislabeled Pub/Sub** — Documentation calls it Elm-like; it's standard pub/sub
5. **Experimental Hook Proliferation** — Too many experimental hooks; keep stable API

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **Enhanced LSP Integration** — Better language server management
- **Improved Provider Discovery** — Better models.dev integration
- **Plugin System Expansion** — More hook points
- **Enterprise Features** — SSO improvements

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `packages/opencode/src/tool/edit.ts` | ~650 | Edit tool with 9 replacers |
| `packages/opencode/src/snapshot/index.ts` | 297 | Shadow git snapshots |
| `packages/opencode/src/worktree/index.ts` | 643 | Git worktree isolation |
| `packages/opencode/src/permission/next.ts` | 286 | Permission engine |
| `packages/opencode/src/session/processor.ts` | ~1,000 | Agent loop |
| `packages/opencode/src/lsp/` | ~2,000 | LSP integration |
| `packages/opencode/src/provider/` | ~1,500 | Provider loading |
| `packages/opencode/src/hooks.ts` | ~200 | Plugin hooks |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
