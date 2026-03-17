# AVA Plugin System Design

> Status: Design (not yet implemented)
> Phase 1 target: v3.0

## Overview

AVA's plugin system has two tiers:

1. **Simple**: TOML custom tools — define a shell command + params in a `.toml` file. Any language.
2. **Power**: External process plugins via JSON-RPC — full hook system for auth, tools, events, config. TypeScript/Python/any language.

## Tier 1: TOML Custom Tools (exists today)

Drop a `.toml` file in `~/.ava/tools/` (global) or `.ava/tools/` (project):

```toml
name = "deploy"
description = "Deploy to environment"

[[params]]
name = "env"
type = "string"
required = true
description = "Target (staging/production)"

[execution]
type = "shell"
command = "./deploy.sh {{env}}"
timeout_secs = 120
```

The agent can call this like any built-in tool. Params are shell-escaped automatically.

## Tier 2: Power Plugins (planned)

### Plugin Structure

A plugin is a directory with `plugin.toml`:

```toml
[plugin]
name = "copilot-auth"
version = "0.1.0"
description = "GitHub Copilot auth for AVA"

[runtime]
command = "node"
args = ["index.js"]

[hooks]
subscribe = ["auth", "request.headers", "tool.before"]
```

### Communication

JSON-RPC 2.0 over stdio (same protocol as MCP servers). AVA spawns the plugin as a child process.

Lifecycle:
1. AVA spawns plugin → sends `initialize` with project context
2. Plugin responds with capabilities
3. AVA sends hook calls as events occur
4. AVA sends `shutdown` on exit

### Hook System

| Hook | Type | Purpose |
|------|------|---------|
| `auth` | Request/Response | Provide credentials for a provider |
| `auth.refresh` | Request/Response | Refresh expired tokens |
| `request.headers` | Request/Response | Inject headers into LLM API calls |
| `tool.before` | Request/Response | Intercept tool call before execution |
| `tool.after` | Request/Response | Intercept tool result after execution |
| `agent.before` | Notification | Agent turn starting |
| `agent.after` | Notification | Agent turn completed |
| `session.start` | Notification | Session created/resumed |
| `session.end` | Notification | Session ended |
| `config` | Request/Response | Modify config at runtime |
| `event` | Notification | Broadcast any AgentEvent |
| `shell.env` | Request/Response | Inject env vars into bash |

Request/Response hooks expect a reply (5s timeout). Notification hooks are fire-and-forget.

### TypeScript Plugin Example

```typescript
import { createPlugin } from "@ava-ai/plugin";

export default createPlugin({
  hooks: {
    "auth": async (ctx, input) => {
      return { provider: "copilot", apiKey: "...", headers: {...} };
    },
    "tool.before": async (ctx, input) => {
      if (input.tool === "read" && input.args.file_path.includes(".env")) {
        throw new Error("Blocked: do not read .env files");
      }
      return { args: input.args };
    },
  },
});
```

`@ava-ai/plugin` is a ~50-line npm package wrapping the JSON-RPC stdio loop.

### Installation

```bash
ava plugin add @ava/copilot-auth    # from npm
ava plugin add ./local-plugin       # local directory
ava plugin list
ava plugin remove copilot-auth
```

Installed to `~/.ava/plugins/<name>/`. Config in `~/.ava/plugins.toml`.

### Discovery

Plugin directories scanned at startup:
- `~/.ava/plugins/` (global, installed)
- `.ava/plugins/` (project-local)

Only plugins with matching `hooks.subscribe` entries are spawned (lazy — no wasted processes).

### OpenCode Compatibility (Phase 3)

A bridge script (`ava-opencode-bridge.js`) adapts OpenCode plugins to AVA's JSON-RPC protocol. Configure with `compat = "opencode"` in `plugin.toml`.

## Implementation Plan

### Phase 1: Core runtime
- New crate `ava-plugin` (manifest, discovery, runtime, hooks, manager)
- Reuse `StdioTransport` from `ava-mcp`
- Wire into `AgentStack.run()` and tool middleware

### Phase 2: CLI + SDK
- `ava plugin add/remove/list` commands
- `@ava-ai/plugin` npm package
- `ava-plugin-sdk` Python package

### Phase 3: Advanced
- OpenCode bridge
- Plugin marketplace
- Experimental hooks (compaction, message transform)
