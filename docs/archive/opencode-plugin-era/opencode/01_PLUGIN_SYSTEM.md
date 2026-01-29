# OpenCode Plugin System

> Plugins extend OpenCode with custom hooks, tools, agents, and behavior.

---

## Plugin Entry Point

Plugins export an async function receiving a context object:

```typescript
import type { Plugin } from "@opencode-ai/plugin"

export const MyPlugin: Plugin = async (ctx) => {
  const { project, client, $, directory, worktree } = ctx

  return {
    // Register hooks, agents, tools
  }
}

export default MyPlugin
```

---

## Context Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `project` | `Project` | Current project information |
| `client` | `Client` | SDK client for AI interaction |
| `$` | `Shell` | Bun's shell API for command execution |
| `directory` | `string` | Current working directory |
| `worktree` | `string` | Git worktree path |

---

## Plugin Locations

### Local Plugins (Project-Specific)
```
.opencode/plugins/
├── my-plugin/
│   ├── index.ts       # Plugin entry point
│   └── package.json   # Optional dependencies
```

### Global Plugins
```
~/.config/opencode/plugins/
├── my-global-plugin/
│   └── index.ts
```

### NPM Packages
In `opencode.json`:
```json
{
  "plugin": [
    "opencode-plugin-name",
    "@my-org/custom-plugin"
  ]
}
```

NPM packages auto-install to `~/.cache/opencode/node_modules/`.

---

## Load Order

1. Global config (`~/.config/opencode/opencode.json`)
2. Project config (`opencode.json`)
3. Global plugins (`~/.config/opencode/plugins/`)
4. Project plugins (`.opencode/plugins/`)

Later plugins override earlier ones.

---

## Plugin Return Object

```typescript
export const MyPlugin: Plugin = async (ctx) => {
  return {
    // Register agents
    agent: {
      "my-agent": myAgentDefinition,
    },

    // Register tools
    tool: {
      "my-tool": myToolDefinition,
    },

    // Register event hooks
    "session.created": async ({ event }) => {
      // Handle session creation
    },

    "tool.execute.before": async ({ event }) => {
      // Before any tool executes
    },

    "tool.execute.after": async ({ event }) => {
      // After any tool executes
    },
  }
}
```

---

## Dependencies

Add a `package.json` to your plugin directory:

```json
{
  "name": "my-plugin",
  "dependencies": {
    "zod": "^3.22.0",
    "date-fns": "^3.0.0"
  }
}
```

Bun automatically installs dependencies when the plugin loads.

---

## TypeScript Types

```typescript
import type {
  Plugin,
  PluginContext,
  Tool,
  Agent,
  HookHandler,
} from "@opencode-ai/plugin"
```

---

## Example: Complete Plugin

```typescript
import type { Plugin } from "@opencode-ai/plugin"
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export const Delta9: Plugin = async (ctx) => {
  const { client, directory } = ctx

  // Log on load
  await client.app.log("info", "Delta9 plugin loaded")

  // Custom tool
  const missionTool = tool({
    description: "Create a new mission",
    args: {
      description: z.string().describe("Mission description"),
    },
    async execute({ description }, context) {
      // Implementation
      return { status: "created", description }
    },
  })

  return {
    // Replace default build agent
    agent: {
      build: {
        name: "commander",
        mode: "primary",
        model: "anthropic/claude-opus-4-5",
        prompt: "You are Commander...",
      },
    },

    // Register tools
    tool: {
      mission_create: missionTool,
    },

    // Hook handlers
    "session.created": async ({ event }) => {
      await client.app.log("info", `Session created: ${event.id}`)
    },

    "session.idle": async ({ event }) => {
      // Check for pending mission tasks
    },
  }
}

export default Delta9
```

---

## Best Practices

1. **Use TypeScript** for type safety with `@opencode-ai/plugin`
2. **Validate inputs** with Zod schemas
3. **Log with `client.app.log()`** for debugging
4. **Keep plugins focused** - single responsibility
5. **Handle errors gracefully** - don't crash the host

---

## Reference

- [Official Plugin Docs](https://opencode.ai/docs/plugins/)
- [oh-my-opencode](https://github.com/code-yeongyu/oh-my-opencode) - Reference implementation
