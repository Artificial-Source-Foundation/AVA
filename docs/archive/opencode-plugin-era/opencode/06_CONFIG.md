# OpenCode Configuration

> Configuration controls OpenCode behavior, models, agents, and integrations.

---

## Configuration Locations

Files are merged in order (later overrides earlier):

| Priority | Location | Purpose |
|----------|----------|---------|
| 1 | `.well-known/opencode` | Remote org config |
| 2 | `~/.config/opencode/opencode.json` | Global user config |
| 3 | `OPENCODE_CONFIG` env var | Custom config path |
| 4 | `opencode.json` | Project config |
| 5 | `.opencode/` directories | Project extensions |
| 6 | `OPENCODE_CONFIG_CONTENT` env var | Inline config |

Supports JSON and JSONC (comments allowed).

---

## Core Configuration Options

### Model Settings

```json
{
  "model": "anthropic/claude-sonnet-4-5",
  "small_model": "anthropic/claude-haiku-4-5",
  "provider": {
    "timeout": 30000,
    "cache": true
  }
}
```

### Agent Configuration

```json
{
  "agent": {
    "my-agent": {
      "description": "Custom agent",
      "mode": "subagent",
      "model": "anthropic/claude-opus-4-5",
      "prompt": "You are...",
      "temperature": 0.7,
      "tools": {
        "write": true,
        "bash": false
      }
    }
  },
  "default_agent": "build"
}
```

### Tool Permissions

```json
{
  "permission": {
    "bash": "ask",
    "write": "ask",
    "read": "allow",
    "mcp_*": "ask"
  },
  "tools": {
    "todowrite": false,
    "skill": true
  }
}
```

### Plugin Loading

```json
{
  "plugin": [
    "opencode-plugin-name",
    "@my-org/custom-plugin",
    "./local-plugin"
  ]
}
```

### MCP Servers

```json
{
  "mcp": {
    "context7": {
      "type": "remote",
      "url": "https://mcp.context7.io"
    }
  }
}
```

### Custom Commands

```json
{
  "command": {
    "test": {
      "template": "Run all tests and report results",
      "description": "Run test suite"
    }
  }
}
```

---

## UI & Experience

```json
{
  "theme": "dark",
  "autoupdate": true,
  "tui": {
    "scroll_speed": 3,
    "diff_style": "unified"
  }
}
```

---

## Sharing & Server

```json
{
  "share": "manual",
  "server": {
    "port": 4096,
    "hostname": "127.0.0.1",
    "mdns": false,
    "cors": ["http://localhost:3000"]
  }
}
```

---

## Provider Management

```json
{
  "enabled_providers": ["anthropic", "openai"],
  "disabled_providers": ["azure"]
}
```

---

## Variable Substitution

Use variables in config values:

| Syntax | Description |
|--------|-------------|
| `{env:VAR_NAME}` | Environment variable |
| `{file:path/to/file}` | File contents |

```json
{
  "mcp": {
    "my-server": {
      "headers": {
        "Authorization": "Bearer {env:API_KEY}"
      }
    }
  },
  "agent": {
    "my-agent": {
      "prompt": "{file:./prompts/my-agent.txt}"
    }
  }
}
```

---

## Complete Example

```json
{
  "$schema": "https://opencode.ai/schema/config.json",

  // Model selection
  "model": "anthropic/claude-sonnet-4-5",
  "small_model": "anthropic/claude-haiku-4-5",

  // Plugins
  "plugin": ["delta9"],

  // Custom agents
  "agent": {
    "commander": {
      "description": "Strategic planner",
      "mode": "primary",
      "model": "anthropic/claude-opus-4-5",
      "prompt": "{file:./prompts/commander.md}",
      "tools": {
        "write": false,
        "bash": false
      }
    }
  },

  // Permissions
  "permission": {
    "bash": "ask",
    "write": "ask"
  },

  // MCP servers
  "mcp": {
    "context7": {
      "type": "remote",
      "url": "https://mcp.context7.io",
      "enabled": true
    }
  },

  // Commands
  "command": {
    "sitrep": {
      "template": "Show current mission status from .delta9/mission.json",
      "description": "Mission status"
    }
  },

  // UI
  "theme": "dark",
  "autoupdate": "notify"
}
```

---

## Project vs Global Config

| Setting | Project | Global |
|---------|---------|--------|
| Model defaults | Override | Base |
| Agents | Add/override | Base |
| Plugins | Add | Base |
| Permissions | Override | Base |
| Theme | Override | Base |

---

## Reference

- [Official Config Docs](https://opencode.ai/docs/config/)
