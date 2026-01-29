# Delta9 Internal API Reference

> TypeScript types, functions, and interfaces for Delta9 development.

---

## Type Definitions

### Mission Types

```typescript
// src/types/mission.ts

type MissionStatus = "planning" | "approved" | "in_progress" | "completed" | "aborted"
type ObjectiveStatus = "pending" | "in_progress" | "completed"
type TaskStatus = "pending" | "in_progress" | "completed" | "failed"
type CouncilMode = "none" | "quick" | "standard" | "xhigh"

interface Mission {
  id: string
  description: string
  status: MissionStatus
  councilMode: CouncilMode
  councilSummary?: CouncilSummary
  objectives: Objective[]
  budget: BudgetTracking
  createdAt: string
  updatedAt: string
}

interface Objective {
  id: string
  description: string
  status: ObjectiveStatus
  tasks: Task[]
  checkpoint?: string
}

interface Task {
  id: string
  description: string
  status: TaskStatus
  assignee?: AgentType
  routedTo?: string
  acceptanceCriteria: string[]
  validation?: ValidationResult
  attempts: number
  dependencies: string[]
  filesChanged?: string[]
  tokensUsed?: number
  cost?: number
}

interface CouncilSummary {
  consensus: string[]
  disagreementsResolved: string[]
  confidenceAvg: number
  opinions: OracleOpinion[]
}

interface OracleOpinion {
  oracle: string
  recommendation: string
  confidence: number
  caveats: string[]
}

interface ValidationResult {
  status: "passed" | "fixable" | "failed"
  validatedAt: string
  summary: string
  feedback?: string
}

interface BudgetTracking {
  limit: number
  spent: number
  breakdown: {
    council: number
    operators: number
    validators: number
    support: number
  }
}
```

### Agent Types

```typescript
// src/types/agents.ts

type AgentRole =
  | "commander"
  | "oracle"
  | "operator"
  | "validator"
  | "patcher"
  | "scout"
  | "intel"
  | "strategist"
  | "ui-ops"
  | "scribe"
  | "optics"
  | "qa"

type AgentLayer = "command" | "council" | "execution" | "support"

interface AgentDefinition {
  name: string
  role: AgentRole
  layer: AgentLayer
  model: string
  temperature: number
  systemPrompt: string
  tools: string[]
  permissions?: Record<string, "allow" | "ask" | "deny">
}
```

### Config Types

```typescript
// src/types/config.ts

interface Delta9Config {
  commander: CommanderConfig
  council: CouncilConfig
  operators: OperatorConfig
  validator: ValidatorConfig
  patcher: PatcherConfig
  support: SupportConfig
  mission: MissionConfig
  memory: MemoryConfig
  budget: BudgetConfig
  notifications: NotificationConfig
  ui: UIConfig
  seamless: SeamlessConfig
}

interface CommanderConfig {
  model: string
  temperature: number
  planningModel: string
  dispatchModel: string
}

interface CouncilConfig {
  enabled: boolean
  defaultMode: CouncilMode
  autoDetectComplexity: boolean
  members: OracleConfig[]
  parallel: boolean
  requireConsensus: boolean
  minResponses: number
  timeoutSeconds: number
}

interface OracleConfig {
  name: string
  model: string
  enabled: boolean
  specialty: string
}
```

---

## Mission State API

### MissionState Class

```typescript
// src/mission/state.ts

class MissionState {
  private mission: Mission | null = null
  private directory: string

  constructor(directory: string)

  // Lifecycle
  async create(description: string, councilMode: CouncilMode): Promise<Mission>
  async load(): Promise<Mission | null>
  async save(): Promise<void>
  async clear(): Promise<void>

  // Getters
  getMission(): Mission | null
  getObjective(id: string): Objective | null
  getTask(id: string): Task | null
  getNextTask(): Task | null
  getCurrentObjective(): Objective | null

  // Updates
  updateMission(updates: Partial<Mission>): void
  addObjective(objective: Omit<Objective, "id">): Objective
  updateObjective(id: string, updates: Partial<Objective>): void
  addTask(objectiveId: string, task: Omit<Task, "id">): Task
  updateTask(id: string, updates: Partial<Task>): void

  // Status transitions
  startTask(id: string, assignee: AgentType): void
  completeTask(id: string, validation: ValidationResult): void
  failTask(id: string, reason: string): void

  // Queries
  getBlockedTasks(): Task[]
  getReadyTasks(): Task[]
  getProgress(): { completed: number; total: number; percentage: number }
}
```

