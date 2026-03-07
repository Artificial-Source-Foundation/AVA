# Epic 9: Commander

> Hierarchical delegation

---

## Goal

Build a commander agent that can delegate subtasks to specialized worker agents, enabling complex multi-domain tasks.

---

## Prerequisites

- Epic 7 (Platform) - Sub-agent spawning, MCP integration
- Epic 8 (Agent) - Single agent loop

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 9.1 | Worker Registry | Define worker types and capabilities | ~200 |
| 9.2 | Task Delegation | Route subtasks to appropriate workers | ~300 |
| 9.3 | Result Aggregation | Combine worker outputs | ~200 |
| 9.4 | Coordination | Handle dependencies between workers | ~250 |

**Total:** ~950 lines

---

## Architecture

```
User: "Refactor the auth system and add tests"
                    │
                    ▼
            ┌───────────────┐
            │   Commander   │
            └───────┬───────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
   ┌─────────┐ ┌─────────┐ ┌─────────┐
   │ Coder   │ │ Tester  │ │ Reviewer│
   │ Worker  │ │ Worker  │ │ Worker  │
   └────┬────┘ └────┬────┘ └────┬────┘
        │           │           │
        ▼           ▼           ▼
   Code changes   Tests      Review
        │           │           │
        └───────────┴───────────┘
                    │
                    ▼
            Combined Result
```

---

## Worker Types

| Worker | Specialization | Tools |
|--------|----------------|-------|
| Coder | Write/modify code | read, write, create, delete |
| Tester | Write/run tests | read, write, bash |
| Reviewer | Code review | read, grep |
| Researcher | Find information | grep, glob, web_search |
| Debugger | Fix errors | read, bash, grep |

---

## Key Features

### Worker Definition
```typescript
interface WorkerConfig {
  name: string
  role: string
  systemPrompt: string
  tools: string[]  // Allowed tools
  maxTokens: number
}

const WORKERS: Record<string, WorkerConfig> = {
  coder: {
    name: 'Coder',
    role: 'Write and modify code',
    systemPrompt: 'You are a skilled programmer...',
    tools: ['read_file', 'write_file', 'create_file', 'delete_file', 'grep', 'glob'],
    maxTokens: 8000,
  },
  tester: {
    name: 'Tester',
    role: 'Write and run tests',
    systemPrompt: 'You are a QA engineer...',
    tools: ['read_file', 'write_file', 'create_file', 'bash', 'grep'],
    maxTokens: 8000,
  },
  // ...
}
```

### Task Delegation
```typescript
async function delegateTask(
  task: string,
  workerType: string,
  ctx: CommanderContext
): Promise<WorkerResult> {
  const config = WORKERS[workerType]

  // Spawn sub-agent with limited tools
  const worker = await spawnSubagent({
    ...config,
    tools: config.tools,
    parentContext: ctx,
  })

  // Run task
  const result = await worker.run(task)

  // Cleanup
  await worker.terminate()

  return result
}
```

---

## Acceptance Criteria

- [ ] Commander can route tasks to appropriate workers
- [ ] Workers have isolated tool access
- [ ] Results from multiple workers combine correctly
- [ ] Worker failures don't crash commander
- [ ] Resource limits prevent runaway workers
