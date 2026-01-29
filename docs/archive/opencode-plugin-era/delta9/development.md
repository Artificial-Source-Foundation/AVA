# Delta9 Development Guide

> How to develop, test, and contribute to Delta9.

---

## Prerequisites

- **Node.js**: 18+
- **Bun**: Latest (for plugin loading)
- **OpenCode**: 1.0.150+
- **Git**: For version control

---

## Project Setup

### 1. Clone and Install

```bash
git clone https://github.com/[username]/delta9.git
cd delta9
npm install
```

### 2. Build

```bash
npm run build
```

### 3. Link for Development

```bash
# Create symlink for local testing
ln -s $(pwd) ~/.config/opencode/plugins/delta9
```

### 4. Verify Installation

```bash
opencode
# Delta9 should load automatically
```

---

## Development Workflow

### Watch Mode

```bash
npm run dev
# Watches for changes and rebuilds
```

### Testing

```bash
npm run test           # Run all tests
npm run test:watch     # Watch mode
npm run test:coverage  # Coverage report
```

### Linting

```bash
npm run lint           # ESLint check
npm run lint:fix       # Auto-fix issues
npm run typecheck      # TypeScript check
```

---

## Project Structure

```
delta9/
├── src/
│   ├── index.ts              # Plugin entry point
│   ├── types/                # TypeScript definitions
│   │   ├── mission.ts
│   │   ├── agents.ts
│   │   ├── config.ts
│   │   └── index.ts
│   ├── schemas/              # Zod validation
│   │   ├── mission.schema.ts
│   │   └── config.schema.ts
│   ├── lib/                  # Utilities
│   │   ├── config.ts
│   │   ├── paths.ts
│   │   └── logger.ts
│   ├── mission/              # State management
│   │   ├── state.ts
│   │   ├── markdown.ts
│   │   └── history.ts
│   ├── agents/               # Agent definitions
│   │   ├── commander.ts
│   │   ├── council/
│   │   ├── execution/
│   │   └── support/
│   ├── tools/                # Custom tools
│   │   ├── mission.ts
│   │   ├── dispatch.ts
│   │   └── validation.ts
│   ├── hooks/                # Event handlers
│   │   ├── session.ts
│   │   └── tools.ts
│   └── commands/             # Slash commands
│       ├── sitrep.ts
│       └── mission.ts
├── tests/
│   ├── unit/
│   └── integration/
├── docs/                     # Documentation
├── package.json
├── tsconfig.json
└── vitest.config.ts
```

---

## Coding Guidelines

### TypeScript

```typescript
// DO: Use strict types
function getTask(id: string): Task | undefined {
  return tasks.find(t => t.id === id)
}

// DON'T: Use any
function getTask(id: any): any {
  return tasks.find(t => t.id === id)
}
```

### Zod Validation

```typescript
// Always validate external data
const mission = MissionSchema.parse(data) // Throws if invalid

// Use safeParse for graceful handling
const result = MissionSchema.safeParse(data)
if (!result.success) {
  console.error(result.error)
  return null
}
return result.data
```

### Error Handling

```typescript
// Use try-catch with specific errors
try {
  await fs.readFile(path)
} catch (error) {
  if ((error as NodeJS.ErrnoException).code === "ENOENT") {
    return null // File doesn't exist
  }
  throw error // Re-throw unknown errors
}
```

### Async Patterns

```typescript
// Parallel when independent
const [a, b] = await Promise.all([fetchA(), fetchB()])

// Sequential when dependent
const a = await fetchA()
const b = await fetchB(a.id)
```

---

## Testing Patterns

### Unit Tests

```typescript
// tests/unit/mission.test.ts
import { describe, it, expect, beforeEach } from "vitest"
import { MissionState } from "../../src/mission/state"

describe("MissionState", () => {
  let state: MissionState

  beforeEach(() => {
    state = new MissionState("/tmp/test")
  })

  it("creates mission with valid description", async () => {
    const mission = await state.create("Test mission", "standard")
    expect(mission.id).toBeDefined()
    expect(mission.description).toBe("Test mission")
    expect(mission.councilMode).toBe("standard")
  })

  it("throws on invalid council mode", async () => {
    await expect(
      state.create("Test", "invalid" as any)
    ).rejects.toThrow()
  })
})
```

### Tool Tests

```typescript
// tests/unit/tools.test.ts
import { describe, it, expect } from "vitest"
import { missionCreate } from "../../src/tools/mission"

describe("missionCreate tool", () => {
  it("creates mission with valid input", async () => {
    const result = await missionCreate.execute(
      { description: "Test mission", councilMode: "standard" },
      { agent: "test", sessionID: "s1", messageID: "m1" }
    )

    expect(result.success).toBe(true)
    expect(result.missionId).toBeDefined()
  })
})
```

