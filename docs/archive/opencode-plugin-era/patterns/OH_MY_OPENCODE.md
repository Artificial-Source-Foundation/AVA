# Oh-My-OpenCode Patterns

> Patterns and lessons from the most successful OpenCode plugin.

---

## Overview

[Oh-My-OpenCode](https://github.com/code-yeongyu/oh-my-opencode) is the gold standard for OpenCode plugins with 22.7K+ stars. It implements a sophisticated multi-agent orchestration system called "Sisyphus."

---

## Source Structure

```
oh-my-opencode/
├── src/
│   ├── agents/           # 10 AI agents
│   ├── hooks/            # 31 lifecycle hooks
│   ├── tools/            # 20+ custom tools (LSP, AST-Grep)
│   ├── features/         # Background agents, compatibility
│   ├── shared/           # 50 cross-cutting utilities
│   ├── cli/              # CLI installer, doctor
│   ├── mcp/              # Built-in MCPs
│   ├── config/           # Zod schema, TypeScript types
│   └── index.ts          # Main plugin entry (~590 lines)
├── packages/             # Platform-specific binaries
└── dist/                 # Build output (ESM + .d.ts)
```

---

## Agent Architecture

### Primary Agent: Sisyphus

The main orchestrator (Claude Opus 4.5) that coordinates all work:
- Analyzes requests and delegates to specialists
- Maintains context and conversation flow
- Invokes support agents as needed

### Specialist Agents

| Agent | Model | Purpose |
|-------|-------|---------|
| **Sisyphus** | Claude Opus 4.5 | Main orchestrator |
| **Oracle** | GPT 5.2 | Strategic debugging, architecture |
| **Librarian** | Claude Sonnet | Documentation, codebase exploration |
| **Explore** | Fast model | Quick contextual code search |
| **Frontend** | Gemini 3 | UI/UX component development |
| **Document-Writer** | - | Documentation generation |
| **Multimodal-Looker** | - | Image/PDF analysis |

### Key Insight: Curated Model Selection

Different models excel at different tasks:
- **Claude Opus**: Deep reasoning, architecture
- **GPT 5.2**: Code patterns, best practices
- **Gemini 3**: Visual/creative tasks
- **Haiku/Flash**: Fast, cheap operations

---

## Hook System

31 lifecycle hooks covering the entire OpenCode lifecycle:

### Session Hooks
- `session.created` - Initialize state
- `session.idle` - Check for pending work
- `session.compacted` - Handle context compression

### Tool Hooks
- `tool.execute.before` - Intercept/modify tool calls
- `tool.execute.after` - Track results, log changes

### Message Hooks
- `message.created` - Analyze incoming requests
- `message.updated` - Track modifications

### Disabling Hooks

```json
{
  "disabled_hooks": ["hook-name-1", "hook-name-2"]
}
```

---

## Key Patterns

### 1. Context Injection

Automatically embed project context into agent prompts:

```typescript
"session.created": async ({ event }) => {
  // Load AGENTS.md, README.md into context
  const context = await loadProjectContext()
  await injectContext(event.sessionId, context)
}
```

### 2. Background Agent Parallelization

Run multiple agents concurrently to reduce main context load:

```typescript
// Dispatch research agents in parallel
const [codebaseInfo, docResults] = await Promise.all([
  invokeAgent("explore", "Find authentication files"),
  invokeAgent("librarian", "Search OAuth documentation"),
])
```

### 3. Magic Word Activation

Simple triggers for complex orchestration:

```typescript
// "ultrawork" or "ulw" triggers full orchestration
if (message.includes("ultrawork") || message.includes("ulw")) {
  await enableFullOrchestration()
}
```

### 4. Todo Continuation Enforcer

Prevent premature task abandonment:

```typescript
"session.idle": async ({ event }) => {
  const todos = await getTodos()
  const incomplete = todos.filter(t => !t.completed)
  if (incomplete.length > 0) {
    await promptContinuation(event.sessionId, incomplete)
  }
}
```

### 5. Comment Checker

Keep generated code clean:

```typescript
"tool.execute.after": async ({ event }) => {
  if (event.tool === "write" || event.tool === "edit") {
    await checkForUnnecessaryComments(event.args.path)
  }
}
```

---

## Configuration Pattern

Hierarchical configuration with sensible defaults:

```
~/.config/opencode/oh-my-opencode.json   # Global
.opencode/oh-my-opencode.json            # Project (overrides)
```

Both support JSONC (comments, trailing commas).

### Per-Agent Overrides

```json
{
  "agents": {
    "sisyphus": {
      "model": "anthropic/claude-opus-4-5",
      "temperature": 0.7
    },
    "oracle": {
      "model": "openai/gpt-5.2-codex"
    }
  }
}
```

---

## Built-in MCPs

Enabled by default:

| MCP | Purpose |
|-----|---------|
| **Exa** | Real-time web search |
| **Context7** | Library documentation |
| **grep.app** | GitHub code search |

---

## Lessons for Delta9

### Adopt
1. **Curated model selection** - Match models to tasks
2. **Background parallelization** - Reduce main context load
3. **Hierarchical configuration** - Global + project overrides
4. **Hook-based architecture** - Extensible event handling
5. **Magic word triggers** - Simple UX for complex operations

### Differentiate
1. **Protected Commander context** - Oh-My-OpenCode doesn't separate planning
2. **Council deliberation** - Multi-model consensus (unique to Delta9)
3. **External mission state** - Survives compaction (oh-my uses in-memory)
4. **Validation gate** - Dedicated verification agent
5. **Checkpoints/rollback** - Git-based recovery

---

## Reference

- [oh-my-opencode GitHub](https://github.com/code-yeongyu/oh-my-opencode)
- [oh-my-opencode AGENTS.md](https://github.com/code-yeongyu/oh-my-opencode/blob/master/AGENTS.md)
- [Official Website](https://ohmyopencode.com/)