### Usage Example

```typescript
const state = new MissionState(directory)

// Load existing or create new
let mission = await state.load()
if (!mission) {
  mission = await state.create("Build auth system", "standard")
}

// Add objective
const objective = state.addObjective({
  description: "Set up project structure",
  status: "pending",
  tasks: [],
})

// Add task
const task = state.addTask(objective.id, {
  description: "Initialize Next.js project",
  status: "pending",
  acceptanceCriteria: [
    "package.json exists",
    "TypeScript configured",
  ],
  attempts: 0,
  dependencies: [],
})

// Update task status
state.startTask(task.id, "operator")

// Complete with validation
state.completeTask(task.id, {
  status: "passed",
  validatedAt: new Date().toISOString(),
  summary: "All criteria met",
})

// Persist
await state.save()
```

---

## Custom Tools

### Tool Definition Pattern

```typescript
// src/tools/mission.ts

import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export const missionCreate = tool({
  description: "Create a new mission with objectives",
  args: {
    description: z.string().min(10).describe("Mission description"),
    councilMode: z.enum(["none", "quick", "standard", "xhigh"])
      .default("standard")
      .describe("Council deliberation mode"),
  },
  async execute({ description, councilMode }, ctx) {
    const state = getMissionState()
    const mission = await state.create(description, councilMode)
    await state.save()

    return {
      success: true,
      missionId: mission.id,
      councilMode,
    }
  },
})

export const missionStatus = tool({
  description: "Get current mission status",
  args: {},
  async execute(args, ctx) {
    const state = getMissionState()
    const mission = state.getMission()

    if (!mission) {
      return { active: false }
    }

    const progress = state.getProgress()

    return {
      active: true,
      id: mission.id,
      status: mission.status,
      progress: `${progress.completed}/${progress.total} (${progress.percentage}%)`,
      currentObjective: state.getCurrentObjective()?.description,
    }
  },
})

export const taskComplete = tool({
  description: "Mark a task as complete, triggers validation",
  args: {
    taskId: z.string().describe("Task ID to complete"),
    summary: z.string().describe("Summary of changes made"),
    filesChanged: z.array(z.string()).describe("List of files modified"),
  },
  async execute({ taskId, summary, filesChanged }, ctx) {
    const state = getMissionState()
    const task = state.getTask(taskId)

    if (!task) {
      throw new Error(`Task ${taskId} not found`)
    }

    // Update task with changes
    state.updateTask(taskId, {
      filesChanged,
    })

    // Trigger validation (returns instruction for agent)
    return {
      action: "validate",
      taskId,
      summary,
      filesChanged,
      acceptanceCriteria: task.acceptanceCriteria,
    }
  },
})
```

---

## Hook Handlers

### Session Hooks

```typescript
// src/hooks/session.ts

export const sessionCreated: HookHandler = async ({ event }, ctx) => {
  const state = getMissionState()
  await state.load()
  await ctx.client.app.log("info", "Delta9 initialized")
}

export const sessionCompacted: HookHandler = async ({ event }, ctx) => {
  const state = getMissionState()
  await state.load()
  await ctx.client.app.log("info", "State reloaded after compaction")
}

export const sessionIdle: HookHandler = async ({ event }, ctx) => {
  const state = getMissionState()
  const mission = state.getMission()

  if (!mission || mission.status !== "in_progress") return

  const nextTask = state.getNextTask()
  if (nextTask) {
    await ctx.client.tui.showToast({
      body: {
        message: `Next task: ${nextTask.description}`,
        type: "info",
      },
    })
  }
}
```

### Tool Hooks

```typescript
// src/hooks/tools.ts

export const toolExecuteAfter: HookHandler = async ({ event }, ctx) => {
  // Track file changes for mission state
  if (event.tool === "write" || event.tool === "edit") {
    await recordFileChange({
      path: event.args.path,
      tool: event.tool,
      timestamp: new Date().toISOString(),
    })
  }

  // Log token usage
  if (event.usage) {
    await updateBudget(event.usage)
  }
}
```

