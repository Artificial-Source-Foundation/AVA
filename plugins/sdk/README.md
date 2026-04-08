# @ava-ai/plugin

TypeScript SDK for building [AVA](https://github.com/ASF-GROUP/AVA) plugins.

AVA plugins are standalone processes that communicate with the AVA agent via JSON-RPC 2.0 over stdio. This SDK handles all the protocol details so you can focus on your plugin logic.

## Quick start

```bash
mkdir my-plugin && cd my-plugin
npm init -y
npm install @ava-ai/plugin
```

Create `plugin.toml`:

```toml
[plugin]
name = "my-plugin"
version = "0.1.0"
description = "My first AVA plugin"

[runtime]
command = "node"
args = ["index.js"]

[hooks]
subscribe = ["tool.before"]
```

Create `index.ts`:

```typescript
import { createPlugin } from "@ava-ai/plugin";

createPlugin({
  "tool.before": async (ctx, params) => {
    console.error(`[my-plugin] tool called: ${params.tool}`);
    return { args: params.args };
  },
});
```

Compile and install:

```bash
npx tsc index.ts --outDir .
# Copy the plugin directory to ~/.ava/plugins/my-plugin/
```

## Hook types

| Hook | Type | Description |
|------|------|-------------|
| `auth` | request | Provide credentials for a provider |
| `auth.refresh` | request | Refresh expired tokens |
| `request.headers` | request | Inject headers into LLM API calls |
| `tool.before` | request | Intercept tool call before execution |
| `tool.after` | request | Intercept tool result after execution |
| `agent.before` | notification | Agent turn starting |
| `agent.after` | notification | Agent turn completed |
| `session.start` | notification | Session created or resumed |
| `session.end` | notification | Session ended |
| `config` | request | Modify config at runtime |
| `event` | notification | Broadcast agent events |
| `shell.env` | request | Inject env vars into bash tool |

**Request hooks** receive a JSON-RPC request with an `id` and must return a result (or throw an error). **Notification hooks** are fire-and-forget; any return value is ignored.

## Handler signature

```typescript
type HookHandler = (
  ctx: PluginContext,
  params: Record<string, unknown>
) => Promise<Record<string, unknown> | void>;
```

The `PluginContext` is populated from the `initialize` request AVA sends when starting the plugin:

```typescript
interface PluginContext {
  project: { directory: string; name: string };
  config: Record<string, unknown>;
  tools: string[];
}
```

## Blocking tool calls

Return an error from `tool.before` to block a tool call:

```typescript
"tool.before": async (ctx, params) => {
  if (params.tool === "bash" && String(params.args?.command).includes("rm -rf")) {
    throw new Error("Blocked: dangerous command");
  }
  return { args: params.args };
}
```

## Plugin installation

Place your plugin directory (containing `plugin.toml`) in either:

- `~/.ava/plugins/<name>/` (global)
- `.ava/plugins/<name>/` (project-local)

AVA discovers and loads plugins automatically on startup.

## App host seams

Plugins can also expose namespaced app capabilities through the host seam.

Supported v1 capability types:

1. commands
2. routes
3. events
4. mounts

Example:

```typescript
createPlugin(
  {},
  {
    capabilities: {
      commands: [{ name: 'demo.ping', description: 'Ping the plugin' }],
      routes: [{ path: '/status', method: 'GET', description: 'Plugin status' }],
      events: [{ name: 'demo.updated', description: 'Plugin update event' }],
      mounts: [{ id: 'demo.settings', location: 'settings.section', label: 'Demo' }],
    },
    commands: {
      'demo.ping': async () => ({
        result: { ok: true },
        emittedEvents: [{ event: 'demo.updated', payload: { ok: true } }],
      }),
    },
    routes: {
      'GET /status': async () => ({ result: { status: 'ok' } }),
    },
  }
)
```

## Protocol

Plugins communicate via JSON-RPC 2.0 over stdio with Content-Length framing (the same wire format used by LSP and MCP):

```
Content-Length: 42\r\n
\r\n
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{...}}
```

The SDK handles all framing automatically. Plugin log output should go to stderr (not stdout).
