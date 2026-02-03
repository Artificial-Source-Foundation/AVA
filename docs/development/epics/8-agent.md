# Epic 8: Single Agent

> Autonomous agent loop

---

## Goal

Build an autonomous agent that can plan, execute, and iterate on tasks without constant user intervention.

---

## Prerequisites

- Epic 4 (Safety) - Permissions for autonomous operations
- Epic 5 (Context) - Long conversations without context overflow
- Epic 6 (DX) - Git snapshots for safe rollback

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 8.1 | Agent Loop | Plan → Execute → Evaluate cycle | ~300 |
| 8.2 | Task Planning | Break down goals into steps | ~250 |
| 8.3 | Self-Correction | Detect and recover from errors | ~200 |
| 8.4 | Progress Tracking | Status updates, completion detection | ~150 |

**Total:** ~900 lines

---

## Architecture

```
User Request
     │
     ▼
┌─────────────┐
│   Planner   │ ◄── Breaks down into steps
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Executor   │ ◄── Runs tools for each step
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Evaluator  │ ◄── Checks if step succeeded
└──────┬──────┘
       │
       ▼
   Complete? ──No──► Back to Planner
       │
      Yes
       │
       ▼
   Response
```

---

## Key Features

### Agent Loop
```typescript
interface AgentStep {
  id: string
  description: string
  tools: string[]
  status: 'pending' | 'running' | 'success' | 'failed'
  output?: string
  error?: string
}

async function runAgent(goal: string, ctx: AgentContext): Promise<AgentResult> {
  // 1. Plan
  const steps = await planSteps(goal, ctx)

  // 2. Execute each step
  for (const step of steps) {
    step.status = 'running'
    notifyProgress(ctx, step)

    try {
      const result = await executeStep(step, ctx)
      step.status = 'success'
      step.output = result
    } catch (err) {
      step.status = 'failed'
      step.error = err.message

      // 3. Self-correct
      const recovery = await planRecovery(step, err, ctx)
      if (recovery) {
        steps.splice(steps.indexOf(step) + 1, 0, ...recovery)
      } else {
        break  // Unrecoverable
      }
    }
  }

  // 4. Evaluate completion
  return evaluateResult(goal, steps, ctx)
}
```

### Permission Integration
```typescript
// Before destructive operations
const permission = await requestPermission({
  tool: 'delete_file',
  paths: ['/src/old-code.ts'],
  reason: 'Removing deprecated module as part of refactoring',
  risk: 'medium',
})

if (!permission.allowed) {
  return { success: false, output: 'User denied permission' }
}
```

---

## Dependencies

- Epic 4.1 (Permission System)
- Epic 5.2 (Context Compaction)
- Epic 6.3 (Git Snapshots)

---

## Acceptance Criteria

- [ ] Agent can complete multi-step tasks autonomously
- [ ] Progress updates stream to UI
- [ ] Errors trigger recovery attempts
- [ ] User can cancel at any step
- [ ] All destructive operations require permission
