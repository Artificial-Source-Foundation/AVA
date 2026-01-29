# Hook System

## Available Hook Types

| Hook | When Fired | Purpose |
|------|-----------|---------|
| `event` | Various lifecycle events | Session created/deleted/idle, errors |
| `tool.execute.before` | Before tool runs | Intercept, modify args, block execution |
| `tool.execute.after` | After tool runs | Read results, trigger side effects |
| `chat.message` | Message received | Observe, modify message parts |
| `experimental.chat.messages.transform` | Before API call | Transform message history |
| `experimental.chat.system.transform` | Before API call | Inject system prompt content |
| `command.execute.before` | Before slash command | Intercept commands |

---

## Event Hook Pattern

```typescript
event: async ({ event }) => {
  switch (event.type) {
    case "session.created":
      const { sessionID } = event.properties;
      // Initialize session state
      break;

    case "session.idle":
      const { sessionID } = event.properties;
      // Session finished processing - good time for cleanup
      break;

    case "session.deleted":
      const { id } = event.properties.info;
      // Clean up resources
      break;

    case "session.error":
      const { sessionID, error } = event.properties;
      // Handle errors
      break;
  }
}
```

---

## Tool Interception Pattern

```typescript
// Block dangerous operations
"tool.execute.before": async (input, output) => {
  if (input.tool === "Bash") {
    const command = output.args?.command as string;

    if (isDangerous(command)) {
      throw new Error(
        "This command is blocked for safety. " +
        "Please use alternative approach."
      );
    }
  }

  // Modify tool arguments
  if (input.tool === "task") {
    output.args.tools = {
      ...output.args.tools,
      delegate_task: false, // Prevent recursive delegation
    };
  }
},

// Observe tool results
"tool.execute.after": async (input, output) => {
  if (input.tool === "Read") {
    // Track file reads for context
    trackFileAccess(input.args.filePath, output.result);
  }
}
```

---

## Message Transform Pattern

```typescript
// Inject content into system prompt
"experimental.chat.system.transform": async (_input, output) => {
  const memoryContext = await loadMemoryBlocks();

  // Insert early for caching efficiency
  output.system.splice(1, 0, `
<memory_blocks>
${memoryContext}
</memory_blocks>
  `);
},

// Transform message history before API call
"experimental.chat.messages.transform": async (input, output) => {
  // Find last user message
  const lastUser = output.messages.findLast(m => m.info.role === "user");
  if (!lastUser) return;

  // Inject synthetic part (invisible in UI)
  lastUser.parts.unshift({
    type: "text",
    id: `synthetic-${Date.now()}`,
    sessionID: lastUser.info.sessionID,
    messageID: lastUser.info.id,
    text: "[SYSTEM: Additional context here]",
    synthetic: true, // Invisible to user
  });
}
```

---

## Hook Composition (Multiple Handlers)

```typescript
// Create modular hooks
function createHookA(ctx): HookHandlers { /* ... */ }
function createHookB(ctx): HookHandlers { /* ... */ }

// Compose in plugin
const hookA = createHookA(ctx);
const hookB = createHookB(ctx);

return {
  "tool.execute.before": async (input, output) => {
    await hookA["tool.execute.before"]?.(input, output);
    await hookB["tool.execute.before"]?.(input, output);
  },
  // ...
};
```

---

## Event Types Reference

| Event | Properties | When |
|-------|------------|------|
| `session.created` | `sessionID` | New session started |
| `session.idle` | `sessionID` | Session finished processing |
| `session.deleted` | `info: { id }` | Session removed |
| `session.error` | `sessionID, error` | Error occurred |
| `permission.updated` | - | Permissions changed |

---

## Source Reference

- `oh-my-opencode/src/hooks/` - Full hook implementations
- `safety-net/src/` - Tool interception
- `agent-memory/src/` - System transform
