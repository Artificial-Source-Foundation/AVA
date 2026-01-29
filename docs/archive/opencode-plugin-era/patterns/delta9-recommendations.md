# Delta9 Implementation Recommendations

Synthesized recommendations from SWARM and oh-my-opencode pattern analysis.

---

## Pattern Comparison Summary

### Delegation Models

| Aspect | SWARM | oh-my-opencode | Delta9 Current | Recommendation |
|--------|-------|----------------|----------------|----------------|
| **Hierarchy** | Coordinator → Workers | Sisyphus → Sisyphus-Junior | Commander → Operators | ✅ Already aligned |
| **File Isolation** | Git worktrees + reservations | None (relies on model) | None | Add file assignment |
| **Coordinator Guard** | Runtime blocking | Blocked tools list | Prompt-only | **Add runtime guard** |
| **Task Contract** | Typed `WorkerHandoff` | Category + Skills | Basic task description | Add structured contract |

### Agent Tool Restrictions

| Agent Type | SWARM | oh-my-opencode | Delta9 Current |
|------------|-------|----------------|----------------|
| Coordinator | BLOCKED: edit, write, bash(tests) | Oracle: BLOCKED: edit, write, task, delegate_task | Commander: Prompt says "never writes code" |
| Workers | Full tool access, file-limited | Sisyphus-Junior: BLOCKED: task, delegate_task | Operators: Full access |
| Advisor | N/A | Oracle: Read-only | Council: Full access (should be read-only?) |

### Context Preservation

| Aspect | SWARM | oh-my-opencode | Delta9 Current |
|--------|-------|----------------|----------------|
| Compaction handling | `SWARM_COMPACTION_CONTEXT` with active swarm detection | Structured summarization template (5 sections) | Basic hook exists |
| State persistence | Hive cells (JSONL) | `.sisyphus/` drafts and plans | `.delta9/mission.json` |
| Session resume | Via swarm status tools | `resume` parameter in delegate_task | Not implemented |

### Task Routing

| Aspect | SWARM | oh-my-opencode | Delta9 Current |
|--------|-------|----------------|----------------|
| Routing basis | Decomposition strategy | Category-based | Task router categories |
| Model selection | Coordinator's model | Category → specific model | Config-based |
| Intent classification | Strategy auto-selection | Metis pre-planning | None |

---

## Priority 1: Critical Improvements

### 1.1 Commander Guard (Runtime Enforcement)

**Problem**: Commander only has prompt instructions to not write code. Can be bypassed.

**Solution**: Add runtime tool blocking like SWARM's `coordinator-guard.ts`.

**Implementation**:

```typescript
// src/guards/commander-guard.ts
const BLOCKED_TOOLS = ['edit', 'write']
const BLOCKED_BASH_PATTERNS = [
  /\bnpm\s+(run\s+)?test/i,
  /\bbun\s+test/i,
  /\bvitest\b/i,
]

export function checkCommanderViolation(
  agent: string,
  toolName: string,
  args: Record<string, unknown>
): { blocked: boolean; reason?: string } {
  if (agent !== 'commander') return { blocked: false }

  if (BLOCKED_TOOLS.includes(toolName.toLowerCase())) {
    return {
      blocked: true,
      reason: `Commander cannot use ${toolName}. Delegate to an Operator instead.`
    }
  }

  if (toolName === 'bash' && typeof args.command === 'string') {
    for (const pattern of BLOCKED_BASH_PATTERNS) {
      if (pattern.test(args.command)) {
        return {
          blocked: true,
          reason: `Commander cannot run tests. Use dispatch_task to delegate to Validator.`
        }
      }
    }
  }

  return { blocked: false }
}
```

**Hook Integration**:
```typescript
// In tool.execute.before hook
const violation = checkCommanderViolation(ctx.agent, toolName, args)
if (violation.blocked) {
  return { blocked: true, message: violation.reason }
}
```

**Files to modify**:
- Create: `src/guards/commander-guard.ts`
- Modify: `src/hooks/index.ts` - Add tool.execute.before guard

---

### 1.2 File Assignment in Tasks

**Problem**: No way to detect file conflicts when multiple operators work in parallel.

