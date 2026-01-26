# Delta9 Agent Instructions

> Universal instructions for AI coding agents working on this project

---

## Commands

```bash
npm run build      # Build TypeScript to dist/
npm run test       # Run Vitest tests
npm run lint       # ESLint check
npm run typecheck  # TypeScript strict check
npm run dev        # Development mode with watch
```

---

## Project Structure

```
src/
├── index.ts           # Plugin entry point - START HERE
├── agents/            # Agent definitions (Commander, Oracles, Operators)
├── mission/           # State management (mission.json CRUD)
├── council/           # Council orchestration & synthesis
├── routing/           # Task routing & complexity detection
├── hooks/             # OpenCode event hooks
├── tools/             # Custom tools for agents
├── lib/               # Config, logging, git utilities
├── schemas/           # Zod validation schemas
└── types/             # TypeScript type definitions
```

---

## Code Style

- **TypeScript strict mode** required
- **Zod** for all runtime validation
- **No `any` type** - use `unknown` with type guards
- Export types from `src/types/`
- One component per file when possible
- `kebab-case` for filenames
- `camelCase` for functions
- `PascalCase` for types/classes

---

## Testing

- Framework: **Vitest**
- Run: `npm run test`
- New features require tests
- Use `client.app.log()` for debug logging

---

## Boundaries

### Always Do
- Read `docs/spec.md` before making architectural changes
- Validate all external data with Zod schemas
- Use OpenCode plugin patterns (see `docs/OPENCODE_REFERENCE/`)
- Test changes before committing
- Keep Commander context clean (no implementation details)

### Ask First
- Modifying `mission.json` schema structure
- Adding new agent types to the roster
- Changing Commander's dispatch logic
- Modifying Council synthesis algorithm

### Never Do
- Use `any` type
- Mutate mission state directly (use MissionState methods)
- Skip the Validator gate for task completion
- Hardcode model names (use config)
- Let Commander write code (it only plans)

---

## Key Files

| Priority | File | Purpose |
|----------|------|---------|
| 1 | `docs/spec.md` | Full specification - source of truth |
| 2 | `src/index.ts` | Plugin entry point |
| 3 | `src/mission/state.ts` | Mission state manager |
| 4 | `src/agents/commander.ts` | Lead orchestrator |
| 5 | `src/types/` | Type definitions |

---

## OpenCode Plugin Patterns

Agents are defined as markdown or JSON. See `docs/OPENCODE_REFERENCE/02_AGENTS.md`.

```typescript
// Agent definition pattern
export const myAgent: AgentDefinition = {
  name: 'my-agent',
  mode: 'subagent',
  model: 'anthropic/claude-sonnet-4-5-20250929',
  temperature: 0.3,
  systemPrompt: `Your role...`,
  tools: ['read', 'write', 'bash']
}
```

Custom tools use Zod schemas:

```typescript
import { tool } from '@opencode-ai/plugin'
import { z } from 'zod'

export const myTool = tool({
  description: 'What this tool does',
  args: {
    param: z.string().describe('Parameter description')
  },
  async execute({ param }, ctx) {
    // Implementation
    return { result: 'done' }
  }
})
```

---

## Mission State

Mission state persists in `.delta9/mission.json`. Never modify directly.

```typescript
// Use MissionState class methods
const state = new MissionState()
state.load()                    // Load from disk
state.create('description')     // Create new mission
state.addObjective({...})       // Add objective
state.updateTask(id, {...})     // Update task
state.save()                    // Persist to disk
```

---

## Architecture Summary

```
User Request → Commander (plan) → Council (deliberate) → Mission Plan
                                                              ↓
User Approval ← ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─┘
      ↓
Commander (dispatch) → Operator (execute) → Validator (verify)
                                                   ↓
                                            PASS / FIXABLE / FAIL
```

See `docs/ARCHITECTURE.md` for detailed diagrams.