---

## Configuration Loading

```typescript
// src/lib/config.ts

import { z } from "zod"
import * as fs from "fs/promises"
import * as path from "path"

const ConfigSchema = z.object({
  commander: z.object({
    model: z.string().default("anthropic/claude-opus-4-5"),
    temperature: z.number().default(0.7),
  }),
  council: z.object({
    enabled: z.boolean().default(true),
    defaultMode: z.enum(["none", "quick", "standard", "xhigh"]).default("standard"),
    members: z.array(z.object({
      name: z.string(),
      model: z.string(),
      enabled: z.boolean(),
    })).default([]),
  }),
  // ... more fields
})

export async function loadConfig(directory: string): Promise<Delta9Config> {
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

  return ConfigSchema.parse(deepMerge(DEFAULT_CONFIG, global, project))
}
```

---

## Utility Functions

### Path Helpers

```typescript
// src/lib/paths.ts

export const DELTA9_DIR = ".delta9"
export const MISSION_FILE = ".delta9/mission.json"
export const MISSION_MD = ".delta9/mission.md"
export const HISTORY_FILE = ".delta9/history.jsonl"
export const CONFIG_FILE = ".delta9/config.json"

export function getMissionPath(directory: string): string {
  return path.join(directory, MISSION_FILE)
}

export async function ensureDelta9Dir(directory: string): Promise<void> {
  await fs.mkdir(path.join(directory, DELTA9_DIR), { recursive: true })
}
```

### Structured Logger

```typescript
// src/lib/logger.ts

type LogLevel = 'debug' | 'info' | 'warn' | 'error'

interface Logger {
  debug(message: string, data?: Record<string, unknown>): void
  info(message: string, data?: Record<string, unknown>): void
  warn(message: string, data?: Record<string, unknown>): void
  error(message: string, data?: Record<string, unknown>): void
  child(context: Record<string, unknown>): Logger
}

// Create logger with OpenCode client
function createLogger(
  client?: OpenCodeClient,
  context?: Record<string, unknown>,
  minLevel?: LogLevel
): Logger

// Initialize default logger (call during plugin init)
function initLogger(client?: OpenCodeClient, minLevel?: LogLevel): void

// Get named logger for a component
function getNamedLogger(component: string): Logger
// Output: "12:34:56.789 [delta9:background] [INFO] Task started | taskId=bg_123"

// Convenience functions
function debug(message: string, data?: Record<string, unknown>): void
function info(message: string, data?: Record<string, unknown>): void
function warn(message: string, data?: Record<string, unknown>): void
function error(message: string, data?: Record<string, unknown>): void
```

#### Usage Example

```typescript
import { initLogger, getNamedLogger } from './lib/logger.js'

// During plugin initialization
initLogger(client)

// In components
const log = getNamedLogger('background')
log.info('Task started', { taskId: 'bg_123', agent: 'operator' })
// Output: 12:34:56.789 [delta9:background] [INFO] Task started | taskId=bg_123 agent=operator

// Create child logger with additional context
const taskLog = log.child({ taskId: 'bg_123' })
taskLog.debug('Processing')  // Includes taskId automatically
```

---

### Rich Error Handling

```typescript
// src/lib/errors.ts

class Delta9Error extends Error {
  code: string
  suggestions: string[]
  context?: Record<string, unknown>

  constructor(opts: {
    code: string
    message: string
    suggestions?: string[]
    context?: Record<string, unknown>
  })

  toJSON(): { error: true; code: string; message: string; suggestions: string[]; context?: Record<string, unknown> }
  toToolResponse(): string  // JSON stringified for tool returns
}

// Predefined error factories
const errors = {
  // Task errors
  taskNotFound: (taskId: string) => Delta9Error
  taskCancelFailed: (taskId: string, status: string) => Delta9Error
  taskAlreadyComplete: (taskId: string) => Delta9Error
  taskAlreadyFailed: (taskId: string) => Delta9Error

  // Mission errors
  noActiveMission: () => Delta9Error
  missionAlreadyExists: (missionId: string) => Delta9Error
  objectiveNotFound: (objectiveId: string) => Delta9Error
  missionTaskNotFound: (taskId: string) => Delta9Error

  // Validation errors
  validationPending: (taskId: string) => Delta9Error
  noValidationPending: (taskId: string) => Delta9Error

  // Config errors
  configInvalid: (details: string) => Delta9Error
  sdkUnavailable: () => Delta9Error
}

// Type guard
function isDelta9Error(error: unknown): error is Delta9Error

// Format any error for tool response
function formatErrorResponse(error: unknown): string
```

