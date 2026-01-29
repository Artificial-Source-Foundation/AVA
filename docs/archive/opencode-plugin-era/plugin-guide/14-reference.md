# Quick Reference

## Hook Types

| Hook | Input | Output | Use Case |
|------|-------|--------|----------|
| `event` | `{ event: { type, properties } }` | - | Session lifecycle, errors |
| `tool.execute.before` | `{ tool, sessionID }` | `{ args }` | Intercept, modify, block |
| `tool.execute.after` | `{ tool, sessionID }` | `{ result }` | Observe results |
| `chat.message` | `{ sessionID, message }` | `{ message, parts }` | Modify message |
| `experimental.chat.messages.transform` | `{}` | `{ messages[] }` | Transform history |
| `experimental.chat.system.transform` | `{}` | `{ system[] }` | Inject system prompt |
| `config` | - | `{ command, ... }` | Register commands |

---

## Event Types

| Event | Properties | When |
|-------|------------|------|
| `session.created` | `sessionID` | New session started |
| `session.idle` | `sessionID` | Session finished processing |
| `session.deleted` | `info: { id }` | Session removed |
| `session.error` | `sessionID, error` | Error occurred |
| `permission.updated` | - | Permissions changed |

---

## Tool Schema Types

```typescript
tool.schema.string()
tool.schema.number()
tool.schema.boolean()
tool.schema.array(itemSchema)
tool.schema.object({ key: schema })
tool.schema.enum(["a", "b", "c"])
tool.schema.optional()
tool.schema.describe("Description for AI")
```

---

## Client API

```typescript
// Sessions
client.session.create({ body: { title } })
client.session.get({ path: { id } })
client.session.messages({ path: { id }, query: { limit } })
client.session.prompt({ path: { id }, body: { parts, agent } })
client.session.update({ path: { id }, body: { title } })

// TUI
client.tui.showToast({ body: { title, message, variant } })
client.tui.executeCommand({ body: { command } })
client.tui.appendPrompt({ body: { text } })

// Config
client.config.get()
client.provider.list()
```

---

## File Locations

| Purpose | Path |
|---------|------|
| User config | `~/.config/opencode/{plugin}.jsonc` |
| Project config | `.opencode/{plugin}.jsonc` |
| User skills | `~/.config/opencode/skills/` |
| Project skills | `.opencode/skills/` |
| Logs | `~/.local/share/opencode/logs/` |
| Plugin data | `~/.local/share/opencode/plugins/{name}/` |

---

## Context Object

| Property | Type | Description |
|----------|------|-------------|
| `ctx.client` | `OpencodeClient` | API client |
| `ctx.directory` | `string` | Project directory |
| `ctx.$` | `Shell` | Bun shell helper |

---

## Tool Execute Context

| Property | Type | Description |
|----------|------|-------------|
| `context.sessionID` | `string` | Current session ID |

---

## Common Imports

```typescript
import type { Plugin, PluginInput } from "@opencode-ai/plugin";
import { tool } from "@opencode-ai/plugin";
import { z } from "zod";
```

---

## Return Interface Keys

| Key | Type | Required |
|-----|------|----------|
| `tool` | `Record<string, Tool>` | No |
| `event` | `Function` | No |
| `tool.execute.before` | `Function` | No |
| `tool.execute.after` | `Function` | No |
| `chat.message` | `Function` | No |
| `experimental.chat.messages.transform` | `Function` | No |
| `experimental.chat.system.transform` | `Function` | No |
| `config` | `Function` | No |

---

## Source Reference Plugins

| Pattern | Best Example |
|---------|-------------|
| Plugin architecture | `oh-my-opencode/src/index.ts` |
| Configuration | `oh-my-opencode/src/plugin-config.ts` |
| Background tasks | `background-agents/src/plugin/` |
| Memory persistence | `agent-memory/src/` |
| Vector search | `opencode-mem/src/` |
| Skills system | `opencode-skillful/src/` |
| Safety hooks | `safety-net/src/` |
| Terminal spawning | `worktree/src/lib/spawn/` |
| Session handoff | `handoff/src/` |
| Notifications | `opencode-notify/src/` |
| Token analysis | `tokenscope/plugin/` |

All reference code at `../REFERENCE_CODE/` - see README.md for full index.
