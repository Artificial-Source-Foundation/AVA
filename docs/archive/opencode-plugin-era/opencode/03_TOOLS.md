# OpenCode Tools

> Tools are functions that LLMs can invoke during conversations.

---

## Built-in Tools

| Tool | Description | Default Permission |
|------|-------------|-------------------|
| `bash` | Execute shell commands | ask |
| `read` | Read file contents | allow |
| `write` | Create/overwrite files | ask |
| `edit` | Modify files with string replacements | ask |
| `patch` | Apply patches to files | ask |
| `glob` | Find files by pattern | allow |
| `grep` | Search file contents with regex | allow |
| `list` | List files and directories | allow |
| `lsp` | Code intelligence (experimental) | ask |
| `skill` | Load SKILL.md content | allow |
| `todowrite` | Create/update task lists | allow |
| `todoread` | Read existing todo lists | allow |
| `webfetch` | Fetch web content | ask |
| `question` | Ask user questions | allow |

---

## Tool Permissions

Three-tier permission model:

| Level | Behavior |
|-------|----------|
| `allow` | Immediate access |
| `ask` | Prompts user for approval |
| `deny` | Tool unavailable |

Configure in `opencode.json`:

```json
{
  "permission": {
    "bash": "ask",
    "write": "ask",
    "read": "allow",
    "mymcp_*": "ask"
  }
}
```

Wildcards supported for batch control.

---

## Custom Tools

### File Location

- **Project**: `.opencode/tools/`
- **Global**: `~/.config/opencode/tools/`

Filename becomes tool name (e.g., `database.ts` → `database` tool).

### Tool Definition Structure

```typescript
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export default tool({
  description: "What this tool does",
  args: {
    query: z.string().describe("SQL query to execute"),
    limit: z.number().optional().describe("Result limit"),
  },
  async execute(args, context) {
    const { query, limit } = args
    const { agent, sessionID, messageID } = context

    // Implementation
    const result = await runQuery(query, limit)

    return result // String or serializable data
  },
})
```

### Schema Helpers

Use `tool.schema` or import Zod directly:

```typescript
import { tool } from "@opencode-ai/plugin"

const myTool = tool({
  args: {
    name: tool.schema.string(),
    count: tool.schema.number(),
    enabled: tool.schema.boolean(),
    tags: tool.schema.array(tool.schema.string()),
    config: tool.schema.object({
      key: tool.schema.string(),
    }),
  },
  // ...
})
```

---

## Execute Context

| Property | Type | Description |
|----------|------|-------------|
| `agent` | string | Agent that invoked the tool |
| `sessionID` | string | Current session ID |
| `messageID` | string | Current message ID |

---

## Multiple Exports

Multiple exports create separate tools:

```typescript
// file: database.ts

import { tool } from "@opencode-ai/plugin"

// Creates "database" tool
export default tool({
  description: "Run SQL query",
  // ...
})

// Creates "database_migrate" tool
export const migrate = tool({
  description: "Run migrations",
  // ...
})

// Creates "database_seed" tool
export const seed = tool({
  description: "Seed database",
  // ...
})
```

---

## Multi-Language Support

Tools can invoke scripts in any language:

```typescript
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export default tool({
  description: "Run Python analysis",
  args: {
    input: z.string(),
  },
  async execute({ input }, ctx) {
    // Call Python script
    const result = await Bun.$`python3 .opencode/tools/analyze.py ${input}`.text()
    return result
  },
})
```

---

## Tool Registration in Plugins

```typescript
import type { Plugin } from "@opencode-ai/plugin"
import { tool } from "@opencode-ai/plugin"
import { z } from "zod"

export const MyPlugin: Plugin = async (ctx) => {
  const missionCreate = tool({
    description: "Create a new mission",
    args: {
      description: z.string().describe("Mission description"),
      mode: z.enum(["none", "quick", "standard", "xhigh"]).optional(),
    },
    async execute({ description, mode }, context) {
      // Implementation
      return { id: "mission_123", description, mode }
    },
  })

  const missionStatus = tool({
    description: "Get mission status",
    args: {},
    async execute(args, context) {
      // Implementation
      return { status: "in_progress", progress: 0.45 }
    },
  })

  return {
    tool: {
      mission_create: missionCreate,
      mission_status: missionStatus,
    },
  }
}
```

---

## Disabling Tools for Agents

```json
{
  "agent": {
    "plan": {
      "tools": {
        "write": false,
        "edit": false,
        "bash": false
      }
    }
  }
}
```

---

## Reference

- [Official Tools Docs](https://opencode.ai/docs/tools/)
- [Custom Tools Docs](https://opencode.ai/docs/custom-tools/)