#### Usage Example

```typescript
import { errors, isDelta9Error, formatErrorResponse } from './lib/errors.js'

// In tool execute function
const task = manager.getTask(taskId)
if (!task) {
  return errors.taskNotFound(taskId).toToolResponse()
  // Returns: {"error":true,"code":"TASK_NOT_FOUND","message":"Task bg_123 not found",
  //           "suggestions":["Use background_list to see available tasks",
  //                          "Task may have been cleaned up (30min TTL)",
  //                          "Check if task ID is correct (format: bg_xxxxx)"]}
}

// Error handling
try {
  await riskyOperation()
} catch (err) {
  if (isDelta9Error(err)) {
    log.error(err.message, { code: err.code, context: err.context })
  }
  return formatErrorResponse(err)
}
```

---

### Context-Aware Hints

```typescript
// src/lib/hints.ts

// Static hints
const hints = {
  // Background tasks
  noTasks: string
  noRunningTasks: string
  tasksAllComplete: string
  taskFailed: (agent: string) => string
  taskStale: (taskId: string) => string

  // Mission
  noMission: string
  missionComplete: string
  missionBlocked: string
  noObjectives: string
  missionNoTasks: string
  tasksNeedValidation: string

  // Council
  councilEmpty: string
  councilPartial: (count: number, total: number) => string
  quickConsultAvailable: string

  // Delegation
  simulationMode: string
  agentRecommendation: (complexity: string) => string
  backgroundRecommendation: string

  // Validation
  validationPending: string
  allTasksValidated: string
  validationFailed: string

  // Memory
  memoryEmpty: string
  memoryAvailable: (count: number) => string

  // Config
  usingDefaults: string
  configLoaded: string
}

// Context-based hint selection
interface HintContext {
  totalTasks?: number
  runningTasks?: number
  failedTasks?: number
  hasMission?: boolean
  missionStatus?: string
  oracleCount?: number
  sdkAvailable?: boolean
  memoryKeyCount?: number
  configLoaded?: boolean
}

function getHint(context: HintContext): string | undefined

// Tool-specific hint helpers
function getBackgroundListHint(running: number, completed: number, failed: number, total: number): string | undefined
function getMissionStatusHint(hasMission: boolean, status?: string, taskCount?: number, pendingValidation?: number): string | undefined
function getCouncilStatusHint(oracleCount: number): string | undefined
```

#### Usage Example

```typescript
import { getBackgroundListHint } from './lib/hints.js'

// In background_list tool
const hint = getBackgroundListHint(counts.running, counts.completed, counts.failed, counts.total)

return JSON.stringify({
  success: true,
  summary: '...',
  tasks: [...],
  hint,  // "All tasks completed successfully. Use background_cleanup to remove old tasks."
})
```

---

### Diagnostics Tool

```typescript
// src/tools/diagnostics.ts

const delta9_health = tool({
  description: `Check Delta9 system health and configuration.

**Purpose:** Diagnose issues and verify system status.

**Reports on:**
- SDK connection status (live vs simulation mode)
- Mission state (active, tasks, status)
- Background task pool (active, pending, capacity)
- Configuration validity
- System uptime

**Use when:**
- Troubleshooting why tasks aren't running
- Verifying configuration is loaded correctly
- Checking system status before starting work
- Debugging unexpected behavior

**Related:** background_list, mission_status, council_status`,

  args: {
    verbose: s.boolean().optional()
      .describe('Include detailed diagnostics (task history, mission history)'),
  },

  async execute(args, _ctx) {
    // Returns health report JSON
  },
})
```

