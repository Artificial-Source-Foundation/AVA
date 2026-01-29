# OpenCode Plugin Best Practices

> Collected best practices for building robust OpenCode plugins.

---

## Architecture

### 1. Separate Concerns

```
src/
├── agents/       # Agent definitions only
├── tools/        # Tool definitions only
├── hooks/        # Hook handlers only
├── lib/          # Shared utilities
├── types/        # TypeScript types
└── index.ts      # Composition
```

### 2. Single Responsibility

Each file should do one thing:
- One agent per file
- One tool per file
- Related hooks grouped by feature

### 3. Type Everything

```typescript
import type { Plugin, Tool, Agent } from "@opencode-ai/plugin"
import { z } from "zod"

// Define schemas
const MissionSchema = z.object({
  id: z.string(),
  status: z.enum(["pending", "active", "completed"]),
})

type Mission = z.infer<typeof MissionSchema>
```

---

## State Management

### 1. External Persistence

Don't rely on in-memory state - it's lost on compaction:

```typescript
// BAD: In-memory state
let missionState = null

// GOOD: File-based persistence
async function loadState() {
  return JSON.parse(await fs.readFile(".delta9/mission.json", "utf-8"))
}

async function saveState(state) {
  await fs.writeFile(".delta9/mission.json", JSON.stringify(state, null, 2))
}
```

### 2. Reload on Compaction

```typescript
"session.compacted": async ({ event }) => {
  // State survives via disk
  missionState = await loadState()
  await client.app.log("info", "Reloaded state after compaction")
}
```

### 3. Validate All State

```typescript
const state = await loadState()
const validated = MissionSchema.parse(state) // Throws if invalid
```

---

## Agent Design

### 1. Clear Role Boundaries

```markdown
---
description: Strategic planner - NEVER writes code
---

You are Commander. You:
- Analyze requests
- Create plans
- Dispatch work

You MUST NOT:
- Write or edit code
- Execute bash commands
```

### 2. Model Matching

| Task Type | Model Class | Example |
|-----------|-------------|---------|
| Planning | Opus-class | `claude-opus-4-5` |
| Execution | Sonnet-class | `claude-sonnet-4` |
| Verification | Haiku-class | `claude-haiku-4-5` |
| UI/Creative | Gemini | `gemini-3-pro` |
| Research | Variable | `glm-4.7`, `grok` |

### 3. Temperature Tuning

| Task | Temperature | Rationale |
|------|-------------|-----------|
| Planning | 0.7 | Creative exploration |
| Execution | 0.3 | Precise implementation |
| Verification | 0.1 | Strict evaluation |

---

## Tool Design

### 1. Descriptive Names

```typescript
// BAD
tool({ description: "Does stuff" })

// GOOD
tool({ description: "Create a new mission with objectives and tasks" })
```

### 2. Comprehensive Arg Descriptions

```typescript
args: {
  description: z.string()
    .min(10)
    .describe("Detailed mission description (min 10 chars)"),
  mode: z.enum(["none", "quick", "standard", "xhigh"])
    .default("standard")
    .describe("Council mode: none=simple, xhigh=critical"),
}
```

### 3. Structured Returns

```typescript
async execute(args, ctx) {
  // Return structured data, not just strings
  return {
    success: true,
    missionId: "m_123",
    objectiveCount: 3,
    taskCount: 8,
  }
}
```

---

## Hook Patterns

### 1. Fast Handlers

```typescript
// BAD: Blocking operation
"session.idle": async ({ event }) => {
  await longRunningOperation() // Blocks UI
}

// GOOD: Non-blocking
"session.idle": async ({ event }) => {
  // Quick check
  const hasPending = await quickPendingCheck()
  if (hasPending) {
    // Schedule async work
    setImmediate(() => processPending())
  }
}
```

### 2. Error Isolation

```typescript
"tool.execute.after": async ({ event }) => {
  try {
    await trackChange(event)
  } catch (error) {
    // Log but don't crash
    await client.app.log("error", `Hook error: ${error.message}`)
  }
}
```

### 3. Idempotency

Hooks may fire multiple times. Design for it:

```typescript
"session.created": async ({ event }) => {
  // Check if already initialized
  if (await isInitialized(event.id)) return

  await initialize(event.id)
}
```

---

## Configuration

### 1. Sensible Defaults

```typescript
const DEFAULT_CONFIG = {
  commander: {
    model: "anthropic/claude-opus-4-5",
    temperature: 0.7,
  },
  council: {
    enabled: true,
    defaultMode: "standard",
  },
  budget: {
    limit: 10.0,
    warnAt: 0.7,
  },
}
```

### 2. Deep Merging

```typescript
function loadConfig() {
  const global = loadGlobalConfig()
  const project = loadProjectConfig()
  return deepMerge(DEFAULT_CONFIG, global, project)
}
```

### 3. Environment Variable Support

```json
{
  "api_key": "{env:MY_API_KEY}"
}
```

---

## Logging

### 1. Use Structured Logging

```typescript
await client.app.log("info", JSON.stringify({
  event: "mission_created",
  missionId: mission.id,
  taskCount: mission.tasks.length,
}))
```

### 2. Log Levels

| Level | Use For |
|-------|---------|
| `debug` | Detailed troubleshooting |
| `info` | Normal operations |
| `warn` | Recoverable issues |
| `error` | Failures |

### 3. Audit Trail

```typescript
async function appendHistory(event: HistoryEvent) {
  const line = JSON.stringify({ ...event, timestamp: new Date().toISOString() })
  await fs.appendFile(".delta9/history.jsonl", line + "\n")
}
```

---

## Testing

### 1. Unit Test Tools

```typescript
import { describe, it, expect } from "vitest"
import { missionCreate } from "./tools/mission"

describe("mission_create", () => {
  it("creates mission with valid input", async () => {
    const result = await missionCreate.execute(
      { description: "Test mission" },
      { agent: "test", sessionID: "s1", messageID: "m1" }
    )
    expect(result.success).toBe(true)
  })
})
```

### 2. Integration Test Hooks

```typescript
it("handles session.created", async () => {
  const handler = plugin["session.created"]
  await handler({ event: { id: "test-session" } })
  // Verify state was initialized
})
```

---

## Security

### 1. Validate All Input

```typescript
const validated = MissionSchema.parse(input) // Never trust raw input
```

### 2. Sanitize Shell Commands

```typescript
// BAD: Command injection risk
await $`${userInput}`

// GOOD: Use argument arrays
await $`git commit -m ${sanitize(message)}`
```

### 3. Credential Handling

```typescript
// Never log credentials
await client.app.log("info", "API connected") // NOT: log(apiKey)

// Use environment variables
const key = process.env.API_KEY
```

---

## Performance

### 1. Lazy Loading

```typescript
let _config: Config | null = null

function getConfig() {
  if (!_config) {
    _config = loadConfig()
  }
  return _config
}
```

### 2. Parallel Operations

```typescript
// Parallel when independent
const [a, b, c] = await Promise.all([
  fetchA(),
  fetchB(),
  fetchC(),
])

// Sequential when dependent
const a = await fetchA()
const b = await fetchB(a.id)
```

### 3. Efficient File Operations

```typescript
// BAD: Multiple writes
for (const item of items) {
  await fs.appendFile(path, item)
}

// GOOD: Batch write
await fs.writeFile(path, items.join("\n"))
```

---

## Reference

- [Oh-My-OpenCode patterns](./OH_MY_OPENCODE.md)
- [OpenCode Plugin Docs](https://opencode.ai/docs/plugins/)
