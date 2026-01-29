# OpenCode Plugin Examples

> Code examples and patterns for common plugin scenarios.

---

## Basic Plugin Skeleton

```typescript
import type { Plugin } from "@opencode-ai/plugin"

export const MyPlugin: Plugin = async (ctx) => {
  const { project, client, $, directory, worktree } = ctx

  // Initialization
  await client.app.log("info", "Plugin loaded")

  return {
    // Agents
    agent: {},

    // Tools
    tool: {},

    // Hooks
    "session.created": async ({ event }) => {},
  }
}

export default MyPlugin
```

---

## Custom Agent Definition

### As Object in Plugin

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    agent: {
      "code-reviewer": {
        description: "Reviews code for quality and security",
        mode: "subagent",
        model: "anthropic/claude-sonnet-4",
        temperature: 0.1,
        prompt: `You are a code reviewer. Analyze code for:
- Security vulnerabilities
- Performance issues
- Best practice violations

Provide specific, actionable feedback.`,
        tools: {
          read: true,
          grep: true,
          glob: true,
          write: false,
          bash: false,
        },
      },
    },
  }
}
```

### As Markdown File

`.opencode/agents/code-reviewer.md`:

```markdown
---
description: Reviews code for quality and security
mode: subagent
model: anthropic/claude-sonnet-4
temperature: 0.1
tools:
  read: true
  grep: true
  glob: true
  write: false
  bash: false
---

You are a code reviewer. Analyze code for:
- Security vulnerabilities
- Performance issues
- Best practice violations

Provide specific, actionable feedback.
```

---

## Custom Tool Definition

### Simple Tool

```typescript
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export const greetTool = tool({
  description: "Greet the user by name",
  args: {
    name: z.string().describe("Name to greet"),
  },
  async execute({ name }, ctx) {
    return `Hello, ${name}!`
  },
})
```

### Tool with Complex Schema

```typescript
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

const TaskSchema = z.object({
  description: z.string().min(10),
  priority: z.enum(["low", "medium", "high"]),
  assignee: z.string().optional(),
})

export const createTask = tool({
  description: "Create a new task in the mission",
  args: {
    objectiveId: z.string().describe("Parent objective ID"),
    task: TaskSchema.describe("Task details"),
  },
  async execute({ objectiveId, task }, ctx) {
    // Load state
    const state = await loadMissionState()

    // Find objective
    const objective = state.objectives.find(o => o.id === objectiveId)
    if (!objective) {
      throw new Error(`Objective ${objectiveId} not found`)
    }

    // Create task
    const newTask = {
      id: `task_${Date.now()}`,
      ...task,
      status: "pending",
      createdAt: new Date().toISOString(),
    }

    objective.tasks.push(newTask)

    // Save state
    await saveMissionState(state)

    return {
      success: true,
      taskId: newTask.id,
      objectiveId,
    }
  },
})
```

### Tool Calling External Process

```typescript
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export const runTests = tool({
  description: "Run the project test suite",
  args: {
    pattern: z.string().optional().describe("Test file pattern"),
    coverage: z.boolean().default(false).describe("Generate coverage report"),
  },
  async execute({ pattern, coverage }, ctx) {
    const args = ["test"]
    if (pattern) args.push("--pattern", pattern)
    if (coverage) args.push("--coverage")

    try {
      const result = await Bun.$`npm run ${args}`.text()
      return { success: true, output: result }
    } catch (error) {
      return { success: false, error: error.message }
    }
  },
})
```

---

## Hook Patterns

### State Initialization

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  let state = null

  return {
    "session.created": async ({ event }) => {
      // Initialize or load state
      state = await loadOrCreateState()
      await ctx.client.app.log("info", "State initialized")
    },

    "session.compacted": async ({ event }) => {
      // Reload after compaction
      state = await loadOrCreateState()
      await ctx.client.app.log("info", "State reloaded after compaction")
    },
  }
}
```

### Tool Tracking

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    "tool.execute.before": async ({ event }) => {
      await ctx.client.app.log("debug", JSON.stringify({
        type: "tool_start",
        tool: event.tool,
        args: event.args,
      }))
    },

    "tool.execute.after": async ({ event }) => {
      // Track file changes
      if (event.tool === "write" || event.tool === "edit") {
        await recordFileChange({
          path: event.args.path,
          tool: event.tool,
          timestamp: new Date().toISOString(),
        })
      }

      await ctx.client.app.log("debug", JSON.stringify({
        type: "tool_complete",
        tool: event.tool,
        success: event.success,
      }))
    },
  }
}
```

### Auto-Continue Pattern

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    "session.idle": async ({ event }) => {
      const mission = await loadMissionState()
      if (!mission) return

      const pendingTasks = mission.objectives
        .flatMap(o => o.tasks)
        .filter(t => t.status === "pending")

      if (pendingTasks.length > 0) {
        // Notify about pending work
        await ctx.client.tui.showToast({
          body: {
            message: `${pendingTasks.length} tasks pending`,
            type: "info",
          },
        })
      }
    },
  }
}
```

---

## Multi-Agent Dispatch

### Basic Dispatch

