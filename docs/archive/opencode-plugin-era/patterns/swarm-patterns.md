# SWARM Plugin Patterns

Reference documentation from dissecting the OpenCode SWARM plugin.

---

## Overview

SWARM is a multi-agent coordination system for OpenCode that enables parallel task execution with file isolation and conflict prevention. It uses a **Coordinator + Workers** architecture where:

- **Coordinator**: Decomposes tasks, spawns workers, reviews results
- **Workers**: Execute subtasks in isolation, reserve files, report progress

**Key Philosophy**: "Coordinators orchestrate, workers implement"

---

## Core Patterns

### 1. Task Decomposition (CellTree Schema)

**File**: `src/swarm-decompose.ts`

SWARM breaks down tasks into a structured **CellTree** with an epic (parent) and subtasks (children).

```typescript
interface CellTree {
  epic: {
    title: string
    description?: string
    id?: string // Custom ID like 'phase-0'
  }
  subtasks: Array<{
    title: string
    files: string[] // Files this subtask will modify
    priority?: number
    id_suffix?: string // e.g., 'e2e-test' becomes 'phase-0.e2e-test'
  }>
}
```

**Key Tools**:
- `swarm_decompose` - Create decomposition plan
- `swarm_validate_decomposition` - Validate for conflicts
- `swarm_delegate_planning` - Delegate planning to a subagent

**File Conflict Detection**:
```typescript
function detectFileConflicts(subtasks) {
  const fileAssignments = new Map()
  for (const subtask of subtasks) {
    for (const file of subtask.files) {
      if (fileAssignments.has(file)) {
        // CONFLICT - same file in multiple subtasks
      }
    }
  }
}
```

**Instruction Conflict Detection**:
- Checks for contradictory instructions between subtasks
- Matches "must/always/required" vs "never/dont/avoid" patterns

---

### 2. Decomposition Strategies

**File**: `src/swarm-strategies.ts`

Four decomposition strategies with keyword-based auto-selection:

| Strategy | Use Case | Keywords |
|----------|----------|----------|
| **file-based** | Refactoring, migrations, pattern changes | refactor, migrate, update all, rename |
| **feature-based** | New features, vertical slices | add, implement, build, create, feature |
| **risk-based** | Bug fixes, security, critical changes | fix, bug, security, vulnerability, hotfix |
| **research-based** | Investigation, discovery, learning | research, investigate, explore, analyze |

**Strategy Selection**:
```typescript
async function selectStrategy(task: string, projectKey?: string) {
  // 1. Score each strategy based on keyword matches
  // 2. Query precedent from past decisions (if projectKey)
  // 3. Adjust confidence based on success rates
  // 4. Return winner with reasoning
}
```

**Precedent-Aware Selection**:
- Queries past strategy decisions for similar tasks
- Adjusts confidence based on historical success rates
- Cites similar epic IDs for reference

---

### 3. Git Worktree Isolation

**File**: `src/swarm-worktree.ts`

Workers execute in isolated git worktrees to prevent file conflicts.

**How It Works**:
1. Create a shared "start commit" before spawning workers
2. Each worker gets their own worktree at that commit
3. Workers can modify files without affecting others
4. Cherry-pick commits back to main branch on completion

**Tools**:
- `swarm_worktree_create` - Create isolated worktree for worker
- `swarm_worktree_merge` - Merge worktree commits back to main
- `swarm_worktree_cleanup` - Clean up completed worktrees
- `swarm_worktree_list` - List active worktrees

**Benefits**:
- No file reservation needed (true isolation)
- Parallel editing of same files possible
- Git handles merge conflicts at completion time

---

### 4. Coordinator Guard

**File**: `src/coordinator-guard.ts`

Runtime enforcement that **BLOCKS** coordinators from doing worker tasks.

**Blocked Actions for Coordinators**:
| Tool | Why Blocked |
|------|-------------|
| `Edit`, `Write` | Coordinators don't write code |
| `bash` (for tests) | Workers run tests, coordinators review |
| `swarmmail_reserve` | Workers reserve files before editing |

**Implementation**:
```typescript
const VIOLATION_PATTERNS = {
  FILE_MODIFICATION_TOOLS: ['edit', 'write'],
  RESERVATION_TOOLS: ['swarmmail_reserve', 'agentmail_reserve'],
  TEST_EXECUTION_PATTERNS: [
    /\bbun\s+test\b/i,
    /\bnpm\s+(run\s+)?test/i,
    /\bjest\b/i,
    /\bvitest\b/i,
  ]
}

function detectCoordinatorViolation(params) {
  if (params.agentContext !== 'coordinator') return

  if (VIOLATION_PATTERNS.FILE_MODIFICATION_TOOLS.includes(toolName)) {
    // BLOCK and capture violation event
    throw new CoordinatorGuardError('Coordinators should spawn workers')
  }
}
```

