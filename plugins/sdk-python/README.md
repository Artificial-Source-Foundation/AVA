# ava-plugin (Python SDK)

Python SDK for building AVA plugins. Zero dependencies — pure Python stdlib.

Plugins communicate with AVA via JSON-RPC 2.0 over stdio using Content-Length
framing (identical to LSP/MCP wire format).

## Quick Start

```python
import sys
from ava_plugin import create_plugin

def on_session_start(ctx, params):
    sys.stderr.write("Session started!\n")
    return {}

def on_tool_before(ctx, params):
    tool = params.get("tool", "")
    if tool == "bash" and "rm -rf" in params.get("args", {}).get("command", ""):
        raise Exception("Blocked: dangerous command")
    return {"args": params.get("args", {})}

create_plugin({
    "session.start": on_session_start,
    "tool.before": on_tool_before,
})
```

Save as `plugin.py`, then create a `plugin.toml`:

```toml
[plugin]
name = "my-plugin"
version = "0.1.0"
description = "My custom AVA plugin"

[runtime]
command = "python3"
args = ["plugin.py"]

[hooks]
subscribe = ["session.start", "tool.before"]
```

Install into AVA:

```bash
ava plugin add /path/to/my-plugin
```

## Hook Reference

| Hook | Params | Return | Description |
|------|--------|--------|-------------|
| `session.start` | `goal`, `session_id` | `{}` | Session begins |
| `session.end` | `session_id` | `{}` | Session ends |
| `tool.before` | `tool`, `args`, `call_id` | `{"args": {...}}` | Before tool execution (modify args or raise to block) |
| `tool.after` | `tool`, `args`, `result`, `call_id` | `{}` | After tool execution |
| `agent.before` | `messages` | `{}` | Before agent turn |
| `agent.after` | `response` | `{}` | After agent turn |
| `auth` | `provider` | `{"token": "..."}` | Provide auth credentials |
| `auth.refresh` | `provider`, `token` | `{"token": "..."}` | Refresh expired credentials |
| `request.headers` | `url`, `method` | `{"headers": {...}}` | Inject HTTP headers |
| `config` | `key` | `{"value": ...}` | Provide config values |
| `event` | `type`, `data` | `{}` | React to events |
| `shell.env` | `command` | `{"env": {...}}` | Inject env vars for shell commands |

## API

### `create_plugin(hooks)`

Start the plugin event loop. Blocks forever (until AVA sends `shutdown`).

- `hooks`: dict mapping hook names to handler functions
- Each handler receives `(ctx, params)` where:
  - `ctx` is a `PluginContext` with `.project`, `.config`, `.tools`
  - `params` is a dict of hook-specific parameters
- Return a dict (or `None`) for success
- Raise an `Exception` to send an error response (blocks the action for `tool.before`)

### `PluginContext`

Populated during initialization:

- `ctx.project` — `{"directory": "/path/to/project", "name": "my-project"}`
- `ctx.config` — plugin-specific config from `plugin.toml`
- `ctx.tools` — list of available tool names

## Logging

Use `sys.stderr.write()` for logging. Stdout is reserved for the JSON-RPC protocol.
Never use `print()` without `file=sys.stderr`.
