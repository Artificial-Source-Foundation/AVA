# OpenCode Hooks

> Hooks allow plugins to respond to events in the OpenCode lifecycle.

---

## Available Hooks

### Session Hooks

| Hook | Trigger |
|------|---------|
| `session.created` | New session started |
| `session.updated` | Session state changed |
| `session.deleted` | Session removed |
| `session.idle` | Agent finished, waiting for input |
| `session.status` | Session status changed |
| `session.compacted` | Context was compacted |

### Tool Hooks

| Hook | Trigger |
|------|---------|
| `tool.execute.before` | Before any tool executes |
| `tool.execute.after` | After any tool executes |

### Message Hooks

| Hook | Trigger |
|------|---------|
| `message.created` | New message added |
| `message.updated` | Message content changed |
| `message.deleted` | Message removed |

### File Hooks

| Hook | Trigger |
|------|---------|
| `file.edited` | File was modified |
| `file.watcher.updated` | File watcher detected change |

### TUI Hooks

| Hook | Trigger |
|------|---------|
| `tui.prompt.append` | Text appended to prompt |
| `tui.command.execute` | Command executed |
| `tui.toast.show` | Toast notification shown |

### Other Hooks

| Hook | Trigger |
|------|---------|
| `command.executed` | Slash command executed |
| `permission.replied` | User responded to permission |
| `permission.updated` | Permission state changed |
| `server.connected` | Server connection established |
| `lsp.client.diagnostics` | LSP diagnostics received |
| `lsp.updated` | LSP state changed |
| `installation.updated` | Installation state changed |

---

## Hook Handler Signature

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    "session.created": async ({ event }) => {
      // event contains hook-specific data
      console.log("Session created:", event.id)
    },

    "tool.execute.before": async ({ event }) => {
      console.log("Tool executing:", event.tool)
    },

    "tool.execute.after": async ({ event }) => {
      console.log("Tool finished:", event.tool, event.result)
    },
  }
}
```

---

## Key Hook Patterns

### Session Lifecycle

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    "session.created": async ({ event }) => {
      // Load mission state on session start
      await loadMissionState()
    },

    "session.idle": async ({ event }) => {
      // Check for pending tasks when agent is idle
      const pendingTasks = await getPendingTasks()
      if (pendingTasks.length > 0) {
        // Could auto-continue or notify user
      }
    },

    "session.compacted": async ({ event }) => {
      // Context was compressed - mission.json persists
      await ctx.client.app.log("info", "Context compacted, mission state preserved")
    },
  }
}
```

### Tool Interception

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    "tool.execute.before": async ({ event }) => {
      // Log all tool usage
      await ctx.client.app.log("debug", `Tool: ${event.tool}`)

      // Could modify args or abort
      if (event.tool === "bash" && event.args?.command?.includes("rm -rf")) {
        throw new Error("Dangerous command blocked")
      }
    },

    "tool.execute.after": async ({ event }) => {
      // Track file changes for mission state
      if (event.tool === "write" || event.tool === "edit") {
        await recordFileChange(event.args.path)
      }
    },
  }
}
```

### Message Handling

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    "message.created": async ({ event }) => {
      // Analyze incoming messages
      if (event.role === "user") {
        const complexity = analyzeComplexity(event.content)
        // Could trigger different council modes
      }
    },
  }
}
```

---

## Context Compaction Handling

When context is compacted, in-memory state is lost but `mission.json` persists:

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  let missionState = null

  return {
    "session.created": async ({ event }) => {
      // Always reload from disk on session start
      missionState = await loadFromDisk()
    },

    "session.compacted": async ({ event }) => {
      // State survives via mission.json
      missionState = await loadFromDisk()
      await ctx.client.app.log("info", "Reloaded mission state after compaction")
    },
  }
}
```

---

## Best Practices

1. **Keep handlers fast** - Don't block with long operations
2. **Handle errors gracefully** - Don't crash the plugin
3. **Use `client.app.log()`** - For debugging and audit trails
4. **Persist important state** - Don't rely on in-memory state
5. **Be idempotent** - Hooks may fire multiple times

---

## Reference

- [Official Plugin Docs](https://opencode.ai/docs/plugins/)
- [oh-my-opencode hooks](https://github.com/code-yeongyu/oh-my-opencode) - 25+ hook implementations
