# Delta9 Phase 1 Implementation Plan

## Overview

Building the **Foundation** layer of Delta9 - an OpenCode plugin implementing a hierarchical multi-agent system with strategic planning capabilities.

**Goal**: Get a working plugin with Commander → Operator → Validator flow, persistent mission state, and basic config system.

---

## Architecture Summary

```
User Request
     ↓
┌─────────────┐
│  Commander  │  ← Analyzes complexity, creates mission plan
└─────────────┘
     ↓
┌─────────────┐
│  Operator   │  ← Executes tasks (file edits, code changes)
└─────────────┘
     ↓
┌─────────────┐
│  Validator  │  ← Verifies work against acceptance criteria
└─────────────┘
     ↓
  mission.json  ← Persistent state (survives context compaction)
```

---

## Implementation Order

### Step 1: Project Setup
**Files to create:**
- `package.json` - Dependencies and scripts
- `tsconfig.json` - TypeScript configuration
- `.gitignore` - Standard ignores
- `README.md` - Basic documentation

**Actions:**
- Initialize git repository
- Install dependencies with bun/npm
- Create symlink to `~/.config/opencode/plugins/delta9`

---

### Step 2: Type Definitions (`src/types/`)

Create comprehensive TypeScript types based on the spec:

**`src/types/config.ts`**
```typescript
// Configuration types for delta9.json
interface Delta9Config {
  commander: CommanderConfig
  council: CouncilConfig
  operators: OperatorConfig
  validator: ValidatorConfig
  mission: MissionConfig
  budget: BudgetConfig
  // ... etc
}
```

**`src/types/mission.ts`**
```typescript
// Mission state types for mission.json
interface Mission {
  id: string
  description: string
  status: MissionStatus
  councilMode: CouncilMode
  councilSummary?: CouncilSummary
  objectives: Objective[]
  currentObjective: number
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
  acceptanceCriteria: string[]
  validation?: ValidationResult
  attempts: number
  dependencies: string[]
}
```

**`src/types/agents.ts`**
```typescript
// Agent types
type AgentRole = 'commander' | 'operator' | 'validator' | 'scout' | 'intel' | 'oracle'
type OperatorSpecialty = 'general' | 'ui-ops' | 'qa' | 'scribe' | 'patcher'

interface AgentDefinition {
  name: string
  role: AgentRole
  model: string
  temperature: number
  systemPrompt: string
  tools: string[]
}
```

**`src/types/events.ts`**
```typescript
// Plugin event types
interface MissionCreatedEvent { mission: Mission }
interface TaskCompletedEvent { task: Task; validation: ValidationResult }
interface ObjectiveCompletedEvent { objective: Objective }
// ... etc
```

**`src/types/index.ts`** - Re-export all types

---

### Step 3: Zod Schemas (`src/schemas/`)

Create validation schemas that mirror the types:

**`src/schemas/config.schema.ts`**
- Schema for `delta9.json` configuration
- Default values for all optional fields
- Validation rules (model names, temperature ranges, etc.)

**`src/schemas/mission.schema.ts`**
- Schema for `mission.json` state
- Strict validation of status transitions
- Acceptance criteria format validation

---

### Step 4: Configuration System (`src/lib/`)

**`src/lib/config.ts`**
```typescript
// Load and merge configs:
// 1. Default config (hardcoded)
// 2. Global: ~/.config/opencode/delta9.json
// 3. Project: .delta9/config.json

export function loadConfig(): Delta9Config
export function getConfig(): Delta9Config  // Cached getter
export function validateConfig(config: unknown): Delta9Config
```

**`src/lib/paths.ts`**
```typescript
// Path utilities
export const DELTA9_DIR = '.delta9'
export const MISSION_FILE = '.delta9/mission.json'
export const MISSION_MD = '.delta9/mission.md'
export const HISTORY_FILE = '.delta9/history.jsonl'
export const CONFIG_FILE = '.delta9/config.json'
export const GLOBAL_CONFIG = '~/.config/opencode/delta9.json'

export function ensureDelta9Dir(): void
export function getMissionPath(): string
```

**`src/lib/logger.ts`**
```typescript
// Logging utility using OpenCode's client.app.log()
export function log(level: 'debug' | 'info' | 'warn' | 'error', message: string): void
```

---

### Step 5: Mission State Manager (`src/mission/`)

