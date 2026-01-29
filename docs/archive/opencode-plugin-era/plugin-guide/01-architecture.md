# Plugin Architecture

## Plugin Entry Point Pattern

Every plugin exports an async function returning a plugin interface:

```typescript
import type { Plugin, PluginInput } from "@opencode-ai/plugin";

export const MyPlugin: Plugin = async (ctx: PluginInput) => {
  // ctx.client: OpenCode API client
  // ctx.directory: Project working directory
  // ctx.$: Bun shell helper

  // Initialize at startup
  const config = await loadConfig(ctx.directory);
  const manager = new SomeManager(ctx);

  // Return plugin interface
  return {
    // Tools available to the AI
    tool: {
      my_tool: myToolDefinition,
      another_tool: anotherTool,
    },

    // Event handlers (session lifecycle, etc.)
    event: async ({ event }) => {
      if (event.type === "session.created") { /* ... */ }
      if (event.type === "session.idle") { /* ... */ }
      if (event.type === "session.deleted") { /* ... */ }
    },

    // Hook handlers (intercept operations)
    "tool.execute.before": async (input, output) => { /* ... */ },
    "tool.execute.after": async (input, output) => { /* ... */ },
    "chat.message": async (input, output) => { /* ... */ },
    "experimental.chat.messages.transform": async (input, output) => { /* ... */ },
    "experimental.chat.system.transform": async (input, output) => { /* ... */ },

    // Configuration transformation
    config: async (configOutput) => {
      // Register slash commands
      configOutput.command ??= {};
      configOutput.command["my-command"] = {
        description: "My custom command",
        template: "Execute something with $ARGUMENTS",
      };
    },
  };
};

export default MyPlugin;
```

---

## Plugin Lifecycle

1. **Initialization**: Plugin function called once when OpenCode loads
2. **Configuration**: `config` hook runs to register commands/settings
3. **Runtime**: Hooks and tools are invoked as needed
4. **Cleanup**: Handle via `beforeExit` process event

---

## Context Object (ctx)

The `ctx` object provides:

| Property | Type | Description |
|----------|------|-------------|
| `ctx.client` | `OpencodeClient` | API client for sessions, TUI, config |
| `ctx.directory` | `string` | Project working directory |
| `ctx.$` | `Shell` | Bun shell helper for commands |

---

## Return Interface

| Key | Type | Description |
|-----|------|-------------|
| `tool` | `Record<string, Tool>` | Tools available to AI |
| `event` | `Function` | Session lifecycle events |
| `tool.execute.before` | `Function` | Pre-tool interception |
| `tool.execute.after` | `Function` | Post-tool observation |
| `chat.message` | `Function` | Message handler |
| `config` | `Function` | Register commands/settings |

---

## Source Reference

- `oh-my-opencode/src/index.ts` - Gold standard architecture
- `background-agents/src/plugin/background-agents.ts` - Simpler example
- `handoff/src/plugin.ts` - Minimal example