```typescript
import type { Plugin } from "@opencode-ai/plugin"

export const MyPlugin: Plugin = async (ctx) => {
  return {
    agent: {
      // Primary orchestrator
      commander: {
        mode: "primary",
        model: "anthropic/claude-opus-4-5",
        prompt: "You are Commander...",
      },

      // Specialists
      operator: {
        mode: "subagent",
        model: "anthropic/claude-sonnet-4",
        prompt: "You are Operator...",
      },

      validator: {
        mode: "subagent",
        model: "anthropic/claude-haiku-4-5",
        prompt: "You are Validator...",
      },
    },

    tool: {
      dispatch_operator: tool({
        description: "Dispatch a task to Operator for execution",
        args: {
          taskId: z.string(),
          context: z.string(),
        },
        async execute({ taskId, context }, ctx) {
          // Mark task as in_progress
          await updateTaskStatus(taskId, "in_progress")

          // Return dispatch instruction
          return {
            action: "invoke",
            agent: "operator",
            message: `Execute task ${taskId}:\n${context}`,
          }
        },
      }),
    },
  }
}
```

---

## Configuration Loading

```typescript
import type { Plugin } from "@opencode-ai/plugin"
import { z } from "zod"
import * as fs from "fs/promises"
import * as path from "path"

const ConfigSchema = z.object({
  commander: z.object({
    model: z.string().default("anthropic/claude-opus-4-5"),
    temperature: z.number().default(0.7),
  }),
  budget: z.object({
    limit: z.number().default(10.0),
    warnAt: z.number().default(0.7),
  }),
})

type Config = z.infer<typeof ConfigSchema>

async function loadConfig(directory: string): Promise<Config> {
  const globalPath = path.join(
    process.env.HOME || "",
    ".config/opencode/delta9.json"
  )
  const projectPath = path.join(directory, ".delta9/config.json")

  let global = {}
  let project = {}

  try {
    global = JSON.parse(await fs.readFile(globalPath, "utf-8"))
  } catch {}

  try {
    project = JSON.parse(await fs.readFile(projectPath, "utf-8"))
  } catch {}

  // Merge and validate
  return ConfigSchema.parse({ ...global, ...project })
}

export const MyPlugin: Plugin = async (ctx) => {
  const config = await loadConfig(ctx.directory)

  return {
    agent: {
      commander: {
        model: config.commander.model,
        temperature: config.commander.temperature,
        // ...
      },
    },
  }
}
```

---

## Complete Delta9-Style Example

```typescript
import type { Plugin } from "@opencode-ai/plugin"
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"
import * as fs from "fs/promises"
import * as path from "path"

// Schemas
const MissionSchema = z.object({
  id: z.string(),
  description: z.string(),
  status: z.enum(["planning", "active", "completed", "aborted"]),
  objectives: z.array(z.object({
    id: z.string(),
    description: z.string(),
    status: z.enum(["pending", "active", "completed"]),
    tasks: z.array(z.object({
      id: z.string(),
      description: z.string(),
      status: z.enum(["pending", "active", "completed", "failed"]),
    })),
  })),
})

type Mission = z.infer<typeof MissionSchema>

// State management
class MissionState {
  private mission: Mission | null = null
  private directory: string

  constructor(directory: string) {
    this.directory = directory
  }

  private get statePath() {
    return path.join(this.directory, ".delta9/mission.json")
  }

  async load(): Promise<Mission | null> {
    try {
      const data = await fs.readFile(this.statePath, "utf-8")
      this.mission = MissionSchema.parse(JSON.parse(data))
      return this.mission
    } catch {
      return null
    }
  }

  async save(): Promise<void> {
    if (!this.mission) return
    await fs.mkdir(path.dirname(this.statePath), { recursive: true })
    await fs.writeFile(this.statePath, JSON.stringify(this.mission, null, 2))
  }

  create(description: string): Mission {
    this.mission = {
      id: `mission_${Date.now()}`,
      description,
      status: "planning",
      objectives: [],
    }
    return this.mission
  }

  getMission() { return this.mission }
}

// Plugin
export const Delta9: Plugin = async (ctx) => {
  const { client, directory } = ctx
  const state = new MissionState(directory)

  // Load existing mission
  await state.load()

  // Tools
  const missionCreate = tool({
    description: "Create a new mission",
    args: {
      description: z.string().min(10).describe("Mission description"),
    },
    async execute({ description }, ctx) {
      const mission = state.create(description)
      await state.save()
      return { success: true, missionId: mission.id }
    },
  })

  const missionStatus = tool({
    description: "Get current mission status",
    args: {},
    async execute(args, ctx) {
      const mission = state.getMission()
      if (!mission) return { active: false }
      return {
        active: true,
        id: mission.id,
        status: mission.status,
        objectives: mission.objectives.length,
      }
    },
  })

  return {
    agent: {
      build: {
        description: "Commander - strategic planner",
        mode: "primary",
        model: "anthropic/claude-opus-4-5",
        temperature: 0.7,
        prompt: `You are Commander for Delta9...`,
      },
    },

    tool: {
      mission_create: missionCreate,
      mission_status: missionStatus,
    },

    "session.created": async ({ event }) => {
      await state.load()
      await client.app.log("info", "Delta9 initialized")
    },

    "session.compacted": async ({ event }) => {
      await state.load()
      await client.app.log("info", "State reloaded after compaction")
    },
  }
}

export default Delta9
```

---

## Reference

- [OpenCode Plugin Docs](https://opencode.ai/docs/plugins/)
- [Custom Tools Docs](https://opencode.ai/docs/custom-tools/)
- [oh-my-opencode](https://github.com/code-yeongyu/oh-my-opencode)
