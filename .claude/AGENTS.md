# Delta9 - Claude Code Agent Instructions

> Instructions for Claude Code when working on this project.

---

## Priority Information

1. **Read `docs/spec.md` first** - Full 876-line specification (source of truth)
2. **Check `PLAN.md`** - Phase 1 implementation roadmap
3. **Reference `docs/OPENCODE_REFERENCE/`** - OpenCode plugin system docs

---

## Commands

```bash
npm run build      # Build TypeScript
npm run test       # Run Vitest tests
npm run lint       # ESLint check
npm run typecheck  # TypeScript check
npm run dev        # Watch mode
```

---

## Project Structure

```
src/
├── index.ts           # Plugin entry point - START HERE
├── agents/            # Agent definitions
├── mission/           # State management (mission.json)
├── council/           # Council orchestration
├── tools/             # Custom tools
├── hooks/             # OpenCode event hooks
├── lib/               # Config, logging, utilities
└── types/             # TypeScript definitions
```

---

## Code Patterns

### Plugin Entry
```typescript
import type { Plugin } from "@opencode-ai/plugin"

export const Delta9: Plugin = async (ctx) => {
  return {
    agent: { ... },
    tool: { ... },
    "session.created": async ({ event }) => { ... },
  }
}
```

### Custom Tool
```typescript
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export const myTool = tool({
  description: "What it does",
  args: { param: z.string() },
  async execute({ param }, ctx) {
    return { result: "done" }
  },
})
```

### State Persistence
```typescript
// Use MissionState class, never mutate directly
const state = new MissionState(directory)
await state.load()
state.updateTask(id, { status: "completed" })
await state.save()
```

---

## Key Concepts

| Concept | Description |
|---------|-------------|
| **Commander** | Plans only, NEVER writes code |
| **Council** | 1-4 Oracles for diverse opinions |
| **Operators** | Task executors (Sonnet 4) |
| **Validator** | QA gate before completion |
| **mission.json** | External state, survives compaction |

---

## Boundaries

### Always Do
- Read spec.md before architectural changes
- Validate all data with Zod
- Use OpenCode plugin patterns (see docs/OPENCODE_REFERENCE/)
- Persist state via MissionState class

### Ask First
- Modifying mission.json schema
- Adding new agent types
- Changing Commander's dispatch logic

### Never Do
- Use `any` type
- Mutate mission state directly
- Skip the Validator gate
- Hardcode model names
- Let Commander write code

---

## Current Status

- **Phase**: Specification complete
- **Built**: Documentation only
- **Next**: Plugin scaffold, config system, Commander

See `PLAN.md` for full implementation roadmap.

---

## Reference Source Code

Real plugin implementations are in `docs/REFERENCE_CODE/`:
- `oh-my-opencode/` - Gold standard (22.7K stars)
- `oh-my-opencode-slim/` - Token-efficient fork
- `opencode-plugins/` - Utility collection
- `opencode-plugin-template/` - Starter template
- `opencode-skillful/` - Skills system

Study these for patterns, especially `oh-my-opencode/src/`.