**Solution**: Add explicit file lists to tasks (like SWARM's `CellTree.subtasks[].files`).

**Implementation**:

```typescript
// src/types/mission.ts
interface Task {
  id: string
  objective: string
  description: string
  status: 'pending' | 'in_progress' | 'blocked' | 'completed' | 'failed'
  acceptanceCriteria: string[]
  routing: 'operator' | 'validator' | 'council'

  // New fields
  files: string[]           // Files this task will modify
  filesReadonly?: string[]  // Files task can read only
}
```

**Conflict Detection**:
```typescript
// src/mission/conflict-detector.ts
export function detectFileConflicts(tasks: Task[]): FileConflict[] {
  const fileAssignments = new Map<string, string[]>()
  const conflicts: FileConflict[] = []

  for (const task of tasks) {
    for (const file of task.files) {
      const existing = fileAssignments.get(file)
      if (existing) {
        conflicts.push({
          file,
          tasks: [...existing, task.id],
          type: 'multiple_writers'
        })
      } else {
        fileAssignments.set(file, [task.id])
      }
    }
  }

  return conflicts
}
```

**Files to modify**:
- Modify: `src/types/mission.ts` - Add files arrays
- Create: `src/mission/conflict-detector.ts`
- Modify: `src/tools/mission/plan.ts` - Add conflict detection

---

### 1.3 Structured Compaction Context

**Problem**: Current compaction hook is basic. May lose critical context.

**Solution**: Use oh-my-opencode's structured summarization template.

**Implementation**:

```typescript
// src/hooks/compaction-context.ts
const DELTA9_COMPACTION_CONTEXT = `
When summarizing this Delta9 session, you MUST include:

## 1. Mission State
- Current mission ID and title
- Active objectives and their status
- Current task being executed

## 2. User Requests (As-Is)
- List all original user requests exactly as stated
- Preserve the user's exact wording and intent

## 3. Work Completed
- What has been done so far
- Files created/modified
- Tasks marked complete

## 4. Remaining Tasks
- Pending tasks from mission.json
- Blocked tasks and what's blocking them
- Follow-up tasks identified during work

## 5. MUST NOT Do (Critical Constraints)
- Things explicitly forbidden by the user
- Approaches that failed and should not be retried
- User's explicit restrictions or preferences
- Guardrails from mission planning

## 6. Operator Context
- Which operators have been used
- Their results and any learnings
- Retry counts for failed tasks

This context is critical for maintaining mission continuity after compaction.
`
```

**Files to modify**:
- Modify: `src/hooks/session.ts` - Enhance compaction handling

---

## Priority 2: Important Enhancements

### 2.1 Worker Handoff Contract

**Problem**: Task delegation passes minimal context. Operators may miss important details.

**Solution**: Add typed `OperatorHandoff` contract (like SWARM's `WorkerHandoff`).

**Implementation**:

```typescript
// src/types/handoff.ts
interface OperatorHandoff {
  contract: {
    taskId: string
    objective: string
    filesOwned: string[]      // Files operator can modify
    filesReadonly: string[]   // Files operator can read only
    dependenciesCompleted: string[]  // Prior tasks that are done
    successCriteria: string[]
    mustNotDo: string[]       // Explicit guardrails
  }
  context: {
    missionSummary: string    // What we're trying to achieve overall
    yourRole: string          // What this specific task accomplishes
    whatOthersDid: string     // Context from completed tasks
    whatComesNext: string     // Tasks that depend on this one
  }
  escalation: {
    blockedContact: string    // Who to notify if blocked
    scopeChangeProtocol: string  // What to do if scope needs to change
  }
}
```

**Contract Generation**:
```typescript
// src/tools/dispatch/handoff.ts
export function generateOperatorHandoff(
  mission: Mission,
  task: Task
): OperatorHandoff {
  const completedTasks = mission.tasks.filter(t => t.status === 'completed')
  const dependentTasks = mission.tasks.filter(t =>
    t.dependencies?.includes(task.id)
  )

  return {
    contract: {
      taskId: task.id,
      objective: task.description,
      filesOwned: task.files,
      filesReadonly: task.filesReadonly ?? [],
      dependenciesCompleted: completedTasks.map(t => t.id),
      successCriteria: task.acceptanceCriteria,
      mustNotDo: mission.guardrails ?? [],
    },
    context: {
      missionSummary: mission.description,
      yourRole: `Complete: ${task.description}`,
      whatOthersDid: completedTasks.map(t =>
        `${t.id}: ${t.description}`
      ).join('\n'),
      whatComesNext: dependentTasks.map(t =>
        `${t.id}: ${t.description} (blocked by this)`
      ).join('\n'),
    },
    escalation: {
      blockedContact: 'Use report_blocked tool',
      scopeChangeProtocol: 'Do NOT expand scope. Report via report_blocked.',
    },
  }
}
```

**Files to create**:
- Create: `src/types/handoff.ts`
- Create: `src/tools/dispatch/handoff.ts`
- Modify: `src/tools/dispatch.ts` - Use handoff in dispatch_task

---

### 2.2 Intent Classification (Pre-Planning)

**Problem**: Commander jumps straight to planning without classifying the request type.

**Solution**: Add Metis-like intent classification phase.

**Intent Types**:

| Intent | Signals | Focus |
|--------|---------|-------|
| **Refactoring** | "refactor", "restructure", "clean up" | Safety: regression prevention |
| **Build** | "create new", "add feature" | Discovery: explore patterns first |
| **Fix** | "fix", "bug", "broken" | Diagnosis: understand root cause |
| **Architecture** | "how should we structure" | Strategic: council consultation |
| **Research** | "investigate", "explore", "find out" | Investigation: exit criteria |

**Implementation**:

```typescript
// src/planning/intent-classifier.ts
export type IntentType =
  | 'refactoring'
  | 'build'
  | 'fix'
  | 'architecture'
  | 'research'
  | 'simple'

const INTENT_KEYWORDS: Record<IntentType, string[]> = {
  refactoring: ['refactor', 'restructure', 'clean up', 'reorganize'],
  build: ['create', 'add', 'implement', 'build', 'new feature'],
  fix: ['fix', 'bug', 'broken', 'error', 'crash', 'issue'],
  architecture: ['architect', 'design', 'structure', 'how should'],
  research: ['investigate', 'explore', 'research', 'find out', 'understand'],
  simple: ['typo', 'rename', 'update', 'change'],
}

export function classifyIntent(request: string): {
  type: IntentType
  confidence: number
  focus: string
} {
  const requestLower = request.toLowerCase()
  let bestMatch: IntentType = 'build'
  let bestScore = 0

  for (const [intent, keywords] of Object.entries(INTENT_KEYWORDS)) {
    const score = keywords.filter(k => requestLower.includes(k)).length
    if (score > bestScore) {
      bestScore = score
      bestMatch = intent as IntentType
    }
  }

  return {
    type: bestMatch,
    confidence: bestScore > 0 ? Math.min(bestScore / 3, 1) : 0.3,
    focus: getIntentFocus(bestMatch),
  }
}

function getIntentFocus(intent: IntentType): string {
  switch (intent) {
    case 'refactoring': return 'SAFETY: Ensure no regressions'
    case 'build': return 'DISCOVERY: Explore existing patterns first'
    case 'fix': return 'DIAGNOSIS: Understand root cause before fixing'
    case 'architecture': return 'STRATEGIC: Consult council before deciding'
    case 'research': return 'INVESTIGATION: Define exit criteria'
    case 'simple': return 'EXECUTION: Direct implementation'
  }
}
```

**Files to create**:
- Create: `src/planning/intent-classifier.ts`
- Modify: Commander prompt to use intent classification

---

### 2.3 Output Guardrails

**Problem**: Large tool outputs can consume entire context.

**Solution**: Add smart truncation (like SWARM's `output-guardrails.ts`).

**Implementation**:

```typescript
// src/lib/output-guardrails.ts
const DEFAULT_LIMIT = 32000  // ~8,000 tokens
const CODE_TOOL_LIMIT = 64000  // Higher for code tools

const SKIP_TRUNCATION_TOOLS = [
  'mission_status',
  'council_vote',
  'validate_task',
]

export function guardrailOutput(
  toolName: string,
  output: string
): string {
  if (SKIP_TRUNCATION_TOOLS.includes(toolName)) {
    return output
  }

  const limit = toolName.match(/read|grep|glob/i)
    ? CODE_TOOL_LIMIT
    : DEFAULT_LIMIT

  if (output.length <= limit) {
    return output
  }

  return truncateWithBoundaries(output, limit)
}

function truncateWithBoundaries(text: string, maxChars: number): string {
  // Find safe truncation point
  let truncateAt = maxChars

  // Don't break JSON
  const openBraces = (text.slice(0, truncateAt).match(/{/g) || []).length
  const closeBraces = (text.slice(0, truncateAt).match(/}/g) || []).length
  if (openBraces > closeBraces) {
    // Find matching brace
    let depth = openBraces - closeBraces
    for (let i = truncateAt; i < text.length && depth > 0; i++) {
      if (text[i] === '}') depth--
      if (text[i] === '{') depth++
      if (depth === 0) {
        truncateAt = i + 1
        break
      }
    }
  }

  // Don't break code blocks
  const codeBlockStart = text.slice(0, truncateAt).lastIndexOf('```')
  if (codeBlockStart !== -1) {
    const codeBlockEnd = text.indexOf('```', codeBlockStart + 3)
    if (codeBlockEnd > truncateAt) {
      truncateAt = codeBlockEnd + 3
    }
  }

  // Find paragraph boundary
  const paragraphEnd = text.slice(0, truncateAt).lastIndexOf('\n\n')
  if (paragraphEnd > maxChars * 0.8) {
    truncateAt = paragraphEnd
  }

  return text.slice(0, truncateAt) +
    `\n\n[OUTPUT TRUNCATED - ${text.length - truncateAt} chars remaining]`
}
```

**Files to create**:
- Create: `src/lib/output-guardrails.ts`
- Modify: `src/hooks/index.ts` - Add tool.execute.after guardrail

---

## Priority 3: Nice-to-Have

### 3.1 Session Resume

**Problem**: No way to resume a delegated task session.

**Solution**: Add resume parameter to dispatch_task.

```typescript
// In dispatch_task
interface DispatchTaskArgs {
  task?: string
  operator?: string
  context?: string
  resume?: string  // Session ID to resume
}
```

---

### 3.2 Council as Read-Only

**Problem**: Council oracles have full tool access. May accidentally modify files.

**Solution**: Block write tools for council agents (like Oracle).

```typescript
// src/agents/council/index.ts
const COUNCIL_TOOL_RESTRICTIONS = {
  write: 'never',
  edit: 'never',
  bash: 'ask',  // Some commands OK, but not tests
}
```

---

### 3.3 Category-Based Model Selection

**Problem**: All operators use same model regardless of task type.

**Solution**: Route tasks to optimal models based on category.

```typescript
// src/routing/model-selection.ts
const CATEGORY_MODELS: Record<string, string> = {
  'frontend': 'gemini-pro',      // Good at UI
  'backend': 'claude-sonnet',    // Good at logic
  'quick': 'claude-haiku',       // Fast and cheap
  'architecture': 'claude-opus', // Deep reasoning
}

export function selectModelForTask(task: Task): string {
  const category = task.routing || 'backend'
  return CATEGORY_MODELS[category] || 'claude-sonnet'
}
```

---

### 3.4 Edit Error Recovery

**Problem**: Edit failures don't guide the agent to recover.

**Solution**: Add edit error detection hook (like oh-my-opencode).

```typescript
// src/hooks/edit-recovery.ts
const EDIT_ERROR_PATTERNS = [
  'oldString and newString must be different',
  'oldString not found',
  'oldString found multiple times',
]

const EDIT_ERROR_REMINDER = `
[EDIT ERROR - IMMEDIATE ACTION REQUIRED]

1. READ the file to see its ACTUAL current state
2. VERIFY what the content really looks like
3. RETRY with corrected old_string based on real content

DO NOT attempt another edit until you've read the file.
`
```

---

## Implementation Roadmap

### Phase 1: Critical (Do First)
1. **Commander Guard** - Runtime tool blocking
2. **File Assignment** - Conflict detection
3. **Compaction Context** - Structured summarization

### Phase 2: Important (Next Sprint)
4. **Worker Handoff** - Typed contract
5. **Intent Classification** - Pre-planning phase
6. **Output Guardrails** - Smart truncation

### Phase 3: Enhancement (Future)
7. Session Resume
8. Council Read-Only
9. Category-Based Models
10. Edit Error Recovery

---

## Summary

| Pattern | SWARM | oh-my-opencode | Delta9 Action |
|---------|-------|----------------|---------------|
| Coordinator Guard | ✅ Runtime blocking | ✅ Blocked tools list | **ADD** |
| File Assignment | ✅ CellTree.files | ❌ None | **ADD** |
| Compaction Context | ✅ Active swarm detection | ✅ 5-section template | **ENHANCE** |
| Worker Contract | ✅ WorkerHandoff | ✅ Category + Skills | **ADD** |
| Intent Classification | ✅ Strategy selection | ✅ Metis pre-planning | **ADD** |
| Output Guardrails | ✅ Smart truncation | ❌ None | **ADD** |
| Session Resume | ✅ Via swarm_status | ✅ resume parameter | Consider |
| Category Routing | ❌ Single coordinator | ✅ 7 categories | Consider |

**Most impactful additions**:
1. Commander Guard (prevents role violation)
2. File Assignment (enables parallel work)
3. Structured Compaction (preserves context)