**`src/mission/state.ts`** - Core state management
```typescript
export class MissionState {
  private mission: Mission | null = null

  // Lifecycle
  create(description: string, councilMode: CouncilMode): Mission
  load(): Mission | null
  save(): void
  clear(): void

  // Getters
  getMission(): Mission | null
  getObjective(id: string): Objective | null
  getTask(id: string): Task | null
  getNextTask(): Task | null
  getCurrentObjective(): Objective | null

  // Updates
  updateMission(updates: Partial<Mission>): void
  addObjective(objective: Omit<Objective, 'id'>): Objective
  updateObjective(id: string, updates: Partial<Objective>): void
  addTask(objectiveId: string, task: Omit<Task, 'id'>): Task
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

**`src/mission/markdown.ts`** - Generate human-readable mission.md
```typescript
export function generateMissionMarkdown(mission: Mission): string
// Produces formatted markdown with:
// - Mission overview
// - Objectives with status indicators
// - Tasks with acceptance criteria
// - Progress summary
```

**`src/mission/history.ts`** - Append-only audit log
```typescript
export function appendHistory(event: HistoryEvent): void
export function readHistory(): HistoryEvent[]
// Events: mission_created, task_started, task_completed, validation_failed, etc.
```

---

### Step 6: Commander Agent (`src/agents/commander.ts`)

The brain of the operation. For Phase 1, simplified without Council:

```typescript
export const commanderAgent: AgentDefinition = {
  name: 'commander',
  role: 'commander',
  model: 'claude-sonnet-4-20250514',  // Planning model
  temperature: 0.7,
  systemPrompt: `You are Commander, the strategic planning agent for Delta9.

Your responsibilities:
1. Analyze user requests to determine complexity
2. Break down work into objectives and tasks
3. Define clear acceptance criteria for each task
4. Monitor mission progress and adapt plans

When receiving a new request:
- Assess complexity (LOW/MEDIUM/HIGH/CRITICAL)
- Create structured mission with objectives
- Each objective should have 1-5 tasks
- Each task needs specific acceptance criteria

Output your plan as structured JSON that will be saved to mission.json.`,
  tools: ['mission_create', 'mission_update', 'dispatch_operator']
}
```

**Key behaviors:**
- Analyzes incoming requests
- Determines complexity level
- Creates mission structure with objectives/tasks
- Dispatches to Operator for execution
- Monitors progress via mission.json

---

### Step 7: Operator Agent (`src/agents/operator.ts`)

The execution workhorse:

```typescript
export const operatorAgent: AgentDefinition = {
  name: 'operator',
  role: 'operator',
  model: 'claude-sonnet-4-20250514',
  temperature: 0.3,  // Lower for precise execution
  systemPrompt: `You are Operator, the execution agent for Delta9.

You receive specific tasks from Commander with:
- Clear description of what to do
- Acceptance criteria to meet
- Context about the broader mission

Your job:
1. Execute the assigned task precisely
2. Make minimal, focused changes
3. Verify your work meets acceptance criteria
4. Report completion with summary of changes

Stay focused on your assigned task. Don't expand scope.`,
  tools: ['read', 'write', 'edit', 'bash', 'glob', 'grep', 'task_complete']
}
```

---

### Step 8: Validator Agent (`src/agents/validator.ts`)

Quality gate before task completion:

```typescript
export const validatorAgent: AgentDefinition = {
  name: 'validator',
  role: 'validator',
  model: 'claude-haiku-4-20250514',  // Fast and cheap
  temperature: 0.1,  // Strict evaluation
  systemPrompt: `You are Validator, the quality assurance agent for Delta9.

You review completed work against acceptance criteria.

For each task, you receive:
- Task description
- Acceptance criteria (checklist)
- Changes made by Operator

Your job:
1. Verify EACH acceptance criterion is met
2. Check for regressions or issues
3. Run tests if configured
4. Return verdict: PASS / FIXABLE / FAIL

PASS = All criteria met, work is complete
FIXABLE = Minor issues, can retry (max 2 attempts)
FAIL = Fundamental problems, need replanning

Be strict but fair. Don't nitpick style if not in criteria.`,
  tools: ['read', 'bash', 'validation_result']
}
```

---

### Step 9: Custom Tools (`src/tools/`)

**`src/tools/mission.ts`** - Mission management tools
```typescript
// mission_create - Create new mission
// mission_update - Update mission state
// mission_status - Get current mission status
// task_complete - Mark task as complete (triggers Validator)
```

**`src/tools/dispatch.ts`** - Agent dispatch tools
```typescript
// dispatch_operator - Send task to Operator
// dispatch_validator - Send completed work to Validator
```

**`src/tools/validation.ts`** - Validation tools
```typescript
// validation_result - Record validation result
// run_tests - Execute test suite
// check_lint - Run linter
```

---

### Step 10: Plugin Entry Point (`src/index.ts`)

```typescript
import type { Plugin } from "@opencode-ai/plugin"
import { loadConfig } from "./lib/config"
import { MissionState } from "./mission/state"
import { commanderAgent, operatorAgent, validatorAgent } from "./agents"
import { missionTools, dispatchTools, validationTools } from "./tools"

export const Delta9: Plugin = async (ctx) => {
  const config = loadConfig()
  const missionState = new MissionState()

  // Load existing mission if present
  missionState.load()

  return {
    // Replace default build agent with Commander
    agent: {
      build: commanderAgent,
      operator: operatorAgent,
      validator: validatorAgent,
    },

    // Register custom tools
    tool: {
      ...missionTools,
      ...dispatchTools,
      ...validationTools,
    },

    // Event hooks
    "session.created": async ({ event }) => {
      // Load mission state on session start
      missionState.load()
    },

    "session.idle": async ({ event }) => {
      // Check for pending tasks, maybe auto-continue
    },
  }
}