---

## Debugging

### Logging

```typescript
// Use client.app.log for structured logging
await client.app.log("debug", JSON.stringify({
  event: "task_started",
  taskId: task.id,
  assignee: "operator",
}))
```

### Checking State

```bash
# View current mission
cat .delta9/mission.json | jq

# View history
cat .delta9/history.jsonl | jq -s

# View generated markdown
cat .delta9/mission.md
```

### OpenCode Logs

```bash
# OpenCode logs location
tail -f ~/.local/share/opencode/logs/opencode.log
```

---

## Adding Features

### New Agent

1. Create agent file:
   ```typescript
   // src/agents/support/my-agent.ts
   export const myAgent: AgentDefinition = {
     name: "my-agent",
     role: "support",
     layer: "support",
     model: "anthropic/claude-haiku-4-5",
     temperature: 0.5,
     systemPrompt: "You are...",
     tools: ["read", "grep"],
   }
   ```

2. Export from index:
   ```typescript
   // src/agents/index.ts
   export { myAgent } from "./support/my-agent"
   ```

3. Register in plugin:
   ```typescript
   // src/index.ts
   return {
     agent: {
       "my-agent": myAgent,
     },
   }
   ```

### New Tool

1. Create tool file:
   ```typescript
   // src/tools/my-tool.ts
   import { tool } from "@opencode-ai/plugin"
   import { z } from "zod"

   export const myTool = tool({
     description: "What this tool does",
     args: {
       param: z.string().describe("Parameter"),
     },
     async execute({ param }, ctx) {
       return { result: "done" }
     },
   })
   ```

2. Export and register:
   ```typescript
   // src/tools/index.ts
   export { myTool } from "./my-tool"

   // src/index.ts
   return {
     tool: {
       my_tool: myTool,
     },
   }
   ```

### New Hook

1. Create hook handler:
   ```typescript
   // src/hooks/my-hook.ts
   export const myHookHandler: HookHandler = async ({ event }, ctx) => {
     // Handle event
   }
   ```

2. Register in plugin:
   ```typescript
   // src/index.ts
   return {
     "event.name": myHookHandler,
   }
   ```

---

## Release Process

### Version Bump

```bash
npm version patch  # or minor, major
```

### Build and Test

```bash
npm run build
npm run test
npm run lint
```

### Publish

```bash
npm publish
```

---

## Contributing

### Pull Request Guidelines

1. Create feature branch from `main`
2. Write tests for new features
3. Ensure all tests pass
4. Update documentation if needed
5. Submit PR with clear description

### Commit Messages

Follow conventional commits:

```
feat: add new agent type
fix: correct mission state persistence
docs: update API reference
test: add unit tests for MissionState
refactor: simplify council synthesis
```

---

---

## Testing Guide

### Quick Verification

After building, verify the plugin loads correctly:

```bash
npm run build
npm run typecheck
```

### Manual Testing in OpenCode

1. **Start OpenCode with Delta9**:
   ```bash
   opencode
   ```

2. **Check system health**:
   ```
   > Use delta9_health to check system status
   ```

   Expected output:
   ```json
   {
     "status": "healthy",
     "statusEmoji": "✅",
     "sdk": { "available": true, "mode": "live" },
     "mission": { "active": false },
     "backgroundTasks": { "active": 0, "pending": 0 }
   }
   ```

3. **Test mission creation**:
   ```
   > Use mission_create to create a test mission
   ```

4. **Test background task execution**:
   ```
   > Use delegate_task with run_in_background=true to spawn a test agent
   ```

5. **Verify background task status**:
   ```
   > Use background_list to see task status
   ```

   Look for emoji indicators: ⏳ (pending), 🔄 (running), ✅ (completed)

### Testing DX Features

#### Structured Logger

Check logs are formatted correctly:
```bash
# In OpenCode session
> Use delta9_health with verbose=true

# Check console output for format:
# 12:34:56.789 [delta9:core] [INFO] Plugin loading | cwd=/path/to/project
```

#### Rich Error Handling

Test error messages include suggestions:
```
> Use background_output with taskId="invalid_id"
```

Expected response:
```json
{
  "error": true,
  "code": "TASK_NOT_FOUND",
  "message": "Task invalid_id not found",
  "suggestions": [
    "Use background_list to see available tasks",
    "Task may have been cleaned up (30min TTL)",
    "Check if task ID is correct (format: bg_xxxxx)"
  ]
}
```