#### Health Report Structure

```json
{
  "status": "healthy",  // or "degraded", "unhealthy"
  "statusEmoji": "✅",  // or "⚠️", "❌"
  "timestamp": "2026-01-24T12:00:00.000Z",
  "uptime": "2.5h",

  "sdk": {
    "available": true,
    "mode": "live"  // or "simulation"
  },

  "mission": {
    "active": true,
    "id": "mission_abc123",
    "status": "in_progress",
    "taskCount": 5,
    "completedTasks": 2,
    "pendingTasks": 2,
    "inProgressTasks": 1
  },

  "backgroundTasks": {
    "active": 2,
    "pending": 1,
    "maxConcurrency": 3,
    "utilization": "67%",
    "totalTasks": 10,
    "completedTasks": 7,
    "failedTasks": 0,
    "cancelledTasks": 0
  },

  "config": {
    "loaded": true,
    "valid": true,
    "errors": []
  },

  "summary": [
    "System is healthy",
    "Mission active with 5 task(s)",
    "2 background task(s) running",
    "1 task(s) queued"
  ],

  // Only with verbose=true
  "details": {
    "recentTasks": [...],
    "missionTasks": [...]
  }
}
```

### History

```typescript
// src/mission/history.ts

interface HistoryEvent {
  type: string
  timestamp: string
  data: Record<string, unknown>
}

export async function appendHistory(
  directory: string,
  event: Omit<HistoryEvent, "timestamp">
): Promise<void> {
  const entry: HistoryEvent = {
    ...event,
    timestamp: new Date().toISOString(),
  }
  const line = JSON.stringify(entry)
  await fs.appendFile(
    path.join(directory, HISTORY_FILE),
    line + "\n"
  )
}
```

---

## Enhanced Tool Outputs

### Status Emoji Indicators

All status-related tools use consistent emoji indicators:

| Status | Emoji | Meaning |
|--------|-------|---------|
| pending | ⏳ | Waiting in queue |
| running | 🔄 | Currently executing |
| completed | ✅ | Finished successfully |
| failed | ❌ | Encountered an error |
| cancelled | 🚫 | Stopped by user |

### Duration Formatting

Duration values are human-readable:
- `< 1s`: `"450ms"`
- `< 1m`: `"12.5s"`
- `< 1h`: `"3.5m"`
- `≥ 1h`: `"1.2h"`

### background_list Output Example

```json
{
  "success": true,
  "summary": "🔄 2 running | ⏳ 1 pending | ✅ 5 done",
  "tasks": [
    {
      "id": "bg_abc123",
      "status": "🔄 running",
      "agent": "operator",
      "duration": "45.2s",
      "missionTaskId": "task_1"
    },
    {
      "id": "bg_def456",
      "status": "✅ completed",
      "agent": "validator",
      "duration": "12.3s"
    }
  ],
  "counts": {
    "pending": 1,
    "running": 2,
    "completed": 5,
    "failed": 0,
    "cancelled": 0,
    "total": 8
  },
  "pool": {
    "active": 2,
    "pending": 1,
    "maxConcurrency": 3,
    "utilization": "67%"
  },
  "hint": null
}
```

### delegate_task Enhanced Description

```typescript
const delegate_task = tool({
  description: `Spawn a specialized agent for task execution.

**Purpose:** Offload work to background agents for parallel exploration or synchronous execution.

**Agents:**
- operator: General implementation (default)
- operator_complex: Multi-file changes
- validator: Verify against criteria
- explorer: Codebase exploration
- scout: Quick reconnaissance

**Examples:**
- Background: delegate_task(prompt="Fix auth bug", run_in_background=true)
- Sync: delegate_task(prompt="Add tests", agent="validator")
- With context: delegate_task(prompt="Implement feature", taskId="task_123")

**Related:** background_output, background_list, background_cancel`,
  // ...
})
```

---

## Reference

- OpenCode SDK: `OPENCODE_REFERENCE/07_SDK.md`
- Custom Tools: `OPENCODE_REFERENCE/03_TOOLS.md`
- Plugin Examples: `PATTERNS/PLUGIN_EXAMPLES.md`