**Error Messages Include Suggestions**:
- "Use swarm_spawn_subtask instead of editing directly"
- "Workers run tests, use swarm_review to check results"

---

### 5. Swarm Mail (Agent Messaging)

**File**: `src/swarm-mail.ts`

Event-sourced messaging system for agent coordination.

**Key Features**:
- **File Reservations**: Prevent concurrent file edits
- **Message Threading**: Link messages to epics/subtasks
- **Importance Levels**: low, normal, high, urgent
- **Acknowledgements**: Confirm receipt of critical messages

**Tools**:
| Tool | Purpose |
|------|---------|
| `swarmmail_init` | Initialize agent session |
| `swarmmail_send` | Send message to other agents |
| `swarmmail_inbox` | Check inbox (max 5, no bodies) |
| `swarmmail_read_message` | Read single message body |
| `swarmmail_reserve` | Reserve file paths |
| `swarmmail_release` | Release reservations |
| `swarmmail_ack` | Acknowledge message |

**Context Preservation**:
```typescript
const MAX_INBOX_LIMIT = 5  // HARD CAP
// Inbox always excludes bodies to save context
// Use swarmmail_read_message for individual bodies
```

---

### 6. Hive Issue Tracking

**File**: `src/hive.ts`

Cell-based issue tracking system (replaces "beads" terminology).

**Cell Types**:
- `epic` - Parent container for subtasks
- `task` - Individual work item
- `bug`, `feature`, `chore` - Categorization

**Cell Lifecycle**:
```
open → in_progress → blocked/closed
```

**Key Tools**:
| Tool | Purpose |
|------|---------|
| `hive_create` | Create single cell |
| `hive_create_epic` | Atomic epic + subtasks creation |
| `hive_query` | Query cells with filters |
| `hive_update` | Update status/description |
| `hive_close` | Close with reason |
| `hive_start` | Mark as in_progress |
| `hive_sync` | Persist to git |

**Atomic Epic Creation**:
```typescript
// Creates epic and all subtasks atomically
// Rolls back on partial failure
const result = await hive_create_epic({
  epic_title: 'Implement auth',
  subtasks: [
    { title: 'Create service', files: ['src/auth/service.ts'] },
    { title: 'Add endpoints', files: ['src/api/auth.ts'] },
  ]
})
```

---

### 7. Context Compaction Handling

**File**: `src/compaction-hook.ts`

Preserves swarm state across session compaction.

**SWARM_COMPACTION_CONTEXT**:
```typescript
const SWARM_COMPACTION_CONTEXT = `
You are returning to a swarm coordination session that was compacted.

## Your Role as Coordinator
- You ORCHESTRATE work, you do NOT implement
- You spawn workers using swarm_spawn_subtask
- You track progress using swarm_status
- You review work using swarm_review

## Continuation Steps
1. Run swarm_status to see current epic progress
2. Check for completed/blocked subtasks
3. Spawn workers for remaining open subtasks
4. Use swarm_complete when all subtasks done
`
```

**Active Swarm Detection**:
```typescript
async function detectActiveSwarm() {
  // Check multiple signals:
  // 1. Active file reservations
  // 2. Open cells in hive
  // 3. Recent swarm messages
  // 4. Spawned agent processes

  // Philosophy: "Err on the side of continuation"
}
```

---

### 8. Output Guardrails

**File**: `src/output-guardrails.ts`

Prevents MCP tools from blowing out context with massive responses.

**Strategy**:
- Default limit: 32,000 chars (~8,000 tokens)
- Higher limits for code/doc tools: 64,000 chars
- Skip truncation for internal tools (hive_*, swarmmail_*)

