# Epic 10: Parallel Execution

> Concurrent operators

---

## Goal

Enable parallel execution of independent tasks for faster completion of complex work.

---

## Prerequisites

- Epic 8 (Agent) - Single agent loop
- Epic 9 (Commander) - Task delegation

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 10.1 | Dependency Graph | Build task dependency DAG | ~250 |
| 10.2 | Parallel Scheduler | Execute independent tasks concurrently | ~300 |
| 10.3 | Resource Management | Limit concurrent operations | ~200 |
| 10.4 | Result Merging | Handle parallel edit conflicts | ~250 |

**Total:** ~1000 lines

---

## Architecture

```
Task Graph:
    ┌─────┐
    │  A  │ Read codebase
    └──┬──┘
       │
   ┌───┴───┐
   ▼       ▼
┌─────┐ ┌─────┐
│  B  │ │  C  │  Can run in parallel
└──┬──┘ └──┬──┘
   │       │
   └───┬───┘
       ▼
    ┌─────┐
    │  D  │ Depends on B and C
    └─────┘
```

---

## Key Features

### Task DAG
```typescript
interface TaskNode {
  id: string
  task: string
  dependencies: string[]
  status: 'pending' | 'running' | 'complete' | 'failed'
  result?: unknown
}

class TaskGraph {
  private nodes = new Map<string, TaskNode>()

  add(node: TaskNode): void {
    this.nodes.set(node.id, node)
  }

  getReady(): TaskNode[] {
    // Return nodes with all dependencies complete
    return [...this.nodes.values()].filter(node =>
      node.status === 'pending' &&
      node.dependencies.every(dep =>
        this.nodes.get(dep)?.status === 'complete'
      )
    )
  }
}
```

### Parallel Scheduler
```typescript
async function executeParallel(
  graph: TaskGraph,
  maxConcurrency = 3
): Promise<Map<string, unknown>> {
  const results = new Map<string, unknown>()
  const running = new Set<Promise<void>>()

  while (true) {
    const ready = graph.getReady()
    if (ready.length === 0 && running.size === 0) break

    // Start tasks up to concurrency limit
    for (const node of ready) {
      if (running.size >= maxConcurrency) break

      node.status = 'running'
      const promise = executeNode(node).then(result => {
        node.status = 'complete'
        node.result = result
        results.set(node.id, result)
        running.delete(promise)
      }).catch(err => {
        node.status = 'failed'
        running.delete(promise)
      })

      running.add(promise)
    }

    // Wait for at least one to complete
    if (running.size > 0) {
      await Promise.race(running)
    }
  }

  return results
}
```

---

## Acceptance Criteria

- [ ] Independent tasks execute in parallel
- [ ] Dependencies respected in execution order
- [ ] Concurrency limit prevents resource exhaustion
- [ ] Parallel file edits handled without corruption
- [ ] Progress updates for all parallel tasks