#### Hints System

Test hints appear in empty states:
```
> Use background_list
```

When no tasks exist, response should include:
```json
{
  "hint": "No background tasks. Use delegate_task with run_in_background=true to spawn an agent."
}
```

### Unit Testing

```bash
npm run test           # Run all tests
npm run test:watch     # Watch mode
npm run test:coverage  # Coverage report
```

### Test Files Structure

```
tests/
├── unit/
│   ├── mission/
│   │   ├── state.test.ts
│   │   └── markdown.test.ts
│   ├── lib/
│   │   ├── config.test.ts
│   │   ├── logger.test.ts
│   │   ├── errors.test.ts
│   │   └── hints.test.ts
│   └── tools/
│       ├── background.test.ts
│       ├── delegation.test.ts
│       └── diagnostics.test.ts
└── integration/
    └── plugin.test.ts
```

### Common Test Patterns

#### Testing Tools

```typescript
import { describe, it, expect, vi } from "vitest"
import { createBackgroundTools } from "../../src/tools/background"
import { MissionState } from "../../src/mission/state"

describe("background_list", () => {
  it("returns empty state with hint", async () => {
    const state = new MissionState("/tmp/test")
    const tools = createBackgroundTools(state, "/tmp/test")

    const result = await tools.background_list.execute({}, mockCtx)
    const parsed = JSON.parse(result)

    expect(parsed.success).toBe(true)
    expect(parsed.tasks).toHaveLength(0)
    expect(parsed.hint).toContain("No background tasks")
  })
})
```

#### Testing Logger

```typescript
import { describe, it, expect, vi } from "vitest"
import { createLogger, getNamedLogger, initLogger } from "../../src/lib/logger"

describe("Logger", () => {
  it("formats messages with component name", () => {
    const consoleSpy = vi.spyOn(console, "info")
    initLogger()

    const log = getNamedLogger("test")
    log.info("Hello", { foo: "bar" })

    expect(consoleSpy).toHaveBeenCalledWith(
      expect.stringContaining("[delta9:test]")
    )
    expect(consoleSpy).toHaveBeenCalledWith(
      expect.stringContaining("foo=bar")
    )
  })
})
```

#### Testing Errors

```typescript
import { describe, it, expect } from "vitest"
import { errors, isDelta9Error, Delta9Error } from "../../src/lib/errors"

describe("Delta9Error", () => {
  it("creates error with suggestions", () => {
    const err = errors.taskNotFound("bg_123")

    expect(err).toBeInstanceOf(Delta9Error)
    expect(err.code).toBe("TASK_NOT_FOUND")
    expect(err.suggestions).toContain("Use background_list to see available tasks")
  })

  it("serializes to tool response", () => {
    const err = errors.noActiveMission()
    const response = JSON.parse(err.toToolResponse())

    expect(response.error).toBe(true)
    expect(response.code).toBe("NO_ACTIVE_MISSION")
    expect(response.suggestions).toBeInstanceOf(Array)
  })
})
```

---

## Troubleshooting

### Plugin Not Loading

1. Check build completed without errors:
   ```bash
   npm run build
   ```

2. Verify symlink exists:
   ```bash
   ls -la ~/.config/opencode/plugins/delta9
   ```

3. Check OpenCode logs:
   ```bash
   tail -f ~/.local/share/opencode/logs/opencode.log
   ```

### SDK Not Available

If `delta9_health` shows `"mode": "simulation"`:

1. Verify OpenCode version is 1.0.150+
2. Check plugin is loaded via OpenCode, not standalone
3. Ensure `client` is passed to tool factories in `src/index.ts`

### Background Tasks Not Running

1. Check task pool status:
   ```
   > Use background_list
   ```

2. If pool is at capacity (3 active), tasks queue automatically

3. Check for failed tasks:
   ```
   > Use background_list with status="failed"
   ```

4. Get error details:
   ```
   > Use background_output with taskId="bg_xxx"
   ```

### Mission State Issues

1. Check mission file:
   ```bash
   cat .delta9/mission.json | jq
   ```

2. Clear corrupted state:
   ```
   > Use mission_clear
   ```

3. Check history for issues:
   ```bash
   cat .delta9/history.jsonl | jq -s
   ```

---

## Resources

- [OpenCode Plugin Docs](https://opencode.ai/docs/plugins/)
- [Zod Documentation](https://zod.dev/)
- [Vitest Documentation](https://vitest.dev/)
- [oh-my-opencode](https://github.com/code-yeongyu/oh-my-opencode)
- [Delta9 Specification](spec.md)
