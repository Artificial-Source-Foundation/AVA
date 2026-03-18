# Plugin System

> JSON-RPC plugin architecture for extending AVA

## Architecture

Plugins are standalone processes that communicate with AVA via JSON-RPC 2.0 over stdio using Content-Length framing (identical to LSP/MCP wire format).

**Lifecycle:**
1. AVA discovers plugins from `~/.ava/plugins/` or `.ava/plugins/`
2. Spawns process defined in `plugin.toml` `[runtime]` section
3. Sends `initialize` request with project context
4. Dispatches hook events to subscribed plugins
5. Sends `shutdown` notification on exit

**Wire format:**
```
Content-Length: 42\r\n
\r\n
{"jsonrpc":"2.0","id":1,"method":"hook/tool.before","params":{...}}
```

## TypeScript SDK

Location: `plugins/sdk/`

| File | Purpose |
|------|---------|
| `dist/index.js` | Compiled SDK (no runtime dependencies) |
| `dist/index.d.ts` | TypeScript type definitions |
| `package.json` | Package: `@ava-ai/plugin` |

**Usage:**
```typescript
import { createPlugin } from "@ava-ai/plugin";

createPlugin({
  "tool.before": async (ctx, params) => {
    return { args: params.args };
  },
});
```

## Python SDK

Location: `plugins/sdk-python/`

| File | Purpose |
|------|---------|
| `ava_plugin/sdk.py` | Core SDK implementation |
| `ava_plugin/__init__.py` | Package exports (`create_plugin`, `PluginContext`) |
| `pyproject.toml` | Package: `ava-plugin` |

**Usage:**
```python
from ava_plugin import create_plugin

def on_tool_before(ctx, params):
    return {"args": params.get("args", {})}

create_plugin({"tool.before": on_tool_before})
```

## Hook Types

| Hook | Type | When Called | Purpose |
|------|------|-------------|---------|
| `auth` | request | LLM provider needs credentials | Return auth credentials |
| `auth.refresh` | request | Token expired | Refresh and return new credentials |
| `request.headers` | request | Making LLM API request | Inject HTTP headers |
| `tool.before` | request | Before tool execution | Modify args or block (throw error) |
| `tool.after` | request | After tool execution | Inspect/modify result |
| `agent.before` | notification | Agent turn starts | Logging/metrics |
| `agent.after` | notification | Agent turn completes | Logging/metrics |
| `session.start` | notification | Session created | Initialize plugin state |
| `session.end` | notification | Session ended | Cleanup, flush logs |
| `config` | request | Reading config value | Provide config |
| `event` | notification | Agent event fired | React to events |
| `shell.env` | request | Before bash execution | Inject env vars |

**Notification hooks:** Fire-and-forget, no response expected.  
**Request hooks:** Must return result or throw error (blocking for `tool.before`).

**Handler signature:**
```typescript
(ctx: PluginContext, params: Record<string, unknown>) => Promise<Record<string, unknown> | void>
```

**PluginContext fields:**
- `project.directory`: Project root path
- `project.name`: Project name
- `config`: Plugin-specific config
- `tools`: Available tool names

## Example Plugins

Location: `plugins/examples/`

| Example | Language | Shows |
|---------|----------|-------|
| `hello-plugin/` | TypeScript | Basic session hooks |
| `hello-python/` | Python | SDK usage, tool.before/after |
| `copilot-auth/` | JavaScript | Device code OAuth flow, auth hooks |
| `env-guard/` | TypeScript | Blocking dangerous file access |
| `request-logger/` | TypeScript | Persistent logging to `.ava/tool-log.jsonl` |
| `tool-timer/` | JavaScript | Timing metrics, bash command filtering |

## Manifest Format

Each plugin requires `plugin.toml`:

```toml
[plugin]
name = "my-plugin"
version = "0.1.0"
description = "What it does"

[runtime]
command = "node"          # or "python3", "./binary"
args = ["index.js"]       # command arguments

[runtime.env]             # optional env vars
KEY = "value"

[hooks]
subscribe = ["tool.before", "session.start"]
```

## Quick Reference: Creating a Plugin

**1. Install SDK**

TypeScript:
```bash
npm init -y
npm install @ava-ai/plugin
```

Python:
```bash
pip install /path/to/ava-plugin
```

**2. Implement hooks**

TypeScript (`index.ts`):
```typescript
import { createPlugin } from "@ava-ai/plugin";

createPlugin({
  "session.start": async (ctx, params) => {
    console.error(`Session started: ${ctx.project.name}`);
  },
  "tool.before": async (ctx, params) => {
    if (params.tool === "bash") {
      const cmd = String(params.args?.command);
      if (cmd.includes("rm -rf /")) {
        throw new Error("Blocked: dangerous command");
      }
    }
    return { args: params.args };
  },
});
```

Python (`plugin.py`):
```python
from ava_plugin import create_plugin

def on_session_start(ctx, params):
    import sys
    sys.stderr.write(f"Session: {ctx.project['name']}\n")
    return {}

create_plugin({"session.start": on_session_start})
```

**3. Register plugin**

```bash
# Global install
mkdir -p ~/.ava/plugins/my-plugin
cp plugin.toml index.js ~/.ava/plugins/my-plugin/

# Or project-local
mkdir -p .ava/plugins/my-plugin
cp plugin.toml index.js .ava/plugins/my-plugin/
```

**4. Test locally**

```bash
# Run plugin standalone to verify it starts
cd ~/.ava/plugins/my-plugin
node index.js

# Type initialize message (Ctrl+D to test):
# Content-Length: 45
# 
# {"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
```

## Security Notes

- Plugins run as child processes with inherited env (minus sensitive vars like `OPENAI_API_KEY`)
- `tool.before` can block operations by throwing errors
- Use `env-guard` pattern to prevent credential file access
- Never log to stdout (use stderr for logging)

## Rust Runtime Integration

Location: `crates/ava-plugin/`

| File | Purpose |
|------|---------|
| `manager.rs` | PluginManager: lifecycle, discovery, auth sub-protocol |
| `runtime.rs` | PluginProcess: spawn, JSON-RPC framing, shutdown |
| `hooks.rs` | HookEvent types, HookDispatcher, auth types |
| `manifest.rs` | PluginManifest parsing from `plugin.toml` |
| `discovery.rs` | Plugin discovery from plugin directories |

**PluginManager methods:**
- `load_plugins(dirs)` - Discover and spawn plugins
- `trigger_hook(event, params)` - Dispatch to subscribers
- `get_auth_methods(provider)` - Query auth capabilities
- `authorize(provider, method_index, user_input)` - Execute auth flow
- `refresh_auth(provider, refresh_token)` - Refresh credentials
- `shutdown_all()` - Graceful shutdown
