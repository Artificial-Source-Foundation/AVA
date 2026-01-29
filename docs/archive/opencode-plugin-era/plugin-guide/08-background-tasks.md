# Background Tasks

## Background Task Manager

Pattern from `oh-my-opencode`:

```typescript
interface BackgroundTask {
  id: string;
  sessionID?: string;
  parentSessionID: string;
  prompt: string;
  agent: string;
  status: "pending" | "running" | "completed" | "error";
  queuedAt: Date;
  startedAt?: Date;
  completedAt?: Date;
  result?: string;
  error?: string;
}

class BackgroundManager {
  private tasks = new Map<string, BackgroundTask>();
  private concurrency = 3;
  private running = 0;
  private queue: BackgroundTask[] = [];

  constructor(private client: OpencodeClient) {}

  async launch(input: { prompt: string; agent: string; parentSessionID: string }): Promise<BackgroundTask> {
    const task: BackgroundTask = {
      id: `bg_${crypto.randomUUID().slice(0, 8)}`,
      parentSessionID: input.parentSessionID,
      prompt: input.prompt,
      agent: input.agent,
      status: "pending",
      queuedAt: new Date(),
    };

    this.tasks.set(task.id, task);
    this.queue.push(task);

    // Fire-and-forget processing
    this.processQueue();

    return task;
  }

  private async processQueue(): Promise<void> {
    if (this.running >= this.concurrency || this.queue.length === 0) {
      return;
    }

    const task = this.queue.shift()!;
    this.running++;
    task.status = "running";
    task.startedAt = new Date();

    try {
      // Create new session for background work
      const session = await this.client.session.create({
        body: { title: `Background: ${task.id}`, parentID: task.parentSessionID },
      });

      task.sessionID = session.data.id;

      // Run prompt
      const result = await this.client.session.prompt({
        path: { id: session.data.id },
        body: {
          agent: task.agent,
          parts: [{ type: "text", text: task.prompt }],
        },
      });

      task.status = "completed";
      task.completedAt = new Date();
      task.result = extractResult(result.data);
    } catch (error) {
      task.status = "error";
      task.error = error instanceof Error ? error.message : "Unknown error";
    } finally {
      this.running--;
      this.processQueue(); // Continue with next
    }
  }

  getTask(id: string): BackgroundTask | undefined {
    return this.tasks.get(id);
  }

  listTasks(parentSessionID?: string): BackgroundTask[] {
    const tasks = [...this.tasks.values()];
    if (parentSessionID) {
      return tasks.filter(t => t.parentSessionID === parentSessionID);
    }
    return tasks;
  }
}
```

---

## Fire-and-Forget Pattern

```typescript
// In event handler - non-blocking async execution
event: async ({ event }) => {
  if (event.type === "session.idle") {
    // Fire and forget - don't block the event handler
    handleSessionIdle(event.properties.sessionID).catch(error => {
      console.error("Background handler failed:", error);
    });
  }
}

async function handleSessionIdle(sessionID: string): Promise<void> {
  // Long-running work here
  await someAsyncOperation();
}
```

---

## Concurrency Control

```typescript
class ConcurrencyManager {
  private running = 0;

  constructor(private maxConcurrent: number = 3) {}

  async acquire(): Promise<void> {
    while (this.running >= this.maxConcurrent) {
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    this.running++;
  }

  release(): void {
    this.running--;
  }

  async runExclusive<T>(fn: () => Promise<T>): Promise<T> {
    await this.acquire();
    try {
      return await fn();
    } finally {
      this.release();
    }
  }
}
```

---

## Task Status Transitions

```
pending → running → completed
                  → error
```

| Status | Meaning |
|--------|---------|
| `pending` | Queued, waiting for slot |
| `running` | Currently executing |
| `completed` | Finished successfully |
| `error` | Failed with error |

---

## Tools for Background Tasks

```typescript
// Launch tool
const launchTask = tool({
  description: "Launch a background task",
  args: {
    prompt: tool.schema.string(),
    agent: tool.schema.string().optional(),
  },
  async execute(args, ctx) {
    const task = await manager.launch({
      prompt: args.prompt,
      agent: args.agent ?? "default",
      parentSessionID: ctx.sessionID,
    });
    return JSON.stringify({ taskId: task.id, status: task.status });
  },
});

// Read tool
const readTask = tool({
  description: "Read background task result",
  args: {
    taskId: tool.schema.string(),
  },
  async execute(args) {
    const task = manager.getTask(args.taskId);
    if (!task) return JSON.stringify({ error: "Task not found" });
    return JSON.stringify(task);
  },
});

// List tool
const listTasks = tool({
  description: "List background tasks",
  args: {},
  async execute(args, ctx) {
    const tasks = manager.listTasks(ctx.sessionID);
    return JSON.stringify(tasks.map(t => ({
      id: t.id,
      status: t.status,
      agent: t.agent,
    })));
  },
});
```

---

## Source Reference

- `oh-my-opencode/src/features/background-agent/` - Full implementation
- `background-agents/src/plugin/` - Simpler delegation pattern
- `pocket-universe/src/` - Subagent coordination