export default Delta9
```

---

### Step 11: Basic Commands (`src/commands/`)

**`src/commands/sitrep.ts`** - Mission status
```typescript
// /delta9 sitrep - Show current mission status
// Displays: objectives, tasks, progress, budget
```

**`src/commands/mission.ts`** - Mission control
```typescript
// /delta9 mission "description" - Start new mission
// /delta9 pause - Pause current mission
// /delta9 abort - Abort mission
```

---

## File Structure (Phase 1)

```
delta9/
├── src/
│   ├── index.ts                 # Plugin entry point
│   ├── types/
│   │   ├── index.ts
│   │   ├── config.ts
│   │   ├── mission.ts
│   │   ├── agents.ts
│   │   └── events.ts
│   ├── schemas/
│   │   ├── config.schema.ts
│   │   └── mission.schema.ts
│   ├── lib/
│   │   ├── config.ts
│   │   ├── paths.ts
│   │   └── logger.ts
│   ├── mission/
│   │   ├── state.ts
│   │   ├── markdown.ts
│   │   └── history.ts
│   ├── agents/
│   │   ├── index.ts
│   │   ├── commander.ts
│   │   ├── operator.ts
│   │   └── validator.ts
│   ├── tools/
│   │   ├── index.ts
│   │   ├── mission.ts
│   │   ├── dispatch.ts
│   │   └── validation.ts
│   └── commands/
│       ├── sitrep.ts
│       └── mission.ts
├── assets/
│   └── delta9.schema.json       # JSON schema for config
├── docs/
│   └── spec.md                  # Full specification
├── package.json
├── tsconfig.json
├── .gitignore
└── README.md
```

---

## Implementation Tasks

### Task 1: Project Scaffold
- [ ] Create package.json with dependencies
- [ ] Create tsconfig.json
- [ ] Create .gitignore
- [ ] Initialize git repo
- [ ] Install dependencies
- [ ] Create symlink for testing

### Task 2: Type Definitions
- [ ] Create src/types/config.ts
- [ ] Create src/types/mission.ts
- [ ] Create src/types/agents.ts
- [ ] Create src/types/events.ts
- [ ] Create src/types/index.ts

### Task 3: Zod Schemas
- [ ] Create src/schemas/config.schema.ts
- [ ] Create src/schemas/mission.schema.ts

### Task 4: Library Utilities
- [ ] Create src/lib/paths.ts
- [ ] Create src/lib/config.ts
- [ ] Create src/lib/logger.ts

### Task 5: Mission State Manager
- [ ] Create src/mission/state.ts
- [ ] Create src/mission/markdown.ts
- [ ] Create src/mission/history.ts

### Task 6: Agent Definitions
- [ ] Create src/agents/commander.ts
- [ ] Create src/agents/operator.ts
- [ ] Create src/agents/validator.ts
- [ ] Create src/agents/index.ts

### Task 7: Custom Tools
- [ ] Create src/tools/mission.ts
- [ ] Create src/tools/dispatch.ts
- [ ] Create src/tools/validation.ts
- [ ] Create src/tools/index.ts

### Task 8: Plugin Entry Point
- [ ] Create src/index.ts with full plugin structure

### Task 9: Commands
- [ ] Create src/commands/sitrep.ts
- [ ] Create src/commands/mission.ts

### Task 10: Testing & Documentation
- [ ] Basic README.md
- [ ] Manual testing with OpenCode
- [ ] Fix any issues discovered

---

## Dependencies

```json
{
  "dependencies": {
    "zod": "^3.22.0",
    "date-fns": "^3.0.0",
    "nanoid": "^5.0.0"
  },
  "devDependencies": {
    "@opencode-ai/plugin": "^1.0.0",
    "@types/node": "^20.0.0",
    "typescript": "^5.3.0",
    "vitest": "^1.0.0"
  },
  "peerDependencies": {
    "opencode": ">=1.0.150"
  }
}
```

---

## Success Criteria for Phase 1

1. **Plugin loads** in OpenCode without errors
2. **Config system** loads and merges configs correctly
3. **Mission state** persists across sessions via mission.json
4. **Commander** can analyze requests and create mission plans
5. **Operator** can execute simple tasks
6. **Validator** can verify work against acceptance criteria
7. **Basic flow** works: Request → Plan → Execute → Validate → Complete
8. **/delta9 sitrep** shows mission status

---

## Notes

- **No Council in Phase 1** - Commander works alone, Council comes in Phase 2
- **No Support Agents** - Scout, Intel, etc. come in Phase 3
- **No Checkpoints** - Git-based rollback comes in Phase 4
- **No Budget Tracking** - Cost awareness comes in Phase 4
- **Single Operator** - Specialized operators (UI-Ops, QA, etc.) come in Phase 3

Focus on getting the core loop working reliably before adding complexity.
