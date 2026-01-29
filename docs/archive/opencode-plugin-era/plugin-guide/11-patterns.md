# Common Patterns

## Idempotency Guards

```typescript
const processedSessions = new Set<string>();

"chat.message": async (input, output) => {
  const sessionID = output.message.sessionID;

  // Skip if already processed
  if (processedSessions.has(sessionID)) return;

  // Check for specific condition
  if (!shouldProcess(output)) return;

  // Mark as processed
  processedSessions.add(sessionID);

  // Do work
  await processSession(sessionID);
}

// Cleanup on session delete
event: async ({ event }) => {
  if (event.type === "session.deleted") {
    processedSessions.delete(event.properties.info.id);
  }
}
```

---

## Threshold-Based Batching

```typescript
const eventCounts = new Map<string, number>();
const THRESHOLD = 5;

event: async ({ event }) => {
  if (event.type !== "session.idle") return;

  const sessionID = event.properties.sessionID;
  const count = (eventCounts.get(sessionID) ?? 0) + 1;
  eventCounts.set(sessionID, count);

  // Only act every Nth event
  if (count % THRESHOLD !== 0) return;

  await performBatchedAction(sessionID);
}
```

---

## Lazy Loading

```typescript
let expensiveModule: typeof import("expensive-module") | null = null;

async function getExpensiveModule() {
  if (!expensiveModule) {
    expensiveModule = await import("expensive-module");
  }
  return expensiveModule;
}

// Only load when actually needed
async function doExpensiveWork() {
  const mod = await getExpensiveModule();
  return mod.process();
}
```

---

## Parent Session Filtering

```typescript
async function isParentSession(client: OpencodeClient, sessionID: string): Promise<boolean> {
  try {
    const session = await client.session.get({ path: { id: sessionID } });
    return !session.data?.parentID; // No parent = root session
  } catch {
    return true; // Assume parent on error
  }
}

event: async ({ event }) => {
  if (event.type !== "session.idle") return;

  const sessionID = event.properties.sessionID;

  // Skip child sessions (subagents, etc.)
  if (!await isParentSession(client, sessionID)) {
    return;
  }

  await handleParentSessionIdle(sessionID);
}
```

---

## Graceful Degradation

```typescript
const FALLBACK_MODELS = ["claude-haiku", "gpt-4o-mini", "gemini-flash"];

async function getModel(): Promise<Model | null> {
  for (const modelId of FALLBACK_MODELS) {
    try {
      const model = await getLanguageModel(modelId);
      return model;
    } catch {
      continue; // Try next
    }
  }
  return null; // All failed
}

// Usage with fallback behavior
async function generateTitle(content: string): Promise<string> {
  const model = await getModel();

  if (!model) {
    // Fallback to heuristic
    return extractFirstLine(content).slice(0, 50);
  }

  return await model.generate(content);
}
```

---

## Debouncing

```typescript
const debounceTimers = new Map<string, Timer>();

function debounce(key: string, fn: () => void, delay: number): void {
  const existing = debounceTimers.get(key);
  if (existing) {
    clearTimeout(existing);
  }

  const timer = setTimeout(() => {
    debounceTimers.delete(key);
    fn();
  }, delay);

  debounceTimers.set(key, timer);
}

// Usage
event: async ({ event }) => {
  if (event.type === "session.idle") {
    debounce(
      `idle-${event.properties.sessionID}`,
      () => handleIdle(event.properties.sessionID),
      1000 // Wait 1s for more events
    );
  }
}
```

---

## Retry with Backoff

```typescript
async function withRetry<T>(
  fn: () => Promise<T>,
  options: {
    maxAttempts?: number;
    baseDelay?: number;
    maxDelay?: number;
  } = {}
): Promise<T> {
  const { maxAttempts = 3, baseDelay = 1000, maxDelay = 10000 } = options;

  let lastError: Error | undefined;

  for (let attempt = 0; attempt < maxAttempts; attempt++) {
    try {
      return await fn();
    } catch (error) {
      lastError = error instanceof Error ? error : new Error(String(error));

      if (attempt < maxAttempts - 1) {
        const delay = Math.min(baseDelay * Math.pow(2, attempt), maxDelay);
        await new Promise(resolve => setTimeout(resolve, delay));
      }
    }
  }

  throw lastError;
}

// Usage
const result = await withRetry(
  () => fetchData(),
  { maxAttempts: 3, baseDelay: 500 }
);
```

---

## Synthetic Message Injection

```typescript
// Inject content that persists across compaction
async function injectContent(
  client: OpencodeClient,
  sessionID: string,
  content: string
): Promise<void> {
  await client.session.prompt({
    path: { id: sessionID },
    body: {
      noReply: true, // Don't trigger AI response
      parts: [{
        type: "text",
        text: content,
        synthetic: true, // Mark as synthetic
      }],
    },
  });
}
```

---

## Source Reference

- `handoff/src/plugin.ts` - Idempotency guards
- `oh-my-opencode/src/hooks/` - Batching patterns
- `agent-skills/src/` - Synthetic injection