**Smart Truncation**:
```typescript
function truncateWithBoundaries(text, maxChars) {
  // Preserve structure:
  // - Find matching JSON braces
  // - Keep code block ``` boundaries
  // - Cut at markdown ## headers when possible
  // - Don't cut mid-word
}
```

---

### 9. Planning Guardrails

**File**: `src/planning-guardrails.ts`

Detects planning anti-patterns and warns agents.

**TodoWrite Warning**:
- Triggers when 6+ todos created in one call
- Most todos match file modification patterns
- Suggests using swarm instead of todowrite

**Violation Detection**:
```typescript
interface ViolationDetectionResult {
  violationType:
    | 'coordinator_edited_file'
    | 'coordinator_ran_tests'
    | 'coordinator_reserved_files'
    | 'no_worker_spawned'
    | 'worker_completed_without_review'
}
```

---

### 10. Worker Handoff Contract

**File**: `src/swarm-orchestrate.ts`

Machine-readable contract for worker spawning.

```typescript
interface WorkerHandoff {
  contract: {
    task_id: string
    files_owned: string[]      // Files worker can modify
    files_readonly: string[]   // Files worker can read only
    dependencies_completed: string[]
    success_criteria: string[]
  }
  context: {
    epic_summary: string
    your_role: string
    what_others_did: string
    what_comes_next: string
  }
  escalation: {
    blocked_contact: string    // Who to contact if blocked
    scope_change_protocol: string
  }
}
```

**Contract Validation**:
```typescript
function validateContract(files_touched, files_owned) {
  // Check files_touched is subset of files_owned
  // Supports glob patterns (e.g., 'src/auth/**')
  // Returns violations list
}
```

---

## Module Organization

```
swarm-plugin/src/
├── swarm.ts              # Main re-export module
├── swarm-decompose.ts    # Task decomposition (CellTree)
├── swarm-strategies.ts   # Strategy selection
├── swarm-orchestrate.ts  # Status, progress, completion
├── swarm-prompts.ts      # Prompt templates
├── swarm-worktree.ts     # Git worktree isolation
├── swarm-mail.ts         # Agent messaging (embedded)
├── swarm-review.ts       # Adversarial review
├── swarm-verify.ts       # Verification gates
├── hive.ts               # Issue tracking (cells)
├── coordinator-guard.ts  # Runtime violation enforcement
├── compaction-hook.ts    # Context preservation
├── output-guardrails.ts  # Response truncation
├── planning-guardrails.ts # Anti-pattern detection
├── agent-mail.ts         # MCP client (deprecated)
└── schemas/              # Zod schemas
```

---

## Key Design Decisions

### 1. Files as Contract
Every subtask explicitly lists which files it will modify. This enables:
- Conflict detection at decomposition time
- File reservation for parallel execution
- Contract validation at completion

### 2. Coordinator vs Worker Separation
Strict enforcement that coordinators don't write code:
- Runtime guards block edit/write tools for coordinators
- Clear escalation paths for workers
- Review system for quality gates

### 3. Event Sourcing
All state changes are recorded as events:
- Full audit trail
- Replay capability for learning
- Precedent-aware decision making

### 4. Graceful Degradation
Tools check availability before use:
- Agent Mail optional (swarm works without coordination)
- Worktree optional (falls back to reservation)
- Skills optional (enhances but doesn't require)

---

## Delta9 Application

### Already Have
- Mission state (similar to Hive cells)
- Commander + Operators (similar to Coordinator + Workers)
- Validator (similar to review system)

### Should Add
1. **File Assignment in Tasks**
   - Add `files` array to task definitions
   - Detect conflicts at planning time

2. **Coordinator Guard**
   - Block Commander from using Edit/Write tools
   - Runtime enforcement, not just prompt instructions

3. **Worker Handoff Contract**
   - Typed contract for task delegation
   - Explicit files_owned, success_criteria, escalation

4. **Compaction Context**
   - Preserve mission state across compaction
   - Inject continuation instructions

5. **Output Guardrails**
   - Truncate large tool responses
   - Smart boundary detection

---

## File References

| Pattern | File | Key Functions |
|---------|------|---------------|
| Task Decomposition | `swarm-decompose.ts` | `swarm_decompose`, `detectFileConflicts` |
| Strategy Selection | `swarm-strategies.ts` | `selectStrategy`, `formatStrategyGuidelines` |
| Worktree Isolation | `swarm-worktree.ts` | `swarm_worktree_create`, `getStartCommit` |
| Coordinator Guard | `coordinator-guard.ts` | `checkCoordinatorViolation` |
| Swarm Mail | `swarm-mail.ts` | `swarmmail_*` tools |
| Hive Cells | `hive.ts` | `hive_create_epic`, `getHiveAdapter` |
| Compaction | `compaction-hook.ts` | `SWARM_COMPACTION_CONTEXT` |
| Output Guards | `output-guardrails.ts` | `guardrailOutput`, `truncateWithBoundaries` |
| Planning Guards | `planning-guardrails.ts` | `detectCoordinatorViolation` |
| Worker Handoff | `swarm-orchestrate.ts` | `generateWorkerHandoff`, `validateContract` |
