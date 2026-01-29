# OpenCode SDK

> Programmatic control of OpenCode via TypeScript/JavaScript API.

---

## Installation

```bash
npm install @opencode-ai/sdk
```

---

## Client Creation

### Full Client (Starts Server)

```typescript
import { createOpencode } from "@opencode-ai/sdk"

const { client, server } = await createOpencode({
  hostname: "127.0.0.1",  // Default
  port: 4096,             // Default
  timeout: 30000,         // Connection timeout
  config: {               // Inline config overrides
    model: "anthropic/claude-sonnet-4-5",
  },
})
```

### Client-Only (Connect to Existing Server)

```typescript
import { createOpencodeClient } from "@opencode-ai/sdk"

const client = createOpencodeClient({
  baseUrl: "http://localhost:4096",
})
```

---

## API Categories

### Global

```typescript
// Health check
const health = await client.health()
// { version: "1.0.150", status: "ok" }
```

### App

```typescript
// Logging
await client.app.log("info", "Message")
await client.app.log("debug", "Debug info")
await client.app.log("warn", "Warning")
await client.app.log("error", "Error occurred")

// List agents
const agents = await client.app.agents()
```

### Project

```typescript
// List projects
const projects = await client.project.list()

// Get current project
const current = await client.project.current()
```

### Sessions

```typescript
// Create session
const session = await client.session.create({
  body: { projectID: project.id }
})

// List sessions
const sessions = await client.session.list({
  query: { projectID: project.id }
})

// Get session
const session = await client.session.get({
  path: { id: sessionId }
})

// Delete session
await client.session.delete({
  path: { id: sessionId }
})

// Send prompt
await client.session.prompt({
  path: { id: session.id },
  body: {
    model: { id: "anthropic/claude-sonnet-4-5" },
    parts: [{ type: "text", text: "Hello!" }]
  }
})

// Send prompt without AI response (context injection)
await client.session.prompt({
  path: { id: session.id },
  body: {
    parts: [{ type: "text", text: "Context info..." }],
    noReply: true
  }
})

// Execute command
await client.session.command({
  path: { id: session.id },
  body: { command: "/help" }
})

// Abort current operation
await client.session.abort({
  path: { id: session.id }
})

// Share session
await client.session.share({
  path: { id: session.id }
})

// Unshare session
await client.session.unshare({
  path: { id: session.id }
})
```

### Files

```typescript
// Search text in files
const results = await client.find.text({
  query: { pattern: "function.*test" }
})

// Find files by pattern
const files = await client.find.files({
  query: { pattern: "**/*.ts" }
})

// Find symbols
const symbols = await client.find.symbols({
  query: { query: "MyClass" }
})

// Read file
const content = await client.file.read({
  path: { path: "src/index.ts" }
})

// Check file status
const status = await client.file.status({
  path: { path: "src/index.ts" }
})
```

### TUI Control

```typescript
// Append to prompt
await client.tui.appendPrompt({
  body: { text: "Additional context" }
})

// Submit prompt
await client.tui.submitPrompt()

// Show toast notification
await client.tui.showToast({
  body: {
    message: "Operation complete",
    type: "success"
  }
})
```

### Events

```typescript
// Subscribe to events
const unsubscribe = await client.event.subscribe((event) => {
  console.log("Event:", event.type, event.data)
})

// Later: unsubscribe
unsubscribe()
```

### Auth

```typescript
// Set credentials
await client.auth.set({
  body: {
    provider: "anthropic",
    credentials: { apiKey: "sk-..." }
  }
})
```

---

## Event Types

| Event | Description |
|-------|-------------|
| `session.created` | New session started |
| `session.updated` | Session state changed |
| `session.deleted` | Session removed |
| `message.created` | New message added |
| `message.updated` | Message content changed |
| `tool.execute.before` | Before tool execution |
| `tool.execute.after` | After tool execution |

---

## Usage in Plugins

```typescript
import type { Plugin } from "@opencode-ai/plugin"

export const MyPlugin: Plugin = async (ctx) => {
  const { client } = ctx

  // Log from plugin
  await client.app.log("info", "Plugin loaded")

  return {
    "session.idle": async ({ event }) => {
      // Use SDK within hooks
      const session = await client.session.get({
        path: { id: event.sessionId }
      })

      // Inject context
      await client.session.prompt({
        path: { id: event.sessionId },
        body: {
          parts: [{ type: "text", text: "Check mission status..." }],
          noReply: true
        }
      })
    }
  }
}
```

---

## TypeScript Types

All API methods return strongly-typed responses:

```typescript
import type {
  Session,
  Message,
  Project,
  Agent,
  HealthResponse,
} from "@opencode-ai/sdk"
```

---

## Reference

- [Official SDK Docs](https://opencode.ai/docs/sdk/)
