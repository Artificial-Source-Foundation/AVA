# Epic 17: Missing Tool Implementation

> Completed: 2026-02-03

## Goal

Implement all missing tools to achieve feature parity with state-of-the-art AI coding agents (OpenCode, Claude Code, Aider).

---

## Sprints Completed

| Sprint | Feature | Lines |
|--------|---------|-------|
| 17.1 | Edit tool with fuzzy matching | ~785 |
| 17.2 | Ls + Todo tools | ~770 |
| 17.3 | Question tool + module | ~684 |
| 17.4 | WebSearch + WebFetch tools | ~806 |
| 17.5 | Task tool + subagent system | ~693 |
| **Total** | | **~3,738** |

---

## Tools Implemented

| Tool | File | Lines | Purpose |
|------|------|-------|---------|
| edit | edit.ts | 385 | Fuzzy-matching file editing |
| - | edit-replacers.ts | 400 | 7 replacer strategies |
| ls | ls.ts | 419 | Directory listing with tree view |
| todoread | todo.ts | 175 | Read session todo list |
| todowrite | todo.ts | 176 | Update session todo list |
| question | question.ts | 323 | Ask user clarifying questions |
| websearch | websearch.ts | 379 | Web search (Tavily/Exa) |
| webfetch | webfetch.ts | 427 | Fetch and process web pages |
| task | task.ts | 376 | Spawn subagents for complex tasks |

**Total: 8 new tools** (15 tools total in Estela)

---

## Sprint 17.1: Edit Tool with Fuzzy Matching

### What
String replacement editing with 7 fuzzy matching strategies for robust code editing.

### Files Created
- `packages/core/src/tools/edit.ts` (~385 lines)
- `packages/core/src/tools/edit-replacers.ts` (~400 lines)

### Replacer Strategies (in order of strictness)
1. **SimpleReplacer** - Exact match
2. **LineTrimmedReplacer** - Trim whitespace per line
3. **BlockAnchorReplacer** - Match first/last lines + fuzzy middle (Levenshtein)
4. **WhitespaceNormalizedReplacer** - Collapse whitespace
5. **IndentationFlexibleReplacer** - Ignore leading indent
6. **TrimmedBoundaryReplacer** - Trim first/last lines only
7. **MultiOccurrenceReplacer** - Handle multiple matches

### Key Types
```typescript
type Replacer = (content: string, find: string) => Generator<string, void, unknown>

interface EditParams {
  filePath: string
  oldString: string
  newString: string
  replaceAll?: boolean
}
```

---

## Sprint 17.2: Ls + Todo Tools

### Ls Tool
Directory listing with tree-view output and smart ignore patterns.

**Features:**
- Tree-view format output
- Default ignore: node_modules, .git, dist, build, __pycache__, etc.
- Custom ignore patterns
- Recursive/non-recursive modes
- Max file limit

```typescript
interface LsParams {
  path?: string
  ignore?: string[]
  recursive?: boolean
  maxFiles?: number
}
```

### Todo Tools
Session-based task management for LLM tracking.

**Features:**
- `todoread` - Returns current todo list
- `todowrite` - Replaces entire todo list
- Status: pending, in_progress, completed
- Persists with session state

```typescript
interface TodoItem {
  id: string
  content: string
  status: 'pending' | 'in_progress' | 'completed'
  createdAt: number
  completedAt?: number
}
```

---

## Sprint 17.3: Question Tool + Module

### What
Enable LLM to ask clarifying questions during execution with async blocking.

### Files Created
- `packages/core/src/tools/question.ts` (~323 lines)
- `packages/core/src/question/types.ts` (~108 lines)
- `packages/core/src/question/manager.ts` (~253 lines)
- `packages/core/src/question/index.ts` (~10 lines)

### How It Works
1. Tool emits question via metadata callback
2. Execution blocks on Promise
3. UI shows question to user
4. User answer resolves Promise
5. Tool returns formatted answers to LLM

### Key Types
```typescript
interface Question {
  id: string
  text: string
  header?: string
  options?: string[]
  multiSelect?: boolean
  required?: boolean
}

class QuestionManager {
  ask(question: Question): Promise<QuestionResult>
  answer(questionId: string, answer: string): boolean
  cancel(questionId: string): boolean
  getPending(): Question[]
}
```

---

## Sprint 17.4: WebSearch + WebFetch Tools

### WebSearch
AI-optimized web search with multiple providers.

**Providers:**
- **Tavily API** - AI-optimized search, good free tier
- **Exa API** - Neural search

```typescript
interface WebSearchParams {
  query: string
  numResults?: number
  provider?: 'tavily' | 'exa'
}
```

### WebFetch
Fetch and extract content from web pages with HTML-to-Markdown conversion.

**Features:**
- Automatic HTML to Markdown
- Script/style stripping
- Title/description extraction
- Content truncation
- Link preservation

```typescript
interface WebFetchParams {
  url: string
  prompt?: string
  maxChars?: number
}
```

---

## Sprint 17.5: Task Tool + Subagent System

### What
Spawn specialized subagents for complex multi-step tasks.

### Files Created
- `packages/core/src/tools/task.ts` (~376 lines)
- `packages/core/src/agent/subagent.ts` (~317 lines)

### Built-in Agent Types
| Type | Tools | Max Turns | Use Case |
|------|-------|-----------|----------|
| explore | glob, grep, read, ls | 20 | Codebase exploration |
| plan | glob, grep, read, ls, write | 15 | Planning without execution |
| execute | all | 50 | Full execution |
| custom | specified | 30 | Custom tool set |

### Key Types
```typescript
interface TaskParams {
  description: string
  prompt: string
  agentType: SubagentType
  sessionId?: string
  maxTurns?: number
  allowedTools?: string[]
}

class SubagentManager {
  createConfig(type: SubagentType, overrides?): SubagentConfig
  filterTools<T>(tools: T[], config: SubagentConfig): T[]
  register(config: SubagentConfig): void
  unregister(id: string): void
}
```

---

## New Module: `question/`

| File | Lines | Purpose |
|------|-------|---------|
| types.ts | 108 | Question, PendingQuestion, QuestionResult |
| manager.ts | 253 | QuestionManager singleton, ask/answer pattern |
| index.ts | 10 | Exports |

---

## Tool Count Summary

**Before Epic 17:** 7 tools
- glob, read, grep, create, write, delete, bash

**After Epic 17:** 15 tools
- glob, read, grep, create, write, delete, bash
- **edit**, **ls**, **todoread**, **todowrite**, **question**, **websearch**, **webfetch**, **task**

---

## Backward Compatibility

All new tools are additive:
- Existing tools unchanged
- New tools register alongside existing
- Session state TodoItem[] is optional field
- Question system is opt-in via callback
- Subagent system is infrastructure-ready (execution placeholder)
